// 配布用の setup (dist\Utagoe.exe)。
// Utagoe 本体は .NET を同梱しない (同梱すると 25 MB 以上になる) ので、.NET Desktop Runtime 10 以降が要る。
// 1. runtime が無ければ、断ってから Microsoft の配布元から取ってきて、Microsoft の署名を確かめてから入れる。
// 2. 埋め込んだ本体 (LZMS 圧縮) を一時 folder に展開して起動する。本体が %LOCALAPPDATA%\Utagoe への導入をする。
// .NET が無い状態で動く必要があるので native (C++) で書く。

#include <windows.h>
#include <commctrl.h>
#include <compressapi.h>
#include <shellapi.h>
#include <softpub.h>
#include <urlmon.h>
#include <wintrust.h>
#include <wincrypt.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>
#include <thread>
#include <vector>

#include "setup_ids.h"

namespace {

constexpr wchar_t kTitle[] = L"Utagoe Setup";
constexpr wchar_t kRuntimeUrl[] = L"https://aka.ms/dotnet/10.0/windowsdesktop-runtime-win-x64.exe";
constexpr wchar_t kRuntimePage[] = L"https://dotnet.microsoft.com/download/dotnet/10.0";
constexpr int kNeedMajor = 10;


std::wstring DotnetRoot() {
    wchar_t buf[MAX_PATH];
    DWORD size = sizeof buf;
    // x64 の導入先は 32-bit 側の registry に書かれる。
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\dotnet\\Setup\\InstalledVersions\\x64", L"InstallLocation",
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY, nullptr, buf, &size) == ERROR_SUCCESS)
        return buf;
    if (GetEnvironmentVariableW(L"ProgramFiles", buf, MAX_PATH)) return std::wstring(buf) + L"\\dotnet";
    return L"C:\\Program Files\\dotnet";
}

// 試験用: UTAGOE_SETUP_TEST=1 なら runtime が無い扱いにし、取得と署名の確認までして導入はしない。
bool TestMode() {
    wchar_t v[8];
    return GetEnvironmentVariableW(L"UTAGOE_SETUP_TEST", v, 8) && v[0] == L'1';
}

bool HasDesktopRuntime() {
    if (TestMode()) return false;
    std::wstring dir = DotnetRoot();
    if (!dir.empty() && dir.back() != L'\\') dir += L'\\';
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"shared\\Microsoft.WindowsDesktop.App\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && std::wcstol(fd.cFileName, nullptr, 10) >= kNeedMajor) found = true;
    } while (!found && FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}


constexpr UINT WM_PROGRESS = WM_APP + 1;   // wParam: 0..1000 (‰)、lParam: 受け取った byte 数 (MB 表示用)
constexpr UINT WM_DONE = WM_APP + 2;       // wParam には HRESULT が入る。

HWND g_window = nullptr, g_label = nullptr, g_bar = nullptr;
std::atomic<bool> g_cancel{false};

LRESULT CALLBACK WindowProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CLOSE:
        g_cancel = true;   // 取得中なら止める。導入中は導入の画面に任せる。
        return 0;
    case WM_PROGRESS:
        SendMessageW(g_bar, PBM_SETPOS, w, 0);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

void CreateProgressWindow() {
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);
    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_APP));
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"UtagoeSetupWindow";
    RegisterClassW(&wc);

    const UINT dpi = GetDpiForSystem();
    auto S = [dpi](int v) { return MulDiv(v, static_cast<int>(dpi), 96); };
    RECT r{0, 0, S(440), S(110)};
    AdjustWindowRect(&r, WS_CAPTION | WS_SYSMENU, FALSE);
    const int w = r.right - r.left, h = r.bottom - r.top;
    g_window = CreateWindowW(wc.lpszClassName, kTitle, WS_CAPTION | WS_SYSMENU,
                             (GetSystemMetrics(SM_CXSCREEN) - w) / 2, (GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h,
                             nullptr, nullptr, wc.hInstance, nullptr);
    NONCLIENTMETRICSW ncm{sizeof ncm};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    HFONT font = CreateFontIndirectW(&ncm.lfMessageFont);
    g_label = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, S(16), S(18), S(408), S(40), g_window, nullptr, wc.hInstance, nullptr);
    SendMessageW(g_label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    g_bar = CreateWindowW(PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE, S(16), S(64), S(408), S(22), g_window, nullptr, wc.hInstance, nullptr);
    SendMessageW(g_bar, PBM_SETRANGE32, 0, 1000);
    ShowWindow(g_window, SW_SHOW);
    UpdateWindow(g_window);
}

