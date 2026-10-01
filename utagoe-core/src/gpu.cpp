// Direct3D 11 compute による GPU 処理。
// d3d11.dll / dxgi.dll / d3dcompiler_47.dll は実行時に System32 から読み込む。どれかが無い、ハードウェアの
// feature level 11.0 対応 GPU が無い、shader の compile に失敗した、といった場合は CPU で処理する。
// ソフトウェア描画 (WARP / Microsoft Basic Render Driver) は CPU と変わらないので使わない。

#include "gpu.h"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace utagoe {
namespace gpu {
namespace {


// 位置探索。group は (sample 256 個, 候補 1 つ)。部分和を 64-bit (lo, hi) で返す。
const char* kSearchHlsl = R"(
cbuffer P : register(b0) { uint count; uint nch; uint stride; uint ovs; uint shift; uint groups; uint instLen; uint pad; };
StructuredBuffer<int> O : register(t0);
StructuredBuffer<int> I : register(t1);
StructuredBuffer<int> C : register(t2);
RWStructuredBuffer<uint2> Part : register(u0);
groupshared uint2 sh[256];

// 仮想格子 v のインスト値。CPU の補間と同じく、最後の実 sample より後ろは 0。
int inst(uint c, int v) {
    if (ovs == 1) return I[c * instLen + v];
    int j = v >> shift;
    int k = v & (int)(ovs - 1);
    if (j >= (int)instLen - 1) return 0;
    int a = I[c * instLen + j];
    if (k == 0) return a;
    int d = I[c * instLen + j + 1] - a;
    int dq = d >> shift;                 // d = dq * ovs + dr, 0 <= dr < ovs
    int dr = d - (dq << shift);
    int num = dr * k;
    int qt = dq * k + (num >> shift);
    int r = num & (int)(ovs - 1);
    int hn = (int)(ovs >> 1);
    if (r > hn || (r == hn && ((a + qt) & 1) != 0)) qt += 1;
    return a + qt;
}

[numthreads(256, 1, 1)]
void main(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID) {
    uint i = gid.x * 256 + tid.x;
    int pos = C[gid.y];
    uint t = 0;
    if (i < count) {
        for (uint c = 0; c < nch; ++c) {
            int d = O[c * count + i] - inst(c, pos + (int)(stride * i));
            int dp = 0;
            if (i > 0) dp = O[c * count + i - 1] - inst(c, pos + (int)(stride * (i - 1)));
            t += (uint)abs(d) + (uint)abs(d - dp);
        }
    }
    sh[tid.x] = uint2(t, 0);
    GroupMemoryBarrierWithGroupSync();
    for (uint s = 128; s > 0; s >>= 1) {
        if (tid.x < s) {
            uint2 a = sh[tid.x], b = sh[tid.x + s];
            uint lo = a.x + b.x;
            sh[tid.x] = uint2(lo, a.y + b.y + (lo < a.x ? 1u : 0u));
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (tid.x == 0) Part[gid.y * groups + gid.x] = sh[0];
}
)";

// level 推定。group は (sample 256 個, 候補 1 つ)。インストの補間は探索と同じ整数演算。
// 各項は 16-bit 経路なら整数に丸めた |d|、それ以外は 1/256 sample 単位に丸めた |d| で、部分和は 64-bit 整数で持つ。
const char* kLevelHlsl = R"(
cbuffer P : register(b0) { uint count; uint nch; uint stride; uint ovs; uint shift; uint groups; uint instLen; int pos; uint quant; uint3 pad; };
StructuredBuffer<int> O : register(t0);
StructuredBuffer<int> I : register(t1);
StructuredBuffer<float> G : register(t2);
RWStructuredBuffer<uint2> Part : register(u0);
groupshared uint2 sh[256];

int inst(uint c, int v) {
    if (ovs == 1) return I[c * instLen + v];
    int j = v >> shift;
    int k = v & (int)(ovs - 1);
    if (j >= (int)instLen - 1) return 0;
    int a = I[c * instLen + j];
    if (k == 0) return a;
    int d = I[c * instLen + j + 1] - a;
    int dq = d >> shift;
    int dr = d - (dq << shift);
    int num = dr * k;
    int qt = dq * k + (num >> shift);
    int r = num & (int)(ovs - 1);
    int hn = (int)(ovs >> 1);
    if (r > hn || (r == hn && ((a + qt) & 1) != 0)) qt += 1;
    return a + qt;
}

[numthreads(256, 1, 1)]
void main(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID) {
    uint i = gid.x * 256 + tid.x;
    float g = G[gid.y];
    uint t = 0;
    if (i < count) {
        for (uint c = 0; c < nch; ++c) {
            float d = (float)O[c * count + i] - (float)inst(c, pos + (int)(stride * i)) * g;
            t += (uint)round(abs(quant != 0 ? round(d) : d));
        }
    }
    sh[tid.x] = uint2(t, 0);
    GroupMemoryBarrierWithGroupSync();
    for (uint s = 128; s > 0; s >>= 1) {
        if (tid.x < s) {
            uint2 a = sh[tid.x], b = sh[tid.x + s];
            uint lo = a.x + b.x;
            sh[tid.x] = uint2(lo, a.y + b.y + (lo < a.x ? 1u : 0u));
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (tid.x == 0) Part[gid.y * groups + gid.x] = sh[0];
}
)";

// 窓掛け。frame f の k 番目に line[f*hop + k] * win[k] を置く (虚部 0)。2 本同時。
const char* kWindowHlsl = R"(
cbuffer P : register(b0) { uint n; uint hop; uint frames; uint rowThreads; };
StructuredBuffer<float> LA : register(t0);
StructuredBuffer<float> LB : register(t1);
StructuredBuffer<float> W : register(t2);
RWStructuredBuffer<float2> A : register(u0);
RWStructuredBuffer<float2> B : register(u1);
[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint tid = id.y * rowThreads + id.x;
    if (tid >= n * frames) return;
    uint f = tid / n, k = tid - f * n;
    float w = W[k];
    A[tid] = float2(LA[f * hop + k] * w, 0);
    B[tid] = float2(LB[f * hop + k] * w, 0);
}
)";

// Stockham 自動整列 radix-2 FFT の 1 段。Ns = 1, 2, 4, ... n/2 の順に呼ぶと自然順で出る。
const char* kFftHlsl = R"(
cbuffer P : register(b0) { uint n; uint ns; uint batch; uint rowThreads; float sgn; float scale; uint twStep; uint pad; };
StructuredBuffer<float2> S : register(t0);
StructuredBuffer<float2> TW : register(t1);
RWStructuredBuffer<float2> D : register(u0);
[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint tid = id.y * rowThreads + id.x;
    uint hn = n >> 1;
    if (tid >= hn * batch) return;
    uint b = tid / hn, j = tid - b * hn, base = b * n;
    float2 v0 = S[base + j];
    float2 v1 = S[base + j + hn];
    uint k = j & (ns - 1);
    float2 w = TW[k * twStep];
    w.y *= sgn;
    float2 v1w = float2(v1.x * w.x - v1.y * w.y, v1.x * w.y + v1.y * w.x);
    uint d = (j / ns) * ns * 2 + k;
    D[base + d] = (v0 + v1w) * scale;
    D[base + d + ns] = (v0 - v1w) * scale;
}
)";

// By Frequency の bin 判定。下半分の bin を処理し、上半分は 0 にする。
const char* kGateFreqHlsl = R"(
cbuffer P : register(b0) { uint n; uint frames; uint rowThreads; uint mode; float gate; uint3 pad; };
RWStructuredBuffer<float2> A : register(u0);
StructuredBuffer<float2> B : register(t0);
StructuredBuffer<float> TH : register(t1);
StructuredBuffer<float> LV : register(t2);
static const float PI = 3.14159265358979;
[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint tid = id.y * rowThreads + id.x;
    uint hn = n >> 1;
    if (tid >= hn * frames) return;
    uint f = tid / hn, k = tid - f * hn, at = f * n + k;
    float2 a = A[at], b = B[at];
    float magA = sqrt(a.x * a.x + a.y * a.y);
    float magB = sqrt(b.x * b.x + b.y * b.y);
    float dp = 0;
    if (mode == 0) {
        float pa = (a.x == 0 && a.y == 0) ? 0 : atan2(a.y, a.x);
        float pb = (b.x == 0 && b.y == 0) ? 0 : atan2(b.y, b.x);
        dp = abs(pb - pa);
        if (dp > PI) dp = 2 * PI - dp;
    }
    a -= b * LV[f];
    if (magB * gate > magA && (mode != 0 || dp < TH[k])) a = float2(0, 0);
    A[at] = a;
    A[at + hn] = float2(0, 0);
}
)";

// Centralization の bin 判定。左右とも書き換え、上半分は 0 にする。
const char* kGateCntrHlsl = R"(
cbuffer P : register(b0) { uint n; uint frames; uint rowThreads; float kvol; float slope; uint3 pad; };
RWStructuredBuffer<float2> A : register(u0);
RWStructuredBuffer<float2> B : register(u1);
StructuredBuffer<float> TH : register(t0);
static const float PI = 3.14159265358979;
[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint tid = id.y * rowThreads + id.x;
    uint hn = n >> 1;
    if (tid >= hn * frames) return;
    uint f = tid / hn, k = tid - f * hn, at = f * n + k;
    float2 a = A[at], b = B[at];
    float magA = sqrt(a.x * a.x + a.y * a.y);
    float magB = sqrt(b.x * b.x + b.y * b.y);
    float pa = (a.x == 0 && a.y == 0) ? 0 : atan2(a.x, a.y);
    float pb = (b.x == 0 && b.y == 0) ? 0 : atan2(b.x, b.y);
    if (magA <= 0 && magB <= 0) { a = 0; b = 0; }
    else if (magB <= 0) { b = 0; }
    else if (magA <= 0) { a = 0; b = 0; }
    else {
        float d = abs(magA - magB);
        if (magA > magB) b *= (d * slope + magA) / magB;
        else a *= (d * slope + magB) / magA;
        float dp = abs(pb - pa);
        if (dp > PI) dp = 2 * PI - dp;
        float th = TH[k];
        if (dp > th) {
            float x = (dp - th) * 2 * kvol;
            float g = x < PI ? cos(x) * 0.5 + 0.5 : 0;
            a *= g;
            b *= g;
        }
    }
    A[at] = a;
    B[at] = b;
    A[at + hn] = float2(0, 0);
    B[at + hn] = float2(0, 0);
}
)";

