// VCL TTrackBar と同じ Win32 style を持つ TrackBar。
// 実機の GetWindowLong / TBM_GETTHUMBLENGTH から style を確認済み。settings は両側 tick + thumb 15、playback は既定の片側 tick + thumb 20。
// VCL は親背景を透過的に描くが WinForms は BackColor で塗るため、実際に描画している親 surface の色を拾う。

using System.Runtime.InteropServices;
using System.Windows.Forms.VisualStyles;

namespace Utagoe.Vcl;

internal sealed class VclTrackBar : TrackBar
{
    private const int TBS_ENABLESELRANGE = 0x0020;
    private const int TBS_FIXEDLENGTH = 0x0040;
    private const int TBM_SETTHUMBLENGTH = 0x041B;
    private readonly int _thumbLength;

    public VclTrackBar(bool ticksBoth = false, int thumbLength = 20)
    {
        _thumbLength = thumbLength;
        AutoSize = false;
        TickStyle = ticksBoth ? TickStyle.Both : TickStyle.BottomRight;
        TickFrequency = 1;
    }

    protected override CreateParams CreateParams
    {
        get
        {
            var cp = base.CreateParams;
            cp.Style |= TBS_FIXEDLENGTH | TBS_ENABLESELRANGE;
            return cp;
        }
    }

    protected override void OnHandleCreated(EventArgs e)
    {
        base.OnHandleCreated(e);
        int len = _thumbLength * DeviceDpi / 96;
        SendMessage(Handle, TBM_SETTHUMBLENGTH, (IntPtr)len, IntPtr.Zero);
        MatchParentBackground();
    }

    protected override void OnParentChanged(EventArgs e)
    {
        base.OnParentChanged(e);
        MatchParentBackground();
    }

    private void MatchParentBackground()
    {
        // transparent container を上へ辿り、実際に背景を描く surface を探す。
        for (Control? p = Parent; p != null; p = p.Parent)
        {
            if (p is TabPage { UseVisualStyleBackColor: true } && VisualStyleRenderer.IsSupported)
            {
                BackColor = TabBodyColor();
                return;
            }
            if (p is not GroupBox)
            {
                BackColor = p.BackColor;
                return;
            }
        }
    }

    private static Color? _tabBody;

    // themed tab-page body を描いて背景色を sample する。
    private static Color TabBodyColor()
    {
        if (_tabBody is Color c) return c;
        using var bmp = new Bitmap(32, 32);
        using (var g = Graphics.FromImage(bmp))
            TabRenderer.DrawTabPage(g, new Rectangle(0, 0, 32, 32));
        return (_tabBody = bmp.GetPixel(16, 16)).Value;
    }

    [DllImport("user32.dll")]
    private static extern IntPtr SendMessage(IntPtr hwnd, int msg, IntPtr wp, IntPtr lp);
}
