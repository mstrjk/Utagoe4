// window の枠と title bar を theme の色で自前に描く (v4)。
// 枠の領域 (non-client) はそのまま残して描くだけを替えるので、client の座標・配置・拡大は何も変わらない。
// Windows の枠の描画 (DWM) を止め、枠・見出し・最小化 / 最大化 / 閉じる button を描き、button の当たり判定と動作も受け持つ。
// 移動・二重 click での最大化・snap・system menu は Windows にそのまま任せる。
// captionHost を渡した window (terminal) は Windows の枠を持たない (FormBorderStyle.None) で、その部品 (terminal の見出し) が
// title bar になる。端の数 px で大きさを変えられ、見出しをつかんで動かせ、見出しの右端に button を描く。
// Windows の大きさ変更の枠は Windows 10 / 11 では見えない 7 px の余白を作り、中身が切れるか帯が見えるので使わない。

using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Utagoe.App;

using static Utagoe.Ui.Win32;

namespace Utagoe.Ui;

internal sealed class ThemedFrame : NativeWindow
{
    private static readonly ConditionalWeakTable<Form, ThemedFrame> Frames = new();

    private readonly Form _form;
    private readonly Control? _captionHost;
    private bool _active = true;
    private int _hot;       // 0 なし、HTMINBUTTON / HTMAXBUTTON / HTCLOSE
    private int _pressed;
    private bool _tracking;

    private ThemedFrame(Form form, Control? captionHost)
    {
        _form = form;
        _captionHost = captionHost;
        Theme.Changed += Redraw;
        form.Disposed += (_, _) => Theme.Changed -= Redraw;
    }

    /// form の枠を theme で描くようにする。handle が作り直されても (main window への組み込みの出し入れ) 付け直す。
    public static void Attach(Form form, Control? captionHost = null)
    {
        if (Frames.TryGetValue(form, out _)) return;
        var frame = new ThemedFrame(form, captionHost);
        Frames.Add(form, frame);
        if (form.IsHandleCreated) frame.Hook();
        form.HandleCreated += (_, _) => frame.Hook();
        form.HandleDestroyed += (_, _) => frame.ReleaseHandle();
    }

    private void Hook()
    {
        if (Handle != IntPtr.Zero) ReleaseHandle();
        // main window に組み込まれた form と枠の無い form は描かない。
        if (!_form.TopLevel || (_captionHost == null && _form.FormBorderStyle == FormBorderStyle.None)) return;
        AssignHandle(_form.Handle);
        _revealed = false;
        if (_captionHost != null)
        {
            // 枠なし: Windows 11 が描く 1 px の縁は消し、自前の縁 (form の Padding) だけにする。部品の端でも大きさを変えられるようにする。
            SetBorderColor();
            foreach (Control c in _form.Controls) PassEdges(c);
            _form.ControlAdded += (_, e) => { if (e.Control != null) PassEdges(e.Control); };
            SetWindowPos(Handle, IntPtr.Zero, 0, 0, 0, 0, 0x0027);
            return;
        }
        SetDwm(Handle, DWMWA_NCRENDERING_POLICY, 1);
        Redraw();
    }

    private void SetBorderColor()
    {
        SetDwm(Handle, DWMWA_BORDER_COLOR, unchecked((int)0xFFFFFFFE));
    }

    // 子の部品の上でも、window の端の近くと見出しの上は親 (この window) に当たり判定を任せる。
    private void PassEdges(Control c)
    {
        if (c.IsHandleCreated) new EdgePass(this, c).AssignHandle(c.Handle);
        else c.HandleCreated += (_, _) => new EdgePass(this, c).AssignHandle(c.Handle);
        foreach (Control child in c.Controls) PassEdges(child);
    }