// overlap-add に足す項。
const char* kTermsHlsl = R"(
cbuffer P : register(b0) { uint n; uint frames; uint rowThreads; float overlap; };
StructuredBuffer<float2> A : register(t0);
StructuredBuffer<float> W : register(t1);
RWStructuredBuffer<float> T : register(u0);
[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint tid = id.y * rowThreads + id.x;
    if (tid >= n * frames) return;
    uint k = tid % n;
    T[tid] = 5.333330154418945 * A[tid].x * W[k] / overlap;
}
)";

const char* kFirHlsl = R"(
cbuffer P : register(b0) { uint count; uint taps; uint rowThreads; uint pad; };
StructuredBuffer<float> X : register(t0);
StructuredBuffer<float> H : register(t1);
RWStructuredBuffer<float> Y : register(u0);
[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint i = id.y * rowThreads + id.x;
    if (i >= count) return;
    float acc = 0;
    uint top = taps - 1 + i;
    for (uint j = 0; j < taps; ++j) acc = mad(X[top - j], H[j], acc);
    Y[i] = acc;
}
)";


template <class T>
struct Ref {
    T* p = nullptr;
    Ref() = default;
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    ~Ref() { reset(); }
    void reset() {
        if (p) p->Release();
        p = nullptr;
    }
    T** out() {
        reset();
        return &p;
    }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

using PFN_CreateDevice = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT,
                                          UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
using PFN_CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
using PFN_Compile = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR,
                                     UINT, UINT, ID3DBlob**, ID3DBlob**);

