// VCL の font 基準 form scaling を再現する。
// 元 DFM は TextHeight=12 だが英語版 Tahoma -12 は 14px。VCL は load 時に 14/12 で form 全体を拡大する。
// 実機測定でも 569x294 -> 664x343、Start button 89x57 -> 104x66 で一致する。
// control は幅そのものではなく left/right/top/bottom の edge を MulDiv して size を求める。
// form から継承した font は追加 scale しない。独自 font の control だけ point size も MulDiv する。
// TextHeight は 96 DPI の pixel-height font で測るので、Windows 側 DPI scaling とは別処理。

using System.Runtime.InteropServices;

using static Utagoe.Ui.Win32;

namespace Utagoe.Ui;

internal static class VclScaling
{
    public const int DesignTextHeight = 12;

    private static readonly HashSet<Control> OwnFont = new();

    /// ParentFont=False の control には font scaling も適用する印。
    public static T WithOwnFont<T>(this T control) where T : Control
    {
        OwnFont.Add(control);
        return control;
    }

    public static void Apply(Form form)
    {
        int m = TahomaTextHeight();
        const int d = DesignTextHeight;
        if (m == d || m <= 0) return;

        form.SuspendLayout();
        Size client = form.ClientSize;
        BevelPainter.Scale(form, m, d);
        ScaleChildren(form, m, d);
        form.ClientSize = new Size(MulDiv(client.Width, m, d), MulDiv(client.Height, m, d));
        form.ResumeLayout(true);
    }

    private static void ScaleChildren(Control parent, int m, int d)
    {
        foreach (Control c in parent.Controls)
        {
            // tab page と VclRadioGroup は自身の layout に任せ、子だけ再帰処理する。
            if (c is not TabPage && parent is not VclRadioGroup)
            {
                int l = MulDiv(c.Left, m, d), t = MulDiv(c.Top, m, d);
                int r = MulDiv(c.Left + c.Width, m, d), b = MulDiv(c.Top + c.Height, m, d);
                c.SetBounds(l, t, r - l, b - t);
            }

            if (OwnFont.Remove(c))
            {
                int pt = (int)Math.Round(c.Font.SizeInPoints);
                c.Font = new Font(c.Font.FontFamily, MulDiv(pt, m, d), c.Font.Style);
            }

            if (c.HasChildren) ScaleChildren(c, m, d);
        }
    }

    /// MulDiv は a*b/c を half-away-from-zero で丸める Win32 挙動に合わせる。
    public static int MulDiv(int a, int b, int c)
    {
        long n = (long)a * b;
        long q = Math.DivRem(n, c, out long rem);
        if (Math.Abs(rem) * 2 >= Math.Abs(c)) q += Math.Sign(n) * Math.Sign(c);
        return (int)q;
    }

    // Tahoma -12px の TextHeight('0') を 96 DPI で使う。
    private static int TahomaTextHeight()
    {
        IntPtr dc = GetDC(IntPtr.Zero);
        IntPtr font = CreateFontW(-12, 0, 0, 0, 400, 0, 0, 0, 1 , 0, 0, 0, 0, "Tahoma");
        try
        {
            IntPtr old = SelectObject(dc, font);
            GetTextExtentPoint32W(dc, "0", 1, out SIZE sz);
            SelectObject(dc, old);
            return sz.Height;
        }
        finally
        {
            DeleteObject(font);
            ReleaseDC(IntPtr.Zero, dc);
        }
    }
}