    private sealed class EdgePass : NativeWindow
    {
        private readonly ThemedFrame _frame;
        private readonly Control _control;
        public EdgePass(ThemedFrame frame, Control control) { _frame = frame; _control = control; control.HandleDestroyed += (_, _) => ReleaseHandle(); }
        protected override void WndProc(ref Message m)
        {
            if (m.Msg == WM_NCHITTEST && _frame.Handle != IntPtr.Zero)
            {
                Point screen = new(unchecked((short)(long)m.LParam), unchecked((short)((long)m.LParam >> 16)));
                if (_control == _frame._captionHost || _frame.ChromelessHit(screen) != HTCLIENT)
                {
                    m.Result = new IntPtr(HTTRANSPARENT);
                    return;
                }
            }
            base.WndProc(ref m);
        }
    }

    /// 枠なしの window の当たり判定 (screen 座標)。端は大きさ変更、見出しは移動か button、それ以外は client。
    private int ChromelessHit(Point screen)
    {
        Point p = _form.PointToClient(screen);
        Size size = _form.ClientSize;
        if (_form.WindowState != FormWindowState.Maximized)
        {
            int e = Math.Max(4, _form.LogicalToDeviceUnits(6));
            bool l = p.X < e, r = p.X >= size.Width - e, t = p.Y < e, b = p.Y >= size.Height - e;
            if (t && l) return HTTOPLEFT;
            if (t && r) return HTTOPRIGHT;
            if (b && l) return HTBOTTOMLEFT;
            if (b && r) return HTBOTTOMRIGHT;
            if (l) return HTLEFT;
            if (r) return HTRIGHT;
            if (t) return HTTOP;
            if (b) return HTBOTTOM;
        }
        if (_captionHost != null && _captionHost.Visible)
        {
            Point h = _captionHost.PointToClient(screen);
            if (_captionHost.ClientRectangle.Contains(h))
            {
                foreach (var (hit, box) in Buttons(HostButtonsArea())) if (box.Contains(h)) return hit;
                return HTCAPTION;
            }
        }
        return HTCLIENT;
    }

    // 見出しの右上に button を並べる範囲 (見出しの座標)。
    private Rectangle HostButtonsArea()
    {
        var host = _captionHost!;
        int h = Math.Min(host.Height, _form.LogicalToDeviceUnits(30));
        return new Rectangle(0, 0, host.Width, h);
    }

    /// 枠なしの window: 見出しの右端に button を描く。見出しの Paint から呼ぶ。button の幅 (見出しの右から) を返す。
    public static int PaintCaptionButtons(Form form, Graphics g)
    {
        if (!Frames.TryGetValue(form, out var frame) || frame.Handle == IntPtr.Zero || frame._captionHost == null) return 0;
        var t = Theme.Current;
        Color text = frame._active ? t.HeaderText : Theme.Blend(t.HeaderText, t.HeaderTop, 0.45);
        var buttons = frame.Buttons(frame.HostButtonsArea());
        foreach (var (hit, box) in buttons) frame.PaintButton(g, hit, box, text, t);
        return buttons.Count == 0 ? 0 : frame._captionHost.Width - buttons.Min(b => b.box.Left);
    }

    private void Repaint()
    {
        if (_captionHost != null) _captionHost.Invalidate();
        else PaintFrame();
    }

    private void Redraw()
    {
        if (Handle == IntPtr.Zero) return;
        if (_captionHost != null) { SetBorderColor(); _captionHost.Invalidate(); return; }
        Transition.After(PaintFrame);
    }


    private (Rectangle window, Rectangle client, Rectangle caption, int frame) Geometry()
    {
        GetWindowRect(Handle, out RECT wr);
        var origin = new POINT();
        ClientToScreen(Handle, ref origin);
        GetClientRect(Handle, out RECT cr);
        int w = wr.Right - wr.Left, h = wr.Bottom - wr.Top;
        int left = origin.X - wr.Left, top = origin.Y - wr.Top;
        var client = new Rectangle(left, top, cr.Right, cr.Bottom);
        int frame = Math.Max(0, left);
        var caption = new Rectangle(frame, frame, Math.Max(0, w - frame * 2), Math.Max(0, top - frame));
        return (new Rectangle(0, 0, w, h), client, caption, frame);
    }