// GPU 側の buffer。必要な大きさに足りなければ作り直す。
struct Buffer {
    Ref<ID3D11Buffer> buf;
    Ref<ID3D11ShaderResourceView> srv;
    Ref<ID3D11UnorderedAccessView> uav;
    UINT bytes = 0;
};

std::string narrow(const wchar_t* w) {
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(len > 0 ? static_cast<std::size_t>(len - 1) : 0, '\0');
    if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
    return s;
}

class Device {
public:
    std::mutex mutex;
    Status st;

    Device() { open(); }

    // 失敗したら以後使わない。
    void lost(const char* what, HRESULT hr) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "the GPU stopped responding (%s, 0x%08lX)", what, static_cast<unsigned long>(hr));
        st.available = false;
        st.reason = buf;
    }

    bool ok() const { return st.available; }

    bool ensure(Buffer& b, UINT bytes, UINT stride, bool srv, bool uav) {
        if (b.buf && b.bytes >= bytes) return true;
        b.srv.reset();
        b.uav.reset();
        b.buf.reset();
        bytes = std::max<UINT>(bytes, stride) * 5 / 4 / stride * stride + stride;
        D3D11_BUFFER_DESC d{};
        d.ByteWidth = bytes;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = (srv ? D3D11_BIND_SHADER_RESOURCE : 0) | (uav ? D3D11_BIND_UNORDERED_ACCESS : 0);
        d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        d.StructureByteStride = stride;
        HRESULT hr = dev_->CreateBuffer(&d, nullptr, b.buf.out());
        if (FAILED(hr)) { lost("CreateBuffer", hr); return false; }
        if (srv) {
            D3D11_SHADER_RESOURCE_VIEW_DESC v{};
            v.Format = DXGI_FORMAT_UNKNOWN;
            v.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            v.Buffer.NumElements = bytes / stride;
            hr = dev_->CreateShaderResourceView(b.buf.p, &v, b.srv.out());
            if (FAILED(hr)) { lost("CreateShaderResourceView", hr); return false; }
        }
        if (uav) {
            D3D11_UNORDERED_ACCESS_VIEW_DESC v{};
            v.Format = DXGI_FORMAT_UNKNOWN;
            v.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            v.Buffer.NumElements = bytes / stride;
            hr = dev_->CreateUnorderedAccessView(b.buf.p, &v, b.uav.out());
            if (FAILED(hr)) { lost("CreateUnorderedAccessView", hr); return false; }
        }
        b.bytes = bytes;
        return true;
    }

    bool upload(Buffer& b, const void* data, UINT bytes, UINT stride, bool uav = false) {
        if (!ensure(b, bytes, stride, true, uav)) return false;
        if (bytes == 0) return true;
        D3D11_BOX box{0, 0, 0, bytes, 1, 1};
        ctx_->UpdateSubresource(b.buf.p, 0, &box, data, 0, 0);
        return true;
    }

    bool download(Buffer& b, void* dst, UINT bytes) {
        if (!stage_.p || stageBytes_ < bytes) {
            D3D11_BUFFER_DESC d{};
            d.ByteWidth = bytes * 5 / 4 + 16;
            d.Usage = D3D11_USAGE_STAGING;
            d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            HRESULT hr = dev_->CreateBuffer(&d, nullptr, stage_.out());
            if (FAILED(hr)) { lost("CreateBuffer (staging)", hr); return false; }
            stageBytes_ = d.ByteWidth;
        }
        D3D11_BOX box{0, 0, 0, bytes, 1, 1};
        ctx_->CopySubresourceRegion(stage_.p, 0, 0, 0, 0, b.buf.p, 0, &box);
        D3D11_MAPPED_SUBRESOURCE m{};
        HRESULT hr = ctx_->Map(stage_.p, 0, D3D11_MAP_READ, 0, &m);
        if (FAILED(hr)) { lost("Map", hr); return false; }
        std::memcpy(dst, m.pData, bytes);
        ctx_->Unmap(stage_.p, 0);
        return true;
    }

    bool constants(const void* data, UINT bytes) {
        D3D11_MAPPED_SUBRESOURCE m{};
        HRESULT hr = ctx_->Map(cb_.p, 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
        if (FAILED(hr)) { lost("Map (constants)", hr); return false; }
        std::memcpy(m.pData, data, bytes);
        ctx_->Unmap(cb_.p, 0);
        return true;
    }

    // shader を設定して dispatch する。srv / uav は順に t0.., u0.. へ。
    void run(ID3D11ComputeShader* cs, std::initializer_list<Buffer*> srvs, std::initializer_list<Buffer*> uavs,
             UINT gx, UINT gy) {
        ctx_->CSSetShader(cs, nullptr, 0);
        ctx_->CSSetConstantBuffers(0, 1, &cb_.p);
        ID3D11ShaderResourceView* sv[8] = {};
        UINT ns = 0;
        for (Buffer* b : srvs) sv[ns++] = b->srv.p;
        ID3D11UnorderedAccessView* uv[8] = {};
        UINT nu = 0;
        for (Buffer* b : uavs) uv[nu++] = b->uav.p;
        ctx_->CSSetShaderResources(0, ns, sv);
        ctx_->CSSetUnorderedAccessViews(0, nu, uv, nullptr);
        ctx_->Dispatch(gx, gy, 1);
        // 次の dispatch で同じ buffer を読み書きできるよう外す。
        ID3D11ShaderResourceView* nsv[8] = {};
        ID3D11UnorderedAccessView* nuv[8] = {};
        ctx_->CSSetShaderResources(0, 8, nsv);
        ctx_->CSSetUnorderedAccessViews(0, 8, nuv, nullptr);
    }

    // threads 個の thread を (gx * 256, gy) に割り付ける。rowThreads は gx * 256。
    static void grid(unsigned long long threads, UINT& gx, UINT& gy, UINT& rowThreads) {
        const unsigned long long groups = (threads + 255) / 256;
        gx = static_cast<UINT>(std::min<unsigned long long>(groups, 32768));
        gy = static_cast<UINT>((groups + gx - 1) / gx);
        if (gx == 0) gx = 1;
        if (gy == 0) gy = 1;
        rowThreads = gx * 256;
    }

    ID3D11Device* dev() const { return dev_.p; }
    ID3D11DeviceContext* ctx() const { return ctx_.p; }

    Ref<ID3D11ComputeShader> search, level, window, fft, gateFreq, gateCntr, terms, fir;

    Buffer blockO, blockI, cand, part, gains;
    Buffer lineA, lineB, win, thr, lv, specA, specB, tmp, tw, termA, termB;
    Buffer firX, firH, firY;
    int blockCount = 0, blockInstLen = 0, blockCh = 0;
    int twN = 0;

private:
    void open() {
        st.available = false;
        // GPU の無い PC と同じ状態を試すための切り替え。
        if (const char* off = std::getenv("UTAGOE_GPU_DISABLE"); off && *off && *off != '0') {
            st.reason = "GPU acceleration was turned off with UTAGOE_GPU_DISABLE";
            return;
        }
        // System32 以外から同名 DLL を読まないようにする。
        const DWORD flags = LOAD_LIBRARY_SEARCH_SYSTEM32;
        HMODULE d3d = LoadLibraryExW(L"d3d11.dll", nullptr, flags);
        HMODULE dxgi = LoadLibraryExW(L"dxgi.dll", nullptr, flags);
        HMODULE comp = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, flags);
        if (!d3d || !dxgi) { st.reason = "Direct3D 11 is not installed"; return; }
        if (!comp) { st.reason = "the shader compiler (d3dcompiler_47.dll) is missing"; return; }
        auto createDevice = reinterpret_cast<PFN_CreateDevice>(reinterpret_cast<void*>(GetProcAddress(d3d, "D3D11CreateDevice")));
        auto createFactory = reinterpret_cast<PFN_CreateFactory>(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1")));
        compile_ = reinterpret_cast<PFN_Compile>(reinterpret_cast<void*>(GetProcAddress(comp, "D3DCompile")));
        if (!createDevice || !createFactory || !compile_) { st.reason = "Direct3D 11 is incomplete on this system"; return; }

        // ハードウェアの adapter のうち、専用 memory が最も多いものを使う。
        Ref<IDXGIFactory1> factory;
        if (FAILED(createFactory(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.out())))) {
            st.reason = "DXGI could not be initialised";
            return;
        }
        Ref<IDXGIAdapter1> best;
        SIZE_T bestMem = 0;
        DXGI_ADAPTER_DESC1 bestDesc{};
        for (UINT i = 0;; ++i) {
            Ref<IDXGIAdapter1> a;
            if (factory->EnumAdapters1(i, a.out()) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 d{};
            if (FAILED(a->GetDesc1(&d))) continue;
            if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            if (d.VendorId == 0x1414) continue;
            const SIZE_T mem = d.DedicatedVideoMemory + d.SharedSystemMemory / 4;
            if (!best || mem > bestMem) {
                best.reset();
                best.p = a.p;
                a.p = nullptr;
                bestMem = mem;
                bestDesc = d;
            }
        }
        if (!best) { st.reason = "no GPU with Direct3D 11 support was found"; return; }

        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL got{};
        HRESULT hr = createDevice(best.p, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_SINGLETHREADED,
                                  levels, 2, D3D11_SDK_VERSION, dev_.out(), &got, ctx_.out());
        if (FAILED(hr)) hr = createDevice(best.p, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_SINGLETHREADED,
                                          levels + 1, 1, D3D11_SDK_VERSION, dev_.out(), &got, ctx_.out());
        if (FAILED(hr)) { st.reason = "the GPU does not support Direct3D 11 compute shaders"; return; }

        D3D11_BUFFER_DESC cd{};
        cd.ByteWidth = 64;
        cd.Usage = D3D11_USAGE_DYNAMIC;
        cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev_->CreateBuffer(&cd, nullptr, cb_.out()))) { st.reason = "the GPU could not allocate memory"; return; }

        if (!build(kSearchHlsl, search) || !build(kLevelHlsl, level) || !build(kWindowHlsl, window) ||
            !build(kFftHlsl, fft) || !build(kGateFreqHlsl, gateFreq) || !build(kGateCntrHlsl, gateCntr) ||
            !build(kTermsHlsl, terms) || !build(kFirHlsl, fir))
            return;

        st.adapter = narrow(bestDesc.Description);
        st.available = true;
    }

    bool build(const char* src, Ref<ID3D11ComputeShader>& cs) {
        Ref<ID3DBlob> code, err;
        HRESULT hr = compile_(src, std::strlen(src), "utagoe", nullptr, nullptr, "main", "cs_5_0",
                              D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.out(), err.out());
        if (FAILED(hr)) {
            st.reason = "a compute shader failed to compile";
            if (err) st.reason += std::string(": ") + static_cast<const char*>(err->GetBufferPointer());
            return false;
        }
        hr = dev_->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, cs.out());
        if (FAILED(hr)) { st.reason = "the GPU rejected a compute shader"; return false; }
        return true;
    }

    PFN_Compile compile_ = nullptr;
    Ref<ID3D11Device> dev_;
    Ref<ID3D11DeviceContext> ctx_;
    Ref<ID3D11Buffer> cb_;
    Ref<ID3D11Buffer> stage_;
    UINT stageBytes_ = 0;
};

