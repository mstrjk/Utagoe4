// 元実装の解析と本処理 (0x409c4c, 0x40fbb4..0x4122b8) を写した。
// 元実装との違い:
// - 解析結果の cache (ファイル名 / ProcMode / Block Size が同じなら前回値を再利用) は持たない。常に初回実行と同じ動作になる。
// - stream の範囲外や読み残しは 0 として扱う (元実装は header や前回 block の残りを読む)。
// - FreqEngine は channel ごとの thread ではなく順番に処理する。結果は同じ。

#include "engine.h"
#include "log.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <vector>

namespace utagoe {
namespace v3 {
namespace {

constexpr double kBig = 1.7e308;   // 元実装の対応箇所: 0x4cd324。

int roundInt(long double v) { return static_cast<int>(rint87(v)); }

// 進捗の区間を一時的に差し替える。
struct Stage {
    Context& c;
    float from, to;
    Stage(Context& ctx, float a, float b) : c(ctx), from(ctx.progressFrom), to(ctx.progressTo) {
        const float span = to - from;
        c.progressFrom = from + span * a;
        c.progressTo = from + span * b;
    }
    ~Stage() {
        c.progressFrom = from;
        c.progressTo = to;
    }
};

double streamFraction(const Source& s) {
    return s.frames > 0 ? static_cast<double>(s.pos) / static_cast<double>(s.frames) : 1.0;
}

}

// 元実装の対応箇所: 0x409c4c
Params toParams(const Settings& s, int channels) {
    Params p;
    p.procMode = static_cast<int>(s.procMode);
    p.mergeMode = static_cast<int>(s.mergeMode);
    p.oversample = s.oversample;
    p.ovsMul = std::max(1, s.oversampleMul);
    p.blockMs = std::max(50, s.blockSizeMs);
    const int adptNum = std::max(0, s.adptRange);
    p.adptMode = static_cast<int>(s.adptMode);
    p.driftRange = roundInt(static_cast<long double>(p.blockMs) * 0.01L * adptNum);
    p.blockSec = static_cast<double>(static_cast<long double>(p.blockMs) * 0.001L);
    p.centralize = s.centralize;
    p.cntr = s.centralizeValue();
    p.introMode = static_cast<int>(s.introMode);
    p.levelAdpt = static_cast<int>(s.levelAdpt);
    p.lowPass = s.lowPass;
    p.highPass = s.highPass;
    p.lowPassHz = static_cast<int>(s.lowPassHz());
    p.highPassHz = s.highPassHz();
    p.kvol = s.extractLevelValue();
    p.klvl = s.instLevelValue();
    p.krkPhase = static_cast<int>(s.krkPhase);
    p.soundQty = static_cast<int>(s.soundQty);

    // 元実装 0x40fbb4。mono では L/R Difference と Centralization が意味を持たない。
    if (channels < 2) {
        p.procMode = 0;
        p.centralize = false;
    }
    return p;
}


// 元実装 0x410570。ofs を初期オフセットとして本処理と同じ block 探索を流し、drift と残差を集計する。
// secs が 0 でなければ先頭 secs 秒だけ見る。final なら 30 block ごとに level も推定する。
int scoreOffsets(Context& c, int ofs, int base, bool final, bool phase, int secs, int range) {
    const Params& p = c.p;
    Worker w;
    w.init(c.channels, c.rate, p.blockSec * 2.0f, 1, c.quantize);
    w.setGpu(c);
    w.setPhase(phase);

    const int fitEvery = final ? 30 : INT_MAX;
    const bool limited = secs != 0;
    const int maxBlk = limited ? roundInt(static_cast<long double>(secs) / p.blockSec) : 0;

    if (ofs < 0) {
        c.orig.seek(range - ofs);
        c.inst.seek(range);
    } else {
        c.orig.seek(range);
        c.inst.seek(ofs + range);
    }

    const int n = w.n(), q = n / 4, e = n / 4 + n / 2;
    long long tO = c.orig.tell(), tI = c.inst.tell();

    struct Rec { int pos; double voc, org; };
    std::vector<Rec> recs;
    int cnt = 0, fits = 0;
    double sumG = 0.0;

    for (;;) {
        tI -= range;
        c.inst.seek(tI);
        const int ro = c.orig.read(w.rawOrig(), n);
        const int ri = c.inst.read(w.rawInst(), w.m());
        if (ro == 0 || ri == 0) break;
        if (limited && static_cast<int>(recs.size()) >= maxBlk) break;

        w.prepare();
        int pos = 0;
        switch (p.procMode) {
        case 0:
            pos = w.searchCh(range, range, 1);
            if (++cnt >= fitEvery) {
                double g = w.levelFitCh(pos, 0.02, 0.2, 1.0);
                g = w.levelFitCh(pos, 0.002, 0.03, g);
                sumG += g;
                ++fits;
                cnt = 0;
            }
            w.sub(pos, 1.0);
            break;
        case 1:
        case 2:
            if (p.procMode == 1) w.toLRDiff(); else w.toMono();
            pos = w.searchMix(range, range, 1, n);
            if (++cnt >= fitEvery) {
                double g = w.levelFitMix(pos, 0.02, 0.2, 1.0);
                g = w.levelFitMix(pos, 0.002, 0.03, g);
                sumG += g;
                ++fits;
                cnt = 0;
            }
            if (p.procMode == 1) w.sub(pos, 1.0); else w.subMono(pos, 1.0);
            break;
        default:
            break;
        }

        recs.push_back({pos - range, w.sumAbsOut(q, e), w.sumAbsOrig(q, e)});

        tI += pos + base + n / 2;
        tO += n / 2;
        c.orig.seek(tO);
        const double f = limited && maxBlk > 0 ? static_cast<double>(recs.size()) / maxBlk : streamFraction(c.orig);
        log::status(2, false, "  scoring offset %d, range %d: %s | %zu blocks", ofs, range, log::progressBar(f, 16).c_str(), recs.size());
        if (!c.poll(f)) break;
    }

    // 平均オフセット、平均残差、平均原曲量、オフセットの平均絶対偏差。
    const double k = static_cast<double>(recs.size());
    if (recs.empty()) {
        c.ofs = c.voc = c.org = c.bnsn = 0.0;
    } else {
        long long sumPos = 0;
        double sumVoc = 0.0, sumOrg = 0.0;
        for (const Rec& r : recs) {
            sumPos += r.pos;
            sumVoc += r.voc;
            sumOrg += r.org;
        }
        c.ofs = static_cast<double>(sumPos) / k;
        c.voc = sumVoc / k;
        c.org = sumOrg / k;
        double dev = 0.0;
        for (const Rec& r : recs) dev += std::fabs(r.pos - c.ofs);
        c.bnsn = dev / k;
    }
    c.vol = fits > 0 ? sumG / fits : 1.0;
    log::clearStatus(2);
    // 元実装の debug 表示 "range:%d vol:%f voc:%f org:%f ofs:%f bnsn:%f" と同じ値。
    log::detail("  score  offset %6d range %3d%s: drift %+.3f/blk spread %.3f residual %.4f (voc %.0f / org %.0f) level %.4f, %zu blocks",
                ofs, range, secs ? (" (" + std::to_string(secs) + " s)").c_str() : " (whole song)", c.ofs, c.bnsn,
                c.org != 0.0 ? c.voc / c.org : 0.0, c.voc, c.org, c.vol, recs.size());
    return 0;
}

// 元実装 0x411a7c。無音でない最初の sample (L+R >= 128) を両方で探し、そこから 1 秒 block で照合する。
int simpleOffset(Context& c, bool phase) {
    Worker w;
    w.init(c.channels, c.rate, 1.0, 1, c.quantize);
    w.setGpu(c);
    w.setPhase(phase);
    c.orig.seek(0);
    c.inst.seek(0);

    const int n = w.n(), m = w.m();

    // 元実装は符号付きで比較するので、負側に振れた sample は無音として数える。
    int leadO = 0;
    for (bool found = false; !found && c.orig.read(w.rawOrig(), n) != 0;) {
        w.prepare();
        w.toMono();
        for (int i = 0; i < n; ++i) {
            if (w.mixOrig()[static_cast<std::size_t>(i)] >= 128) { found = true; break; }
            ++leadO;
        }
    }
    int leadI = 0;
    for (bool found = false; !found && c.inst.read(w.rawInst(), m) != 0;) {
        w.prepare();
        w.toMono();
        for (int i = 0; i < m; ++i) {
            if (w.mixInst()[static_cast<std::size_t>(i)] >= 128) { found = true; break; }
            ++leadI;
        }
    }
    if (!c.poll(0.5)) return 0;

    int s = leadI - n / 4;
    if (s < 0) s = 0;
    c.orig.seek(leadO);
    c.inst.seek(s);
    c.orig.read(w.rawOrig(), n);
    c.inst.read(w.rawInst(), m);
    w.prepare();
    w.toMono();
    const int pos = w.searchMix(n / 4, n / 4, 1, n / 5);
    return pos + s - leadO;
}

// 元実装 0x411cd4。20 ms ごとの振幅 envelope (原曲 60 秒、インスト 120 秒) で粗く合わせ、0.2 秒 block で詰める。
// mono が false なら envelope は L-R、true なら L+R。
int detailOffset(Context& c, bool mono, bool phase) {
    const int h = roundInt(static_cast<long double>(c.rate) * 0.02);
    const int countO = 3000, countI = 6000;
    if (h <= 0) return 0;

    Worker w;
    w.init(c.channels, c.rate, 0.2, 1, c.quantize);
    w.setGpu(c);
    w.setPhase(phase);
    c.orig.seek(0);
    c.inst.seek(0);

    auto env = [&](const std::vector<double>& mix) {
        double s = 0.0;
        for (int i = 0; i < h; ++i) s += std::fabs(mix[static_cast<std::size_t>(i)]);
        return c.quantize ? std::floor(s / h) : s / h;
    };

    std::vector<double> envO(countO), envI(countI);
    for (int k = 0; k < countO; ++k) {
        c.orig.read(w.rawOrig(), h);
        w.prepare();
        if (mono) w.toMono(); else w.toLRDiff();
        envO[static_cast<std::size_t>(k)] = env(w.mixOrig());
        if ((k & 63) == 0 && !c.poll(k / 9000.0)) return 0;
    }
    for (int k = 0; k < countI; ++k) {
        c.inst.read(w.rawInst(), h);
        w.prepare();
        if (mono) w.toMono(); else w.toLRDiff();
        envI[static_cast<std::size_t>(k)] = env(w.mixInst());
        if ((k & 63) == 0 && !c.poll((countO + k) / 9000.0)) return 0;
    }

    // envelope を mix buffer に置いて 1500 点幅で照合する。buffer は元実装より短い rate でも収まるよう広げる。
    w.reset();
    std::vector<double>& om = w.mixOrig();
    std::vector<double>& im = w.mixInst();
    if (om.size() < static_cast<std::size_t>(countO)) om.resize(countO, 0.0);
    if (im.size() < static_cast<std::size_t>(countO / 2 + countI)) im.resize(countO / 2 + countI, 0.0);
    std::copy(envO.begin(), envO.end(), om.begin());
    std::copy(envI.begin(), envI.end(), im.begin() + countO / 2);
    const int savedN = w.n();
    w.setN(countO);
    const int p = w.searchMixAbs(countO / 2, countO / 2, 1);
    w.setN(savedN);
    const int coarse = (p - countO / 2) * h;

    int lead = 0;
    while (lead < countI && envI[static_cast<std::size_t>(lead)] <= 256) ++lead;
    const int l = lead * h;

    if (coarse >= 0) {
        c.orig.seek(l);
        c.inst.seek(l + coarse - 2 * h);
    } else {
        c.orig.seek(l - coarse);
        c.inst.seek(l - 2 * h);
    }
    w.init(c.channels, c.rate, 0.2, 1, c.quantize);
    w.setGpu(c);
    w.setPhase(phase);
    c.orig.read(w.rawOrig(), w.n());
    c.inst.read(w.rawInst(), w.m());
    w.prepare();
    w.toMono();
    const int pos = w.searchMix(2 * h, 2 * h, 1, w.n());
    return pos + coarse - 2 * h;
}

// 元実装 0x4102c4。正相 / 逆相それぞれで 3 通りの初期オフセットを試し、残差比 voc/org が 0.8 未満なら即採用、
// そうでなければ最小のものを採る。極性も c.phase に決まる。
int decideInitialOffset(Context& c) {
    double best = kBig;
    int result = 0;
    c.phase = false;
    int slot = 0;
    for (int ph = 0; ph < 2; ++ph) {
        const bool phase = ph != 0;
        for (int method = 0; method < 3; ++method, ++slot) {
            int ofs;
            {
                Stage st(c, slot / 6.0f, (slot + 0.5f) / 6.0f);
                ofs = method == 0 ? simpleOffset(c, phase) : detailOffset(c, method == 2, phase);
            }
            if (c.cancelled) return result;
            {
                Stage st(c, (slot + 0.5f) / 6.0f, (slot + 1.0f) / 6.0f);
                scoreOffsets(c, ofs, 0, false, phase, 30, 20);
            }
            if (c.cancelled) return result;
            static const char* names[] = {"simple", "detailed L-R", "detailed mono"};
            log::detail("  candidate %-13s phase %s -> offset %d, residual %.4f", names[method], phase ? "inverted" : "normal",
                        ofs, c.org != 0.0 ? c.voc / c.org : 0.0);
            if (c.org != 0.0) {
                const double ratio = c.voc / c.org;
                if (ratio < 0.8) {
                    c.phase = phase;
                    return ofs;
                }
                if (best > ratio) {
                    best = ratio;
                    result = ofs;
                    c.phase = phase;
                }
            }
        }
    }
    return result;
}

// 元実装 0x40fdc4。初期オフセット、極性、block ごとの drift 予測と探索幅、level を決める。
// 戻り値 0 は解析済み、3 は自動の項目がないため解析しなかったことを表す。
int introAnalysis(Context& c) {
    const Params& p = c.p;
    if (p.introMode != 0 && p.adptMode != 0 && p.krkPhase != 0 && p.levelAdpt != 0) return 3;

    int ofs0;
    {
        Stage st(c, 0.0f, 0.35f);
        ofs0 = decideInitialOffset(c);
    }
    if (c.cancelled) return 0;

    {
        Stage st(c, 0.35f, 0.45f);
        scoreOffsets(c, ofs0, 0, false, c.phase, 120, 20);
    }
    if (c.cancelled) return 0;

    const bool found = c.voc < c.org;
    c.baseOfs = roundInt(c.ofs);
    const int r0 = roundInt(std::fabs(c.ofs - c.baseOfs) + c.bnsn + 0.5f);

    int r;
    if (found) {
        double best = kBig, prev = 0.0;
        bool seen = false;
        int bestR = 0;
        for (r = r0; r <= r0 + 8; ++r) {
            {
                Stage st(c, 0.45f + (r - r0) * 0.03f, 0.48f + (r - r0) * 0.03f);
                scoreOffsets(c, ofs0, c.baseOfs, false, c.phase, 120, r);
            }
            if (c.cancelled) return 0;
            if (c.voc < c.org) {
                // 残差がほとんど変わらなくなったら、その 1 つ手前の幅で十分とみなす。
                if (seen) {
                    if (prev == 0.0) { --r; break; }
                    const double ratio = c.voc / prev;
                    if (ratio > 0.997 && ratio < 1.003) { --r; break; }
                }
                seen = true;
                prev = c.voc;
            }
            if (c.voc < best) {
                best = c.voc;
                bestR = r;
            }
        }
        if (r > r0 + 8) r = bestR;
    } else {
        r = r0 + 1;
    }

    {
        Stage st(c, 0.72f, 1.0f);
        if (c.hasAnalysisInst) std::swap(c.inst, c.analysisInst);
        scoreOffsets(c, ofs0, c.baseOfs, true, c.phase, 0, r);
        if (c.hasAnalysisInst) std::swap(c.inst, c.analysisInst);
    }
    if (c.cancelled) return 0;

    c.range = r;
    log::detail("  selected offset %d, phase %s, drift %d/blk, range %d, level %.4f",
                ofs0, c.phase ? "inverted" : "normal", c.baseOfs, r, c.vol);
    c.offset = ofs0;
    c.analysed = true;
    return 0;
}


namespace {

// block の範囲 [start, start + count) を書き出し側へ渡す。By Frequency はエンジン入力、それ以外は減算結果。
// collect のときは減算せず、原曲とその位置に合わせたインストを記録する (代替モデルへ渡す)。
// インストは stride 間隔 (oversampling なら 1/ovs sample 単位の位置) で読む。hasRef が false の範囲はインストが無い。
void emit(Context& c, const Params& p, Worker& w, Pipeline& pipe, int start, int count, int pos, double g,
          int stride, bool hasRef) {
    if (c.collect) {
        const int ch = c.channels;
        for (int i = 0; i < count; ++i) {
            for (int k = 0; k < ch; ++k) {
                c.colMix.push_back(static_cast<float>(w.orig(k)[start + i]));
                const double r = stride == 1 ? w.inst(k)[pos + start + i]
                                             : w.instOvs(k)[pos + static_cast<std::ptrdiff_t>(stride) * (start + i)];
                c.colRef.push_back(hasRef ? static_cast<float>(r) : 0.0f);
            }
            c.colValid.push_back(hasRef ? 1 : 0);
        }
        return;
    }
    if (p.mergeMode == 1) {
        pipe.pushOutput(w.out(0) + start, w.out(1) + start, count);
        return;
    }
    if (p.procMode == 2) w.monoAverage(start, count, pos);
    const double* o[2] = {w.orig(0) + start, w.orig(1) + start};
    const double* i[2] = {w.inst(0) + pos + start, w.inst(1) + pos + start};
    pipe.pushEngine(o, i, count, g);
}

// 元実装 0x41211c。インストとの照合をせず、位置 0 / level 0.95 で count frame を処理して書き出す。
// 冒頭の探索幅ぶんと、最初の block の前 1/4 を埋めるのに使う。withInst が false ならインストは 0。
void passThrough(Context& c, Worker& w, Pipeline& pipe, int count, bool withInst) {
    const Params& p = c.p;
    const double g = 0.95;
    w.reset();
    const int n = w.n();
    for (int rem = count; rem > 0; rem -= n) {
        const int k = rem > n ? n : rem;
        c.orig.read(w.rawOrig(), k);
        if (withInst) c.inst.read(w.rawInst(), k);
        w.prepare();
        if (p.mergeMode == 1) {
            if (p.procMode == 2) w.subMono(0, g); else w.sub(0, g);
        }
        emit(c, p, w, pipe, 0, k, 0, g, 1, withInst);
    }
}

}

// 元実装の対応箇所: 0x410cfc
int processMain(Context& c) {
    const Params& p = c.p;

    if (c.hasAnalysisInst) std::swap(c.inst, c.analysisInst);
    int ia;
    bool ok = false;
    {
        Stage st(c, 0.0f, 0.4f);
        log::Stage ls("analysis (offset, phase, drift, level)");
        ia = introAnalysis(c);
        if (ia == 3) log::detail("  skipped: offset, drift, phase and level are all set manually");
    }
    if (ia == 0) ok = true;
    else if (ia == 3) ia = 0;
    if (c.cancelled || ia != 0) {
        if (c.hasAnalysisInst) std::swap(c.inst, c.analysisInst);
        return ia;
    }

    // 処理の種類。oversampling は By Waveform のときだけ有効。
    int kind = p.procMode, ovs = 1;
    if (p.oversample && p.mergeMode == 1) {
        kind = p.procMode + 3;
        ovs = p.ovsMul;
    }

    // level。By Frequency は解析値 (失敗時 0.95)、By Waveform は Level Adaptation の設定に従う。
    bool adaptive = false;
    double g0 = 1.0;
    if (p.mergeMode == 0) {
        g0 = ok ? c.vol : 0.95;
    } else {
        switch (p.levelAdpt) {
        case 0: g0 = c.vol; break;
        case 1: adaptive = true; g0 = 1.0; break;
        case 2: g0 = p.klvl; break;
        default: g0 = 1.0; break;
        }
    }

    // drift。Automatic は解析値、Manual は block ごとの予測なしで設定値の幅だけ探す。
    int base = 0, range = 0;
    if (p.adptMode == 0) {
        base = c.baseOfs;
        range = c.range;
    } else if (p.adptMode == 1) {
        range = p.driftRange;
    }

    bool phase = false;
    switch (p.krkPhase) {
    case 0: phase = c.phase; break;
    case 2: phase = true; break;
    default: phase = false; break;
    }

    Worker w;
    w.init(c.channels, c.rate, p.blockSec * 2.0f, ovs, c.quantize);
    w.setGpu(c);
    w.setPhase(phase);
    Pipeline pipe;
    pipe.init(c, p.mergeMode == 0);

    int offset = 0;
    switch (p.introMode) {
    case 0: offset = c.offset; break;
    case 1: {
        Stage st(c, 0.4f, 0.45f);
        offset = simpleOffset(c, phase);
        break;
    }
    case 2: {
        Stage st(c, 0.4f, 0.45f);
        offset = detailOffset(c, false, phase);
        break;
    }
    default: offset = 0; break;
    }
    if (c.hasAnalysisInst) std::swap(c.inst, c.analysisInst);
    if (c.cancelled) return 0;

    c.usedOffset = offset;
    c.usedPhase = phase;
    c.usedLevel = g0;
    c.adaptiveLevel = adaptive;
    c.usedBase = base;
    c.usedRange = range;

    // 冒頭は探索幅 (と負のオフセット) の分だけ、インストなしで書き出す。
    c.orig.seek(0);
    if (offset < 0) {
        c.inst.seek(range);
        passThrough(c, w, pipe, range - offset, false);
    } else {
        c.inst.seek(offset + range);
        passThrough(c, w, pipe, range, false);
    }

    const int n = w.n(), q = n / 4, half = n / 2;
    long long tO = c.orig.tell(), tI = c.inst.tell();
    passThrough(c, w, pipe, q, true);
    c.orig.seek(tO);
    c.inst.seek(tI);

    Stage st(c, 0.45f, 1.0f);
    int blocks = 0;
    const char* what = c.collect ? "alignment pass" : p.mergeMode == 1 ? "Waveform subtraction" : "Frequency extraction";
    log::Stage ls(what);
    log::detail("  offset %d, phase %s, level %s, drift %d/blk +- %d, block %d samples%s", offset, phase ? "inverted" : "normal",
                adaptive ? "adaptive" : std::to_string(g0).c_str(), base, range, w.n(),
                ovs > 1 ? (", oversampling x" + std::to_string(ovs)).c_str() : "");
    const long long totalBlocks = std::max<long long>(1, (c.orig.frames - tO) / std::max(1, half) + 1);
    log::Eta eta;
    for (;;) {
        tI -= range;
        c.inst.seek(tI);
        const int ro = c.orig.read(w.rawOrig(), n);
        const int ri = c.inst.read(w.rawInst(), w.m());
        if (ro == 0 || ri == 0) break;
        w.prepare();

        int pos = 0;
        double g = g0;
        switch (kind) {
        case 0:
            pos = w.searchCh(range, range, 1);
            if (adaptive) {
                g = w.levelFitCh(pos, 0.02, 0.2, 1.0);
                g = w.levelFitCh(pos, 0.002, 0.03, g);
            }
            if (p.mergeMode == 1) w.sub(pos, g);
            break;
        case 1:
        case 2:
            if (kind == 1) w.toLRDiff(); else w.toMono();
            pos = w.searchMix(range, range, 1, n);
            if (adaptive) {
                g = w.levelFitMix(pos, 0.02, 0.2, 1.0);
                g = w.levelFitMix(pos, 0.002, 0.03, g);
            }
            if (p.mergeMode == 1) {
                if (kind == 1) w.sub(pos, g); else w.subMono(pos, g);
            }
            break;
        case 3:
            w.upsample();
            pos = w.searchOvsCh(range * ovs, range * ovs, ovs);
            pos = w.searchOvsCh(pos, ovs - 1, 1);
            if (adaptive) {
                g = w.levelFitOvsCh(pos, 0.02, 0.2, 1.0);
                g = w.levelFitOvsCh(pos, 0.002, 0.03, g);
            }
            w.subOvs(pos, g);
            break;
        case 4:
        case 5:
            if (kind == 4) w.toLRDiff(); else w.toMono();
            w.upsample();
            w.upsampleMix();
            pos = w.searchOvsMix(range * ovs, range * ovs, ovs);
            pos = w.searchOvsMix(pos, ovs - 1, 1);
            if (adaptive) {
                g = w.levelFitOvsMix(pos, 0.02, 0.2, 1.0);
                g = w.levelFitOvsMix(pos, 0.002, 0.03, g);
            }
            if (kind == 4) w.subOvs(pos, g); else w.subOvsMono(pos, g);
            break;
        default:
            break;
        }

        // oversampling の位置は 1/ovs sample 単位。By Frequency はその場合ないので pos をそのまま使う。
        emit(c, p, w, pipe, q, half, pos, g, kind >= 3 ? ovs : 1, true);

        // 次の block のインスト位置は、今回見つけた位置 + drift 予測 + 半 block。
        tI += pos / ovs + base + half;
        tO += half;
        c.orig.seek(tO);
        ++blocks;
        const double frac = streamFraction(c.orig);
        log::status(1, false, "  %s | block %d/%lld | %s / %s | %s elapsed | ~%s left", log::progressBar(frac).c_str(), blocks,
                    totalBlocks, log::clock(static_cast<double>(tO) / c.rate).c_str(),
                    log::clock(static_cast<double>(c.orig.frames) / c.rate).c_str(), log::clock(eta.elapsed()).c_str(),
                    log::clock(eta.remaining(frac)).c_str());
        if ((blocks & 7) == 0 && !c.poll(frac)) break;
    }
    log::clearStatus(1);
    if (c.cancelled) ls.fail();
    if (!c.cancelled) pipe.flush();
    return 0;
}

// 元実装 0x411818。原曲とインストに同じファイルが指定されたときの処理。インストを使わず後処理だけを掛ける。
int processAlt(Context& c) {
    const Params& p = c.p;
    Worker w;
    w.init(c.channels, c.rate, p.blockSec * 2.0f, 1, c.quantize);
    w.setGpu(c);
    Pipeline pipe;
    pipe.init(c, false);
    c.orig.seek(0);
    const int n = w.n();
    for (int blocks = 1;; ++blocks) {
        w.reset();
        if (c.orig.read(w.rawOrig(), n) == 0) break;
        w.prepare();
        w.sub(0, 0.0);
        pipe.pushOutput(w.out(0), w.out(1), n);
        if ((blocks & 7) == 0 && !c.poll(streamFraction(c.orig))) break;
    }
    if (!c.cancelled) pipe.flush();
    return 0;
}

}
}