    private List<(int hit, Rectangle box)> Buttons(Rectangle caption)
    {
        var list = new List<(int, Rectangle)>();
        if (!_form.ControlBox || caption.Height <= 0) return list;
        int bh = caption.Height, bw = (int)(bh * 1.45);
        int x = caption.Right;
        void Add(int hit) { x -= bw; list.Add((hit, new Rectangle(x, caption.Top, bw, bh))); }
        Add(HTCLOSE);
        if (_form.MaximizeBox) Add(HTMAXBUTTON);
        if (_form.MinimizeBox) Add(HTMINBUTTON);
        return list;
    }

    private int HitButton(Point windowPoint)
    {
        var (_, _, caption, _) = Geometry();
        foreach (var (hit, box) in Buttons(caption)) if (box.Contains(windowPoint)) return hit;
        return 0;
    }

    private Point ToWindow(IntPtr lParam)
    {
        GetWindowRect(Handle, out RECT wr);
        int x = unchecked((short)(long)lParam), y = unchecked((short)((long)lParam >> 16));
        return new Point(x - wr.Left, y - wr.Top);
    }


    public static bool Draw(Form form, Graphics g)
    {
        if (!Frames.TryGetValue(form, out ThemedFrame? f) || f.Handle == IntPtr.Zero || f._captionHost != null) return false;
        var (window, _, caption, _) = f.Geometry();
        f.Render(g, window, caption);
        return true;
    }

    private void PaintFrame()
    {
        var (window, client, caption, _) = Geometry();
        if (window.Width <= 0 || window.Height <= 0) return;
        int height = _form.WindowState == FormWindowState.Maximized ? Math.Max(1, client.Top) : window.Height;
        using var bmp = new Bitmap(window.Width, height, System.Drawing.Imaging.PixelFormat.Format32bppRgb);
        using (var g = Graphics.FromImage(bmp)) Render(g, window, caption);
        IntPtr hdc = GetWindowDC(Handle);
        if (hdc == IntPtr.Zero) return;
        try
        {
            using var wg = Graphics.FromHdc(hdc);
            wg.ExcludeClip(client);
            wg.DrawImageUnscaled(bmp, 0, 0);
        }
        finally { ReleaseDC(Handle, hdc); }
    }

    private void Render(Graphics g, Rectangle window, Rectangle caption)
    {
        var t = Theme.Current;
        Color text = _active ? t.HeaderText : Theme.Blend(t.HeaderText, t.HeaderTop, 0.45);
        // 枠は見出しの下側の色、外周 1 px は theme の縁の色。非 active のときは面の色へ寄せる。
        Color frameColor = Fade(t.HeaderBottom);
        using (var fb = new SolidBrush(frameColor)) g.FillRectangle(fb, window);
        if (caption.Height > 0)
        {
            if (_active) t.FillHeader(g, caption);
            else using (var cb = new SolidBrush(Fade(t.HeaderTop))) g.FillRectangle(cb, caption);
        }
        using (var edge = new Pen(_active ? t.Edge : Theme.Blend(t.Edge, t.Window, 0.5)))
            g.DrawRectangle(edge, 0, 0, window.Width - 1, window.Height - 1);

        var buttons = Buttons(caption);
        int textLeft = caption.Left + 8;
        if (_form.ShowIcon && _form.Icon != null && caption.Height > 0)
        {
            int size = Math.Max(12, (int)(caption.Height * 0.62));
            var at = new Rectangle(caption.Left + (caption.Height - size) / 2 + 2, caption.Top + (caption.Height - size) / 2, size, size);
            g.DrawIcon(_form.Icon, at);
            textLeft = at.Right + 6;
        }
        int textRight = buttons.Count > 0 ? buttons.Min(b => b.box.Left) - 4 : caption.Right - 6;
        var textBox = Rectangle.FromLTRB(textLeft, caption.Top, Math.Max(textLeft, textRight), caption.Bottom);

        foreach (var (hit, box) in buttons) PaintButton(g, hit, box, text, t);
        if (textBox.Width > 0)
            TextRenderer.DrawText(g, _form.Text, SystemFonts.CaptionFont, textBox, text,
                TextFormatFlags.VerticalCenter | TextFormatFlags.Left | TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPrefix);

        Color Fade(Color c) => _active ? c : Theme.Blend(c, t.Window, 0.5);
    }