// device は最初に使うときに作る。DLL の unload 時に解放すると driver によっては止まるため、破棄しない。
Device& device() {
    static Device* d = new Device;
    return *d;
}

unsigned log2u(unsigned v) {
    unsigned s = 0;
    while ((1u << s) < v) ++s;
    return s;
}

// 複素 FFT を batch 本まとめて行う。結果は a か tmp のどちらかに入り、入った方を返す。
Buffer* fftRun(Device& d, Buffer& a, Buffer& tmp, int n, int batch, bool inverse) {
    // twiddle は exp(-2 pi i k / n), k < n/2。倍精度で作って float に丸める。
    if (d.twN != n) {
        std::vector<float> t(static_cast<std::size_t>(n));
        for (int k = 0; k < n / 2; ++k) {
            const double ang = -2.0 * 3.14159265358979323846 * k / n;
            t[static_cast<std::size_t>(2 * k)] = static_cast<float>(std::cos(ang));
            t[static_cast<std::size_t>(2 * k + 1)] = static_cast<float>(std::sin(ang));
        }
        if (!d.upload(d.tw, t.data(), static_cast<UINT>(t.size() * sizeof(float)), 8)) return nullptr;
        d.twN = n;
    }
    if (!d.ensure(tmp, a.bytes, 8, true, true)) return nullptr;
    struct { UINT n, ns, batch, rowThreads; float sgn, scale; UINT twStep, pad; } p{};
    UINT gx, gy, row;
    Device::grid(static_cast<unsigned long long>(n / 2) * batch, gx, gy, row);
    Buffer* src = &a;
    Buffer* dst = &tmp;
    for (int ns = 1; ns < n; ns *= 2) {
        p = {static_cast<UINT>(n), static_cast<UINT>(ns), static_cast<UINT>(batch), row,
             inverse ? -1.0f : 1.0f, (inverse && ns * 2 == n) ? 1.0f / static_cast<float>(n) : 1.0f,
             static_cast<UINT>(n / (2 * ns)), 0};
        if (!d.constants(&p, sizeof p)) return nullptr;
        d.run(d.fft.p, {src, &d.tw}, {dst}, gx, gy);
        std::swap(src, dst);
    }
    return src;
}

}

