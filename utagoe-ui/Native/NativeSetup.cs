// native DLL の読み込み準備 (v3 にない)。utagoe_core.dll と同梱ライブラリの DLL は lib\ に置く。
// DLL の検索先を System32 と lib\ などの既定の場所に限り、current directory や PATH からの読み込みを防ぐ。
// core の記録は LogHub へ流す。

using System.Reflection;
using System.Runtime.InteropServices;
using Utagoe.App;

namespace Utagoe.Native;

internal static class NativeSetup
{
    private const uint LOAD_LIBRARY_SEARCH_DEFAULT_DIRS = 0x00001000;

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool SetDefaultDllDirectories(uint flags);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr AddDllDirectory(string path);

    // core の callback。native 側から呼ばれる間に GC で回収されないよう static に持つ。
    private static Core.LogCallback? s_log;

    public static void Configure(string libDir)
    {
        SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (Directory.Exists(libDir)) AddDllDirectory(libDir);

        NativeLibrary.SetDllImportResolver(Assembly.GetExecutingAssembly(), (name, _, _) =>
        {
            string candidate = Path.Combine(libDir, name.EndsWith(".dll", StringComparison.OrdinalIgnoreCase) ? name : name + ".dll");
            return File.Exists(candidate) && NativeLibrary.TryLoad(candidate, out IntPtr h) ? h : IntPtr.Zero;
        });
    }

    /// core の記録を LogHub に流し、crash 記録の置き場所を教える。core が読めなければ何もしない。
    public static void ConnectLog(string logsDir)
    {
        try
        {
            s_log = (kind, id, text, _) => LogHub.Add((LogKind)kind, id, Marshal.PtrToStringUTF8(text) ?? "");
            Core.SetLog(s_log, IntPtr.Zero);
            Directory.CreateDirectory(logsDir);
            Core.SetLogDir(logsDir);
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException or BadImageFormatException)
        {
            LogHub.Exception("the native core could not be connected to the log", ex);
        }
    }
}