    private void PaintButton(Graphics g, int hit, Rectangle box, Color text, Theme t)
    {
        bool hot = _hot == hit, pressed = _pressed == hit && hot;
        Color glyph = text;
        if (hit == HTCLOSE && (hot || pressed))
        {
            using var red = new SolidBrush(pressed ? Color.FromArgb(241, 112, 122) : Color.FromArgb(232, 17, 35));
            g.FillRectangle(red, box);
            glyph = Color.White;
        }
        else if (hot || pressed)
        {
            using var hb = new SolidBrush(Color.FromArgb(pressed ? 70 : 40, t.HeaderText));
            g.FillRectangle(hb, box);
        }

        float s = Math.Max(1f, box.Height / 30f);
        int cx = box.Left + box.Width / 2, cy = box.Top + box.Height / 2, r = (int)Math.Round(5 * s);
        using var pen = new Pen(glyph, Math.Max(1f, s));
        switch (hit)
        {
            case HTCLOSE:
                g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
                g.DrawLine(pen, cx - r, cy - r, cx + r, cy + r);
                g.DrawLine(pen, cx - r, cy + r, cx + r, cy - r);
                g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.Default;
                break;
            case HTMAXBUTTON when _form.WindowState == FormWindowState.Maximized:
                g.DrawRectangle(pen, cx - r, cy - r + (int)(2 * s), r * 2 - (int)(2 * s), r * 2 - (int)(2 * s));
                g.DrawLine(pen, cx - r + (int)(2 * s), cy - r, cx + r, cy - r);
                g.DrawLine(pen, cx + r, cy - r, cx + r, cy + r - (int)(2 * s));
                break;
            case HTMAXBUTTON:
                g.DrawRectangle(pen, cx - r, cy - r, r * 2, r * 2);
                break;
            case HTMINBUTTON:
                g.DrawLine(pen, cx - r, cy, cx + r, cy);
                break;
        }
    }


    private bool _revealed;