Status status() {
    Device& d = device();
    std::lock_guard<std::mutex> lk(d.mutex);
    return d.st;
}

bool setBlock(const int32_t* const* orig, int channels, int count, const int32_t* const* inst, int instLen) {
    Device& d = device();
    std::lock_guard<std::mutex> lk(d.mutex);
    if (!d.ok()) return false;
    std::vector<int32_t> o(static_cast<std::size_t>(count) * channels), in(static_cast<std::size_t>(instLen) * channels);
    for (int c = 0; c < channels; ++c) {
        std::copy(orig[c], orig[c] + count, o.begin() + static_cast<std::ptrdiff_t>(c) * count);
        std::copy(inst[c], inst[c] + instLen, in.begin() + static_cast<std::ptrdiff_t>(c) * instLen);
    }
    if (!d.upload(d.blockO, o.data(), static_cast<UINT>(o.size() * 4), 4)) return false;
    if (!d.upload(d.blockI, in.data(), static_cast<UINT>(in.size() * 4), 4)) return false;
    d.blockCount = count;
    d.blockInstLen = instLen;
    d.blockCh = channels;
    return true;
}

bool searchScores(int ovs, int stride, const int32_t* candidates, int count, std::vector<uint64_t>& scores) {
    Device& d = device();
    std::lock_guard<std::mutex> lk(d.mutex);
    if (!d.ok() || count <= 0 || count > 65535 || (ovs & (ovs - 1)) != 0) return false;
    const UINT groups = static_cast<UINT>((d.blockCount + 255) / 256);
    if (groups == 0 || groups > 65535) return false;
    if (!d.upload(d.cand, candidates, static_cast<UINT>(count * 4), 4)) return false;
    if (!d.ensure(d.part, groups * count * 8, 8, false, true)) return false;
    struct { UINT count, nch, stride, ovs, shift, groups, instLen, pad; } p{
        static_cast<UINT>(d.blockCount), static_cast<UINT>(d.blockCh), static_cast<UINT>(stride),
        static_cast<UINT>(ovs), log2u(static_cast<unsigned>(ovs)), groups, static_cast<UINT>(d.blockInstLen), 0};
    if (!d.constants(&p, sizeof p)) return false;
    d.run(d.search.p, {&d.blockO, &d.blockI, &d.cand}, {&d.part}, groups, static_cast<UINT>(count));
    std::vector<uint32_t> raw(static_cast<std::size_t>(groups) * count * 2);
    if (!d.download(d.part, raw.data(), static_cast<UINT>(raw.size() * 4))) return false;
    scores.assign(static_cast<std::size_t>(count), 0);
    for (int k = 0; k < count; ++k) {
        uint64_t s = 0;
        for (UINT g = 0; g < groups; ++g) {
            const std::size_t at = (static_cast<std::size_t>(k) * groups + g) * 2;
            s += static_cast<uint64_t>(raw[at]) | (static_cast<uint64_t>(raw[at + 1]) << 32);
        }
        scores[static_cast<std::size_t>(k)] = s;
    }
    return true;
}

