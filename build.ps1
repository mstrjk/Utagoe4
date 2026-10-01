# ネイティブコアと WinForms フロントエンドをまとめてビルドするスクリプト。
# 前提: PATH 上に 64-bit MinGW-w64 の g++、CMake、Ninja と .NET 10 SDK が必要。
# 同梱ライブラリの source は deps.ps1 が取得する (初回だけ network を使う)。
# 生成物:
#   dist\Utagoe.exe                                    配布用の 1 file。初回起動で %LOCALAPPDATA%\Utagoe\ に導入される
#   utagoe-ui\bin\Release\net10.0-windows\win-x64\     開発用。lib\ を横に置いてその場で動く

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $MyInvocation.MyCommand.Path
$core  = Join-Path $root 'utagoe-core'
$build = Join-Path $core 'build'
$ui    = Join-Path $root 'utagoe-ui\Utagoe.csproj'
$dist  = Join-Path $root 'dist'

# Windows PowerShell 5.1 は Stop のままだと native command の stderr (警告だけでも) を例外にする。成否は終了コードだけで判定する。
function Invoke-Native([string]$what, [scriptblock]$cmd) {
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $cmd 2>&1 | ForEach-Object { "$_" } } finally { $ErrorActionPreference = $saved }
    if ($LASTEXITCODE) { throw "$what failed" }
}

Write-Host 'deps: third-party sources'
& (Join-Path $root 'deps.ps1')

Write-Host 'core: configure'
Invoke-Native 'CMake configure' { cmake -S $core -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -Wno-author } | Out-Null

Write-Host 'core: build (utagoe_core.dll, codec DLLs, utagoe.exe, tests)'
Invoke-Native 'core build' { cmake --build $build }

# テストは DLL 版を PowerShell から読み込んで実行する。未署名 exe を止める環境 (Smart App Control など) でも動く。
# 同梱ライブラリの DLL は utagoe_tests.dll と同じ folder にあるので、その folder から依存先を探すよう読み込む。
Write-Host 'core: tests'
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class UtagoeTestRunner {
    const uint LOAD_WITH_ALTERED_SEARCH_PATH = 0x8;
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi)]
    static extern IntPtr GetProcAddress(IntPtr module, string name);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    delegate int RunTests([MarshalAs(UnmanagedType.LPStr)] string logPath);
    public static int Run(string dll, string log) {
        IntPtr m = LoadLibraryExW(dll, IntPtr.Zero, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (m == IntPtr.Zero) throw new Exception("cannot load " + dll + " (error " + Marshal.GetLastWin32Error() + ")");
        var run = (RunTests)Marshal.GetDelegateForFunctionPointer(GetProcAddress(m, "utagoe_run_tests"), typeof(RunTests));
        return run(log);
    }
}
"@
$log = Join-Path $build 'test.log'
$failures = [UtagoeTestRunner]::Run((Join-Path $build 'utagoe_tests.dll'), $log)
Get-Content $log -Encoding UTF8 | Select-Object -Last 1
if ($failures -ne 0) { Get-Content $log -Encoding UTF8 | Select-String 'FAIL'; throw 'core tests failed' }

Write-Host 'ui: development build'
Invoke-Native 'UI build' { dotnet build $ui -c Release --nologo -v quiet }

# 配布用の本体: .NET を同梱しない 1 つの exe (native DLL と license 文書は中に埋め込まれている)。
$app = Join-Path $build 'app'
Write-Host 'ui: app (single file, uses the installed .NET Desktop Runtime)'
Invoke-Native 'UI publish' {
    dotnet publish $ui -c Release -r win-x64 --self-contained false --nologo -v quiet -o $app `
        -p:PublishSingleFile=true -p:DebugType=embedded
}

# 配布用の setup: 本体を圧縮して埋め込み、.NET が無ければ Microsoft から入れてから本体を起動する native の exe。
Write-Host 'setup: dist\Utagoe.exe'
$payload = Join-Path $app 'Utagoe.exe'
Invoke-Native 'CMake configure (setup)' { cmake -S $core -B $build "-DUTAGOE_SETUP_PAYLOAD=$payload" } | Out-Null
Invoke-Native 'setup build' { cmake --build $build --target utagoe_setup } | Select-String 'packed' | ForEach-Object { "  $_" }
New-Item -ItemType Directory -Force $dist | Out-Null
Get-ChildItem $dist | Remove-Item -Recurse -Force
Copy-Item (Join-Path $build 'UtagoeSetup.exe') (Join-Path $dist 'Utagoe.exe')
$size = (Get-Item (Join-Path $dist 'Utagoe.exe')).Length / 1MB

Write-Host ("done: {0}\Utagoe.exe ({1:0.0} MB)" -f $dist, $size)