void SetStatus(const wchar_t* text, bool marquee) {
    SetWindowTextW(g_label, text);
    LONG_PTR style = GetWindowLongPtrW(g_bar, GWL_STYLE);
    SetWindowLongPtrW(g_bar, GWL_STYLE, marquee ? (style | PBS_MARQUEE) : (style & ~PBS_MARQUEE));
    SendMessageW(g_bar, PBM_SETMARQUEE, marquee, 30);
}


class Download : public IBindStatusCallback {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (id == IID_IUnknown || id == IID_IBindStatusCallback) { *out = this; return S_OK; }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE OnStartBinding(DWORD, IBinding*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPriority(LONG*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE OnLowResource(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnProgress(ULONG done, ULONG total, ULONG, LPCWSTR) override {
        if (total > 0) PostMessageW(g_window, WM_PROGRESS, static_cast<WPARAM>(1000ull * done / total), done);
        return g_cancel ? E_ABORT : S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnStopBinding(HRESULT, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetBindInfo(DWORD* flags, BINDINFO*) override {
        *flags = BINDF_GETNEWESTVERSION | BINDF_NOWRITECACHE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDataAvailable(DWORD, DWORD, FORMATETC*, STGMEDIUM*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnObjectAvailable(REFIID, IUnknown*) override { return S_OK; }
};

// message を回しながら待つ (window が固まらないように)。
void Pump(HANDLE wait) {
    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(wait ? 1 : 0, wait ? &wait : nullptr, FALSE, INFINITE, QS_ALLINPUT);
        if (wait && r == WAIT_OBJECT_0) return;
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (m.message == WM_DONE && m.hwnd == g_window) return;
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
}

// 署名の確認: 信頼できる発行元の正しい署名があり、署名者が Microsoft Corporation の file だけを実行する

bool SignedByMicrosoft(const std::wstring& file) {
    WINTRUST_FILE_INFO fi{sizeof fi, file.c_str(), nullptr, nullptr};
    WINTRUST_DATA wd{};
    wd.cbStruct = sizeof wd;
    wd.dwUIChoice = WTD_UI_NONE;
    wd.fdwRevocationChecks = WTD_REVOKE_NONE;
    wd.dwUnionChoice = WTD_CHOICE_FILE;
    wd.pFile = &fi;
    wd.dwStateAction = WTD_STATEACTION_VERIFY;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG trust = WinVerifyTrust(nullptr, &action, &wd);
    wd.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &action, &wd);
    if (trust != ERROR_SUCCESS) return false;

    HCERTSTORE store = nullptr;
    HCRYPTMSG msg = nullptr;
    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, file.c_str(), CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                          CERT_QUERY_FORMAT_FLAG_BINARY, 0, nullptr, nullptr, nullptr, &store, &msg, nullptr))
        return false;
    bool ok = false;
    DWORD size = 0;
    if (CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &size)) {
        std::vector<unsigned char> buf(size);
        if (CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, buf.data(), &size)) {
            auto* si = reinterpret_cast<CMSG_SIGNER_INFO*>(buf.data());
            CERT_INFO ci{};
            ci.Issuer = si->Issuer;
            ci.SerialNumber = si->SerialNumber;
            if (PCCERT_CONTEXT cert = CertFindCertificateInStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
                                                                 CERT_FIND_SUBJECT_CERT, &ci, nullptr)) {
                // 証明書の組織 (O=) が Microsoft Corporation であること (表示名は ".NET" などになる)。
                wchar_t org[256];
                CertGetNameStringW(cert, CERT_NAME_ATTR_TYPE, 0, const_cast<char*>(szOID_ORGANIZATION_NAME), org, 256);
                ok = std::wcscmp(org, L"Microsoft Corporation") == 0;
                CertFreeCertificateContext(cert);
            }
        }
    }
    CryptMsgClose(msg);
    CertCloseStore(store, 0);
    return ok;
}

std::wstring TempDir() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"Utagoe-setup-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

void Fail(const std::wstring& text) {
    if (g_window) ShowWindow(g_window, SW_HIDE);
    MessageBoxW(nullptr, text.c_str(), kTitle, MB_OK | MB_ICONERROR);
}

// .NET Desktop Runtime を取ってきて入れる。成功したら true。
bool InstallRuntime(const std::wstring& dir) {
    int answer = MessageBoxW(nullptr,
        L"Utagoe needs the free Microsoft .NET Desktop Runtime (version 10).\n\n"
        L"It is about 57 MB, installed once, and shared by every app that uses it. "
        L"Utagoe will download it from Microsoft and install it now (Windows will ask for permission).\n\n"
        L"Continue?",
        kTitle, MB_YESNO | MB_ICONINFORMATION);
    if (answer != IDYES) return false;

    CreateProgressWindow();
    SetStatus(L"Downloading the .NET Desktop Runtime from Microsoft...", false);
    const std::wstring file = dir + L"\\windowsdesktop-runtime-win-x64.exe";
    HRESULT hr = E_FAIL;
    std::thread worker([&] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Download cb;
        hr = URLDownloadToFileW(nullptr, kRuntimeUrl, file.c_str(), 0, &cb);
        CoUninitialize();
        PostMessageW(g_window, WM_DONE, 0, 0);
    });
    Pump(nullptr);
    worker.join();
    if (g_cancel) { DestroyWindow(g_window); g_window = nullptr; return false; }
    if (FAILED(hr)) {
        Fail(L"The .NET Desktop Runtime could not be downloaded. Check the internet connection and try again, "
             L"or install it yourself from:\n\n" + std::wstring(kRuntimePage));
        return false;
    }
    if (!SignedByMicrosoft(file)) {
        DeleteFileW(file.c_str());
        Fail(L"The downloaded file is not signed by Microsoft, so it was not run. Install the runtime yourself from:\n\n" +
             std::wstring(kRuntimePage));
        return false;
    }