bool levelScores(int ovs, int stride, int pos, float scale, bool quantize,
                 const float* gainsIn, int count, std::vector<double>& scores) {
    Device& d = device();
    std::lock_guard<std::mutex> lk(d.mutex);
    if (!d.ok() || count <= 0 || count > 65535 || (ovs & (ovs - 1)) != 0) return false;
    const UINT groups = static_cast<UINT>((d.blockCount + 255) / 256);
    if (groups == 0 || groups > 65535) return false;
    if (!d.upload(d.gains, gainsIn, static_cast<UINT>(count * 4), 4)) return false;
    if (!d.ensure(d.part, groups * count * 8, 8, false, true)) return false;
    struct { UINT count, nch, stride, ovs, shift, groups, instLen; INT pos; UINT quant; UINT pad[3]; } p{
        static_cast<UINT>(d.blockCount), static_cast<UINT>(d.blockCh), static_cast<UINT>(stride), static_cast<UINT>(ovs),
        log2u(static_cast<unsigned>(ovs)), groups, static_cast<UINT>(d.blockInstLen), pos, quantize ? 1u : 0u, {0, 0, 0}};
    if (!d.constants(&p, sizeof p)) return false;
    d.run(d.level.p, {&d.blockO, &d.blockI, &d.gains}, {&d.part}, groups, static_cast<UINT>(count));
    std::vector<uint32_t> raw(static_cast<std::size_t>(groups) * count * 2);
    if (!d.download(d.part, raw.data(), static_cast<UINT>(raw.size() * 4))) return false;
    scores.assign(static_cast<std::size_t>(count), 0.0);
    for (int k = 0; k < count; ++k) {
        uint64_t s = 0;
        for (UINT g = 0; g < groups; ++g) {
            const std::size_t at = (static_cast<std::size_t>(k) * groups + g) * 2;
            s += static_cast<uint64_t>(raw[at]) | (static_cast<uint64_t>(raw[at + 1]) << 32);
        }
        scores[static_cast<std::size_t>(k)] = static_cast<double>(s) / scale;
    }
    return true;
}

