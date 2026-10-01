// VCL TRadioGroup と同じ配置規則で radio button を並べる GroupBox。
// TRadioGroup は固定 pitch ではなく group 全高を item 数で等分して配置する。
// Extraction Method の 241px group ではこの規則で 2 item が上下半分へ自然に分かれる。

using System.ComponentModel;
using System.Runtime.InteropServices;
using Utagoe.App;

namespace Utagoe.Vcl;

internal sealed class VclRadioGroup : VclGroupBox
{
    private readonly List<RadioButton> _buttons = new();
    // 最後に選ばれた button。click では新しい button が先に checked になり、古い方が外れるのはその後なので、
    // CheckedChanged の中で checked を探すと古い方を返してしまう。選ばれた順で覚えておく。
    private int _index = -1;
    private ScrollHost? _host;

    public event EventHandler? ItemIndexChanged;

    internal sealed class ScrollHost : Panel
    {
        public ScrollHost()
        {
            HorizontalScroll.Maximum = 0;
            HorizontalScroll.Enabled = false;
            HorizontalScroll.Visible = false;
            AutoScroll = true;
            SetStyle(ControlStyles.OptimizedDoubleBuffer | ControlStyles.AllPaintingInWmPaint, true);
            HandleCreated += (_, _) => DarkScroll.Apply(this, Theme.Current.Dark);
            Theme.Changed += OnTheme;
        }

        private void OnTheme()
        {
            if (IsHandleCreated) DarkScroll.Apply(this, Theme.Current.Dark);
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing) Theme.Changed -= OnTheme;
            base.Dispose(disposing);
        }
    }

    [DefaultValue(false)]
    public bool Scrollable
    {
        get => _host != null;
        set
        {
            if (value == (_host != null)) return;
            var items = Items;
            int index = ItemIndex;
            if (value)
            {
                _host = new ScrollHost();
                Controls.Add(_host);
            }
            else
            {
                Controls.Remove(_host);
                _host!.Dispose();
                _host = null;
            }
            Items = items;
            if (index >= 0) ItemIndex = index;
        }
    }

    [DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
    public string[] Items
    {
        get => _buttons.Select(b => b.Text).ToArray();
        set
        {
            foreach (var b in _buttons) { b.Parent?.Controls.Remove(b); b.Dispose(); }
            _buttons.Clear();
            foreach (var text in value)
            {
                var rb = new ThemedRadioButton
                {
                    Text = text,
                    AutoSize = false,
                    CheckAlign = ContentAlignment.MiddleLeft,
                    TextAlign = ContentAlignment.MiddleLeft,
                };
                rb.CheckedChanged += (_, _) =>
                {
                    if (!rb.Checked) return;
                    _index = _buttons.IndexOf(rb);
                    _host?.ScrollControlIntoView(rb);
                    ItemIndexChanged?.Invoke(this, EventArgs.Empty);
                };
                _buttons.Add(rb);
                (_host ?? (Control)this).Controls.Add(rb);
            }
            Arrange();
        }
    }

    [DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
    public int ItemIndex
    {
        get => _index >= 0 && _index < _buttons.Count && _buttons[_index].Checked ? _index : _buttons.FindIndex(b => b.Checked);
        set
        {
            _index = value;
            for (int i = 0; i < _buttons.Count; i++) _buttons[i].Checked = i == value;
            if (_host != null && value >= 0 && value < _buttons.Count) _host.ScrollControlIntoView(_buttons[value]);
        }
    }

    protected override void OnResize(EventArgs e) { base.OnResize(e); Arrange(); }
    protected override void OnFontChanged(EventArgs e) { base.OnFontChanged(e); Arrange(); }

    private void Arrange()
    {
        int count = _buttons.Count;
        if (count == 0) return;

        int tm = TextMetricHeight(Font);
        int avail = Height - tm - 5;
        if (_host != null)
        {
            ArrangeScrolling(tm, avail);
            return;
        }
        int buttonHeight = avail / count;
        int topMargin = tm + 1 + (avail % count) / 2;
        int width = Width - 10;

        for (int i = 0; i < count; i++)
            _buttons[i].SetBounds(8, topMargin + i * buttonHeight, width, buttonHeight);
    }

    private void ArrangeScrolling(int tm, int avail)
    {
        var host = _host!;
        int pitch = tm + Math.Max(4, tm / 3);
        int count = _buttons.Count;
        bool overflow = count * pitch > avail;
        int scrollY = -host.AutoScrollPosition.Y;
        host.SuspendLayout();
        host.AutoScrollPosition = Point.Empty;
        host.SetBounds(2, tm + 1, Width - 4, avail);
        int width = host.Width - 8 - (overflow ? SystemInformation.VerticalScrollBarWidth : 0);
        int top = overflow ? 0 : (avail - count * pitch) / 2;
        for (int i = 0; i < count; i++)
            _buttons[i].SetBounds(6, top + i * pitch, width, pitch);
        host.AutoScrollMinSize = overflow ? new Size(0, count * pitch) : Size.Empty;
        host.ResumeLayout(true);
        if (overflow) host.AutoScrollPosition = new Point(0, scrollY);
    }

    // font の GDI tmHeight は VCL の GetTextMetrics が見る値を使う。
    private static int TextMetricHeight(Font font)
    {
        IntPtr dc = GetDC(IntPtr.Zero);
        IntPtr hfont = font.ToHfont();
        try
        {
            IntPtr old = SelectObject(dc, hfont);
            GetTextMetricsW(dc, out TEXTMETRICW tm);
            SelectObject(dc, old);
            return tm.tmHeight;
        }
        finally
        {
            DeleteObject(hfont);
            ReleaseDC(IntPtr.Zero, dc);
        }
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct TEXTMETRICW
    {
        public int tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading,
                   tmAveCharWidth, tmMaxCharWidth, tmWeight, tmOverhang,
                   tmDigitizedAspectX, tmDigitizedAspectY;
        public char tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar;
        public byte tmItalic, tmUnderlined, tmStruckOut, tmPitchAndFamily, tmCharSet;
    }

    [DllImport("user32.dll")] private static extern IntPtr GetDC(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern int ReleaseDC(IntPtr hwnd, IntPtr dc);
    [DllImport("gdi32.dll")] private static extern IntPtr SelectObject(IntPtr dc, IntPtr obj);
    [DllImport("gdi32.dll")] private static extern bool DeleteObject(IntPtr obj);
    [DllImport("gdi32.dll", CharSet = CharSet.Unicode)]
    private static extern bool GetTextMetricsW(IntPtr dc, out TEXTMETRICW tm);
}