    protected override void WndProc(ref Message m)
    {
        if (m.Msg == WM_SHOWWINDOW && m.WParam != IntPtr.Zero && m.LParam == IntPtr.Zero && !_revealed)
        {
            _revealed = true;
            OpenReveal.Begin(_form, keepTransitions: true);
        }
        if (_captionHost != null)
        {
            switch (m.Msg)
            {
                case WM_GETMINMAXINFO:
                {
                    // 枠の無い window は最大化すると task bar まで覆うので、作業領域に収める。
                    base.WndProc(ref m);
                    var screen = Screen.FromHandle(Handle);
                    Rectangle wa = screen.WorkingArea, b = screen.Bounds;
                    var info = Marshal.PtrToStructure<MINMAXINFO>(m.LParam);
                    info.MaxPosX = wa.X - b.X; info.MaxPosY = wa.Y - b.Y;
                    info.MaxSizeX = wa.Width; info.MaxSizeY = wa.Height;
                    Marshal.StructureToPtr(info, m.LParam, false);
                    return;
                }
                case WM_NCLBUTTONDBLCLK when (int)m.WParam == HTCAPTION:
                    // 見出しの二重 click で最大化 / 元に戻す。
                    Click(HTMAXBUTTON);
                    m.Result = IntPtr.Zero;
                    return;
                case WM_NCHITTEST:
                {
                    Point screen = new(unchecked((short)(long)m.LParam), unchecked((short)((long)m.LParam >> 16)));
                    m.Result = new IntPtr(ChromelessHit(screen));
                    return;
                }
                case WM_NCACTIVATE:
                    _active = m.WParam != IntPtr.Zero;
                    m.LParam = new IntPtr(-1);
                    base.WndProc(ref m);
                    _captionHost.Invalidate();
                    return;
                // WM_NCPAINT は Windows に任せる。左右と下の見えない枠は Windows が描くと透明になる (止めると白い帯になる)。
            }
        }
        switch (m.Msg)
        {
            case WM_NCPAINT:
                PaintFrame();
                m.Result = IntPtr.Zero;
                return;
            case WM_NCACTIVATE:
                _active = m.WParam != IntPtr.Zero;
                m.Result = new IntPtr(1);
                PaintFrame();
                return;
            case WM_NCUAHDRAWCAPTION:
            case WM_NCUAHDRAWFRAME:
                // theme 付きの window で Windows が見出しを直接描き直す内部 message。描かせない。
                m.Result = IntPtr.Zero;
                return;
            case WM_SETTEXT:
            case WM_SETICON:
            {
                IntPtr style = GetWindowLongPtr(Handle, GWL_STYLE);
                bool visible = ((long)style & WS_VISIBLE) != 0;
                if (visible) SetWindowLongPtr(Handle, GWL_STYLE, (IntPtr)((long)style & ~WS_VISIBLE));
                try { base.WndProc(ref m); }
                finally { if (visible) SetWindowLongPtr(Handle, GWL_STYLE, style); }
                Transition.After(PaintFrame);
                return;
            }
            case WM_NCHITTEST:
            {
                base.WndProc(ref m);
                long hit = (long)m.Result;
                if (hit is HTCAPTION or HTMINBUTTON or HTMAXBUTTON or HTCLOSE or HTSYSMENU)
                {
                    int b = HitButton(ToWindow(m.LParam));
                    m.Result = new IntPtr(b != 0 ? b : HTCAPTION);
                }
                return;
            }
            case WM_NCMOUSEMOVE:
            {
                int b = (int)m.WParam is HTMINBUTTON or HTMAXBUTTON or HTCLOSE ? (int)m.WParam : 0;
                if (b != _hot) { _hot = b; Repaint(); }
                if (!_tracking)
                {
                    var tme = new TRACKMOUSEEVENT { cbSize = Marshal.SizeOf<TRACKMOUSEEVENT>(), dwFlags = 0x10 | 0x02, hwndTrack = Handle };
                    _tracking = TrackMouseEvent(ref tme);
                }
                if (b == 0) base.WndProc(ref m);
                return;
            }
            case WM_NCMOUSELEAVE:
                _tracking = false;
                if (_hot != 0 || _pressed != 0) { _hot = 0; _pressed = 0; Repaint(); }
                base.WndProc(ref m);
                return;
            case WM_NCLBUTTONDOWN:
            case WM_NCLBUTTONDBLCLK when (int)m.WParam is HTMINBUTTON or HTMAXBUTTON or HTCLOSE:
                if ((int)m.WParam is HTMINBUTTON or HTMAXBUTTON or HTCLOSE)
                {
                    // 既定の処理は古い形の button を描くので、押した状態は自分で持つ。
                    _pressed = (int)m.WParam;
                    Repaint();
                    m.Result = IntPtr.Zero;
                    return;
                }
                break;
            case WM_NCLBUTTONUP:
                if (_pressed != 0)
                {
                    int was = _pressed;
                    _pressed = 0;
                    Repaint();
                    if ((int)m.WParam == was) Click(was);
                    m.Result = IntPtr.Zero;
                    return;
                }
                break;
        }
        base.WndProc(ref m);
    }

    private void Click(int hit)
    {
        switch (hit)
        {
            case HTCLOSE: _form.Close(); break;
            case HTMINBUTTON: SendMessage(_form.Handle, WM_SYSCOMMAND, new IntPtr(SC_MINIMIZE), IntPtr.Zero); break;
            case HTMAXBUTTON:
                SendMessage(_form.Handle, WM_SYSCOMMAND, new IntPtr(_form.WindowState == FormWindowState.Maximized ? SC_RESTORE : SC_MAXIMIZE), IntPtr.Zero);
                break;
        }
    }

    private const int HTCAPTION = 2, HTSYSMENU = 3, HTMINBUTTON = 8, HTMAXBUTTON = 9, HTCLOSE = 20;
    private const int HTCLIENT = 1, HTTRANSPARENT = -1, HTLEFT = 10, HTRIGHT = 11, HTTOP = 12, HTTOPLEFT = 13, HTTOPRIGHT = 14,
                      HTBOTTOM = 15, HTBOTTOMLEFT = 16, HTBOTTOMRIGHT = 17;
}