namespace {

// 2 本の line から frame を切り出して窓を掛け、両方を forward FFT する。結果の場所を fa / fb に返す。
bool windowAndForward(Device& d, const float* lineA, const float* lineB, int frames, int n, int hop,
                      const float* window, Buffer*& fa, Buffer*& fb) {
    const UINT lineLen = static_cast<UINT>((frames - 1) * hop + n);
    if (!d.upload(d.lineA, lineA, lineLen * 4, 4)) return false;
    if (!d.upload(d.lineB, lineB, lineLen * 4, 4)) return false;
    if (!d.upload(d.win, window, static_cast<UINT>(n) * 4, 4)) return false;
    const UINT specBytes = static_cast<UINT>(frames) * static_cast<UINT>(n) * 8;
    if (!d.ensure(d.specA, specBytes, 8, true, true) || !d.ensure(d.specB, specBytes, 8, true, true)) return false;
    UINT gx, gy, row;
    Device::grid(static_cast<unsigned long long>(n) * frames, gx, gy, row);
    struct { UINT n, hop, frames, rowThreads; } p{static_cast<UINT>(n), static_cast<UINT>(hop), static_cast<UINT>(frames), row};
    if (!d.constants(&p, sizeof p)) return false;
    d.run(d.window.p, {&d.lineA, &d.lineB, &d.win}, {&d.specA, &d.specB}, gx, gy);
    fa = fftRun(d, d.specA, d.tmp, n, frames, false);
    if (!fa) return false;
    // tmp は specA の結果で使われている可能性があるので、B は termA を作業領域に借りる。
    if (!d.ensure(d.termA, specBytes, 8, true, true)) return false;
    fb = fftRun(d, d.specB, fa == &d.tmp ? d.termA : d.tmp, n, frames, false);
    return fb != nullptr;
}

bool termsOut(Device& d, Buffer& spec, int frames, int n, int overlap, float* out) {
    const UINT count = static_cast<UINT>(frames) * static_cast<UINT>(n);
    if (!d.ensure(d.termB, count * 4, 4, true, true)) return false;
    UINT gx, gy, row;
    Device::grid(count, gx, gy, row);
    struct { UINT n, frames, rowThreads; float overlap; } p{static_cast<UINT>(n), static_cast<UINT>(frames), row,
                                                            static_cast<float>(overlap)};
    if (!d.constants(&p, sizeof p)) return false;
    d.run(d.terms.p, {&spec, &d.win}, {&d.termB}, gx, gy);
    return d.download(d.termB, out, count * 4);
}

}

