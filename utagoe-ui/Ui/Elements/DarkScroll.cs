using System.Runtime.InteropServices;

using static Utagoe.Ui.Win32;

namespace Utagoe.Ui;

internal static class DarkScroll
{
    /// scroll bar などを Windows の暗い見た目にする (暗い theme のときだけ)。
    public static void Apply(Control c, bool dark)
    {
        if (!c.IsHandleCreated) { c.HandleCreated += (_, _) => Apply(c, dark); return; }
        try { SetWindowTheme(c.Handle, dark ? "DarkMode_Explorer" : null, null); c.Invalidate(); }
        catch (DllNotFoundException) { }
        catch (EntryPointNotFoundException) { }
    }
}