    if (TestMode()) {
        DeleteFileW(file.c_str());
        DestroyWindow(g_window);
        g_window = nullptr;
        MessageBoxW(nullptr, L"Test mode: the runtime was downloaded and is signed by Microsoft. It was not installed.", kTitle, MB_OK);
        return false;
    }

    SetStatus(L"Installing the .NET Desktop Runtime...", true);
    SHELLEXECUTEINFOW se{sizeof se};
    se.fMask = SEE_MASK_NOCLOSEPROCESS;
    se.hwnd = g_window;
    se.lpFile = file.c_str();
    se.lpParameters = L"/install /passive /norestart";
    se.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&se) || !se.hProcess) {
        DeleteFileW(file.c_str());
        Fail(L"The .NET Desktop Runtime installer could not be started.");
        return false;
    }
    Pump(se.hProcess);
    DWORD code = 1;
    GetExitCodeProcess(se.hProcess, &code);
    CloseHandle(se.hProcess);
    DeleteFileW(file.c_str());
    DestroyWindow(g_window);
    g_window = nullptr;
    // 0 は成功、3010 は再起動が要るが導入は済んだ。
    if ((code != 0 && code != 3010) || !HasDesktopRuntime()) {
        Fail(L"The .NET Desktop Runtime was not installed (code " + std::to_wstring(code) + L"). Install it yourself from:\n\n" +
             std::wstring(kRuntimePage));
        return false;
    }
    return true;
}

bool ExtractApp(const std::wstring& path) {
    HMODULE self = GetModuleHandleW(nullptr);
    HRSRC res = FindResourceW(self, MAKEINTRESOURCEW(IDR_PAYLOAD), RT_RCDATA);
    if (!res) return false;
    const auto* data = static_cast<const unsigned char*>(LockResource(LoadResource(self, res)));
    const DWORD size = SizeofResource(self, res);
    if (!data || size < 12 || std::memcmp(data, "UTGZ", 4) != 0) return false;
    std::uint64_t original = 0;
    std::memcpy(&original, data + 4, sizeof original);
    std::vector<unsigned char> out(static_cast<std::size_t>(original));
    DECOMPRESSOR_HANDLE d = nullptr;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &d)) return false;
    SIZE_T got = 0;
    const BOOL ok = Decompress(d, const_cast<unsigned char*>(data + 12), size - 12, out.data(), out.size(), &got);
    CloseDecompressor(d);
    if (!ok || got != out.size()) return false;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL wrote = WriteFile(f, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
    CloseHandle(f);
    return wrote && written == out.size();
}

// この exe に渡された引数 (exe の名前を除く部分)。
std::wstring Arguments() {
    const wchar_t* p = GetCommandLineW();
    if (*p == L'"') { ++p; while (*p && *p != L'"') ++p; if (*p) ++p; }
    else while (*p && *p != L' ' && *p != L'\t') ++p;
    while (*p == L' ' || *p == L'\t') ++p;
    return p;
}

}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const std::wstring dir = TempDir();

    if (!HasDesktopRuntime() && !InstallRuntime(dir)) {
        RemoveDirectoryW(dir.c_str());
        return 1;
    }

    const std::wstring app = dir + L"\\Utagoe.exe";
    if (!ExtractApp(app)) {
        Fail(L"Utagoe could not be unpacked to the temporary folder:\n\n" + dir);
        return 1;
    }
    std::wstring cmd = L"\"" + app + L"\" " + Arguments();
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(app.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
        Fail(L"Utagoe could not be started (error " + std::to_wstring(GetLastError()) + L").");
        DeleteFileW(app.c_str());
        RemoveDirectoryW(dir.c_str());
        return 1;
    }
    // 本体が導入を済ませて導入先の Utagoe を起動し終えるまで待ち、一時 file を片付ける。
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    DeleteFileW(app.c_str());
    RemoveDirectoryW(dir.c_str());
    return static_cast<int>(code);
}