bool freqFrames(const float* lineA, const float* lineB, int frames, int n, int hop, int overlap,
                int mode, float gate, const float* window, const float* threshold,
                const double* levels, float* terms) {
    Device& d = device();
    std::lock_guard<std::mutex> lk(d.mutex);
    if (!d.ok() || frames <= 0) return false;
    Buffer *fa = nullptr, *fb = nullptr;
    if (!windowAndForward(d, lineA, lineB, frames, n, hop, window, fa, fb)) return false;

    std::vector<float> lv(levels, levels + frames);
    if (!d.upload(d.lv, lv.data(), static_cast<UINT>(frames) * 4, 4)) return false;
    if (!d.upload(d.thr, threshold, static_cast<UINT>(n / 2) * 4, 4)) return false;
    UINT gx, gy, row;
    Device::grid(static_cast<unsigned long long>(n / 2) * frames, gx, gy, row);
    struct { UINT n, frames, rowThreads, mode; float gate; UINT pad[3]; } p{
        static_cast<UINT>(n), static_cast<UINT>(frames), row, static_cast<UINT>(mode), gate, {0, 0, 0}};
    if (!d.constants(&p, sizeof p)) return false;
    d.run(d.gateFreq.p, {fb, &d.thr, &d.lv}, {fa}, gx, gy);

    Buffer& other = (fa == &d.specA) ? (fb == &d.tmp ? d.termA : d.tmp) : d.specA;
    Buffer* res = fftRun(d, *fa, other, n, frames, true);
    if (!res) return false;
    return termsOut(d, *res, frames, n, overlap, terms);
}

bool centralizeFrames(const float* lineL, const float* lineR, int frames, int n, int hop, int overlap,
                      float kvol, float slope, const float* window, const float* threshold,
                      float* termsL, float* termsR) {
    Device& d = device();
    std::lock_guard<std::mutex> lk(d.mutex);
    if (!d.ok() || frames <= 0) return false;
    Buffer *fa = nullptr, *fb = nullptr;
    if (!windowAndForward(d, lineL, lineR, frames, n, hop, window, fa, fb)) return false;

    if (!d.upload(d.thr, threshold, static_cast<UINT>(n / 2) * 4, 4)) return false;
    UINT gx, gy, row;
    Device::grid(static_cast<unsigned long long>(n / 2) * frames, gx, gy, row);
    struct { UINT n, frames, rowThreads; float kvol, slope; UINT pad[3]; } p{
        static_cast<UINT>(n), static_cast<UINT>(frames), row, kvol, slope, {0, 0, 0}};
    if (!d.constants(&p, sizeof p)) return false;
    d.run(d.gateCntr.p, {&d.thr}, {fa, fb}, gx, gy);

    // 4 つの buffer (specA, specB, tmp, termA) のうち空いているものを作業領域に使う。
    Buffer* pool[4] = {&d.specA, &d.specB, &d.tmp, &d.termA};
    auto spare = [&](Buffer* x, Buffer* y) {
        for (Buffer* b : pool)
            if (b != x && b != y) return b;
        return pool[0];
    };
    Buffer* ra = fftRun(d, *fa, *spare(fa, fb), n, frames, true);
    if (!ra) return false;
    if (!termsOut(d, *ra, frames, n, overlap, termsL)) return false;
    Buffer* rb = fftRun(d, *fb, *spare(fb, ra), n, frames, true);
    if (!rb) return false;
    return termsOut(d, *rb, frames, n, overlap, termsR);
}

bool firConvolve(const float* x, int count, const float* h, int taps, float* out) {
    Device& d = device();
    std::lock_guard<std::mutex> lk(d.mutex);
    if (!d.ok() || count <= 0 || taps <= 0) return false;
    if (!d.upload(d.firX, x, static_cast<UINT>(count + taps - 1) * 4, 4)) return false;
    if (!d.upload(d.firH, h, static_cast<UINT>(taps) * 4, 4)) return false;
    if (!d.ensure(d.firY, static_cast<UINT>(count) * 4, 4, true, true)) return false;
    UINT gx, gy, row;
    Device::grid(static_cast<unsigned long long>(count), gx, gy, row);
    struct { UINT count, taps, rowThreads, pad; } p{static_cast<UINT>(count), static_cast<UINT>(taps), row, 0};
    if (!d.constants(&p, sizeof p)) return false;
    d.run(d.fir.p, {&d.firX, &d.firH}, {&d.firY}, gx, gy);
    return d.download(d.firY, out, static_cast<UINT>(count) * 4);
}

}
}

#else

namespace utagoe {
namespace gpu {

Status status() {
    Status s;
    s.reason = "GPU acceleration is only available on Windows";
    return s;
}
bool setBlock(const int32_t* const*, int, int, const int32_t* const*, int) { return false; }
bool searchScores(int, int, const int32_t*, int, std::vector<uint64_t>&) { return false; }
bool levelScores(int, int, int, float, bool, const float*, int, std::vector<double>&) { return false; }
bool freqFrames(const float*, const float*, int, int, int, int, int, float, const float*, const float*,
                const double*, float*) { return false; }
bool centralizeFrames(const float*, const float*, int, int, int, int, float, float, const float*, const float*,
                      float*, float*) { return false; }
bool firConvolve(const float*, int, const float*, int, float*) { return false; }

}
}

#endif
