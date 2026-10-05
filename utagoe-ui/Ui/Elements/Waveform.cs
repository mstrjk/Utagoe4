using System.Globalization;
using Utagoe.App;

namespace Utagoe.Ui;

internal sealed class WaveTrack
{
    public const int Block = 256;
    public int Rate;
    public short[] Mono = Array.Empty<short>();
    public short[] BlockMin = Array.Empty<short>();
    public short[] BlockMax = Array.Empty<short>();
    public double Duration => Rate > 0 ? Mono.Length / (double)Rate : 0;

    public static WaveTrack Read(string wavPath)
    {
        byte[] all = File.ReadAllBytes(wavPath);
        if (all.Length < 44 || BitConverter.ToInt32(all, 0) != 0x46464952 || BitConverter.ToInt32(all, 8) != 0x45564157)
            throw new InvalidDataException("not a WAV file");
        int pos = 12, channels = 0, rate = 0, bits = 0, dataAt = -1, dataLen = 0;
        while (pos + 8 <= all.Length)
        {
            int id = BitConverter.ToInt32(all, pos), len = BitConverter.ToInt32(all, pos + 4);
            if (id == 0x20746D66)
            {
                channels = BitConverter.ToInt16(all, pos + 10);
                rate = BitConverter.ToInt32(all, pos + 12);
                bits = BitConverter.ToInt16(all, pos + 22);
            }
            else if (id == 0x61746164)
            {
                dataAt = pos + 8;
                dataLen = Math.Min(len, all.Length - dataAt);
                break;
            }
            pos += 8 + len + (len & 1);
        }
        if (dataAt < 0 || channels < 1 || rate <= 0 || bits != 16) throw new InvalidDataException("unexpected WAV layout");
        int frames = dataLen / (2 * channels);
        var t = new WaveTrack { Rate = rate, Mono = new short[frames] };
        for (int f = 0; f < frames; f++)
        {
            int sum = 0, at = dataAt + f * 2 * channels;
            for (int c = 0; c < channels; c++) sum += BitConverter.ToInt16(all, at + 2 * c);
            t.Mono[f] = (short)(sum / channels);
        }
        int blocks = (frames + Block - 1) / Block;
        t.BlockMin = new short[blocks];
        t.BlockMax = new short[blocks];
        for (int b = 0; b < blocks; b++)
        {
            short lo = short.MaxValue, hi = short.MinValue;
            for (int i = b * Block, e = Math.Min(frames, i + Block); i < e; i++)
            {
                short v = t.Mono[i];
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
            t.BlockMin[b] = lo;
            t.BlockMax[b] = hi;
        }
        return t;
    }

    public (float Min, float Max) Range(double t0, double t1)
    {
        if (Mono.Length == 0) return (0, 0);
        long a = Math.Clamp((long)(t0 * Rate), 0, Mono.Length - 1), e = Math.Clamp((long)Math.Ceiling(t1 * Rate), a + 1, Mono.Length);
        int lo = short.MaxValue, hi = short.MinValue;
        if (e - a > 4 * Block)
        {
            for (long b = a / Block, be = (e + Block - 1) / Block; b < be; b++)
            {
                if (BlockMin[b] < lo) lo = BlockMin[b];
                if (BlockMax[b] > hi) hi = BlockMax[b];
            }
        }
        else
        {
            for (long i = a; i < e; i++)
            {
                if (Mono[i] < lo) lo = Mono[i];
                if (Mono[i] > hi) hi = Mono[i];
            }
        }
        return (lo / 32768f, hi / 32768f);
    }
}

internal sealed class SpanState
{
    public WaveTrack? Track;
    public Func<string>? Message;
    public double ViewStart, ViewLength = 1;
    public readonly List<(double A, double B)> Spans = new();
    public int Selected = -1;
    public double Playhead;
    public event Action? Changed;
    public event Action? SpansEdited;

    public double Duration => Track?.Duration ?? 0;

    public void Notify() => Changed?.Invoke();

    public void Edited()
    {
        Normalize();
        SpansEdited?.Invoke();
        Notify();
    }

    public void SetView(double start, double length)
    {
        double d = Duration;
        if (d <= 0) return;
        ViewLength = Math.Clamp(length, Math.Min(d, 0.5), d);
        ViewStart = Math.Clamp(start, 0, d - ViewLength);
        Notify();
    }

    public void ZoomAround(double t, double factor) =>
        SetView(t - (t - ViewStart) * factor, ViewLength * factor);

    public int Hit(double t)
    {
        for (int i = 0; i < Spans.Count; i++)
            if (t >= Spans[i].A && t <= Spans[i].B) return i;
        return -1;
    }

    private void Normalize()
    {
        (double, double)? keep = Selected >= 0 && Selected < Spans.Count ? Spans[Selected] : null;
        var s = Spans.Where(x => x.B - x.A >= 0.05).Select(x => (A: Math.Max(0, x.A), B: Math.Min(Duration > 0 ? Duration : x.B, x.B)))
                     .OrderBy(x => x.A).ToList();
        Spans.Clear();
        foreach (var x in s)
        {
            if (Spans.Count > 0 && x.A <= Spans[^1].B) Spans[^1] = (Spans[^1].A, Math.Max(Spans[^1].B, x.B));
            else Spans.Add(x);
        }
        Selected = keep is { } k ? Spans.FindIndex(x => x.A <= k.Item1 + 1e-9 && x.B >= k.Item2 - 1e-9) : -1;
    }

    public static string Format(double t)
    {
        t = Math.Round(t, 1);
        int h = (int)(t / 3600), m = (int)(t % 3600 / 60);
        double s = t - h * 3600 - m * 60;
        string sec = s.ToString(h > 0 || m > 0 ? "00.0" : "0.0", CultureInfo.InvariantCulture);
        if (sec.EndsWith(".0", StringComparison.Ordinal)) sec = sec[..^2];
        return h > 0 ? $"{h}:{m:00}:{sec}" : m > 0 ? $"{m}:{sec}" : sec;
    }

    public string ToText() => string.Join(", ", Spans.Select(x => Format(x.A) + "-" + Format(x.B)));

    public static List<(double, double)> Parse(string text)
    {
        var list = new List<(double, double)>();
        foreach (string raw in text.Split(',', ';'))
        {
            string item = raw.Trim();
            int dash = item.IndexOf('-');
            if (dash <= 0) continue;
            if (ParseTime(item[..dash].Trim(), out double a) && ParseTime(item[(dash + 1)..].Trim(), out double b) && b > a)
                list.Add((a, b));
        }
        return list;
    }

    private static bool ParseTime(string s, out double t)
    {
        t = 0;
        string[] parts = s.Split(':');
        if (parts.Length is < 1 or > 3) return false;
        foreach (string p in parts)
        {
            if (!double.TryParse(p, NumberStyles.Float, CultureInfo.InvariantCulture, out double v) || v < 0) return false;
            t = t * 60 + v;
        }
        return true;
    }
}

internal abstract class WaveSurface : Control
{
    protected readonly SpanState S;

    protected WaveSurface(SpanState state)
    {
        S = state;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.ResizeRedraw | ControlStyles.Selectable, true);
        S.Changed += Invalidate;
        Theme.Changed += Invalidate;
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            S.Changed -= Invalidate;
            Theme.Changed -= Invalidate;
        }
        base.Dispose(disposing);
    }

    protected static Theme T => Theme.Current;
    protected static Color Wave => T.Progress;
    protected static Color SpanFill => Color.FromArgb(T.Dark ? 70 : 60, T.Text);
    protected static Color SpanSelected => Color.FromArgb(T.Dark ? 120 : 100, T.Text);

    protected void DrawWave(Graphics g, Rectangle area, double t0, double t1)
    {
        var track = S.Track;
        if (track == null || area.Width <= 0) return;
        int mid = area.Top + area.Height / 2;
        float half = area.Height / 2f - 1;
        using var pen = new Pen(Wave);
        using var axis = new Pen(Theme.Blend(T.Input, T.Text, 0.15));
        g.DrawLine(axis, area.Left, mid, area.Right, mid);
        double per = (t1 - t0) / area.Width;
        for (int x = 0; x < area.Width; x++)
        {
            var (lo, hi) = track.Range(t0 + x * per, t0 + (x + 1) * per);
            int y0 = mid - (int)Math.Round(hi * half), y1 = mid - (int)Math.Round(lo * half);
            g.DrawLine(pen, area.Left + x, y0, area.Left + x, Math.Max(y0 + 1, y1));
        }
    }

    protected void DrawSpans(Graphics g, Rectangle area, double t0, double t1, bool edges)
    {
        if (t1 <= t0) return;
        using var fill = new SolidBrush(SpanFill);
        using var sel = new SolidBrush(SpanSelected);
        using var edge = new Pen(T.Text, 1);
        for (int i = 0; i < S.Spans.Count; i++)
        {
            var (a, b) = S.Spans[i];
            if (b < t0 || a > t1) continue;
            int x0 = area.Left + (int)Math.Round((a - t0) / (t1 - t0) * area.Width);
            int x1 = area.Left + (int)Math.Round((b - t0) / (t1 - t0) * area.Width);
            var r = Rectangle.FromLTRB(Math.Max(area.Left, x0), area.Top, Math.Min(area.Right, Math.Max(x0 + 1, x1)), area.Bottom);
            g.FillRectangle(i == S.Selected ? sel : fill, r);
            if (edges)
            {
                if (x0 >= area.Left) g.DrawLine(edge, x0, area.Top, x0, area.Bottom - 1);
                if (x1 <= area.Right) g.DrawLine(edge, x1, area.Top, x1, area.Bottom - 1);
            }
        }
    }

    protected void DrawPlayhead(Graphics g, Rectangle area, double t0, double t1)
    {
        if (S.Track == null || t1 <= t0 || S.Playhead < t0 || S.Playhead > t1) return;
        int x = area.Left + (int)Math.Round((S.Playhead - t0) / (t1 - t0) * area.Width);
        using var pen = new Pen(T.Text, 1);
        g.DrawLine(pen, x, area.Top, x, area.Bottom - 1);
    }

    protected void DrawMessage(Graphics g)
    {
        if (S.Message == null) return;
        TextRenderer.DrawText(g, S.Message(), Font, ClientRectangle, T.Muted,
            TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.WordBreak);
    }
}

internal sealed class WaveOverview : WaveSurface
{
    private double _grab = -1;

    public WaveOverview(SpanState state) : base(state) { Cursor = Cursors.Hand; }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(T.Input);
        var area = new Rectangle(0, 0, Width, Height);
        double d = S.Duration;
        if (S.Track != null && d > 0)
        {
            DrawWave(g, area, 0, d);
            DrawSpans(g, area, 0, d, false);
            int x0 = (int)Math.Round(S.ViewStart / d * Width), x1 = (int)Math.Round((S.ViewStart + S.ViewLength) / d * Width);
            var view = Rectangle.FromLTRB(x0, 0, Math.Max(x0 + 2, x1), Height - 1);
            using var shade = new SolidBrush(Color.FromArgb(40, T.Text));
            g.FillRectangle(shade, view);
            using var frame = new Pen(T.Progress, 2);
            g.DrawRectangle(frame, view);
            DrawPlayhead(g, area, 0, d);
        }
        using var border = new Pen(T.Line);
        g.DrawRectangle(border, 0, 0, Width - 1, Height - 1);
        DrawMessage(g);
    }

    private double TimeAt(int x) => Math.Clamp(x / (double)Math.Max(1, Width), 0, 1) * S.Duration;

    protected override void OnMouseDown(MouseEventArgs e)
    {
        base.OnMouseDown(e);
        if (S.Track == null || e.Button != MouseButtons.Left) return;
        double t = TimeAt(e.X);
        if (t < S.ViewStart || t > S.ViewStart + S.ViewLength) S.SetView(t - S.ViewLength / 2, S.ViewLength);
        _grab = t - S.ViewStart;
        Capture = true;
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        base.OnMouseMove(e);
        if (_grab >= 0) S.SetView(TimeAt(e.X) - _grab, S.ViewLength);
    }

    protected override void OnMouseUp(MouseEventArgs e)
    {
        base.OnMouseUp(e);
        _grab = -1;
        Capture = false;
    }

    protected override void OnMouseWheel(MouseEventArgs e)
    {
        base.OnMouseWheel(e);
        if (S.Track != null) S.ZoomAround(TimeAt(e.X), e.Delta > 0 ? 0.8 : 1.25);
    }
}

internal sealed class WaveDetail : WaveSurface
{
    private enum Drag { None, Create, Left, Right, Move }

    private Drag _drag;
    private int _index = -1;
    private double _anchor, _offset;
    private Point _down;
    private bool _moved;
    private readonly ContextMenuStrip _menu = new();
    private int _menuIndex = -1;

    public event Action<double>? Seek;
    public event Action<int>? PlaySpan;

    public WaveDetail(SpanState state) : base(state)
    {
        _menu.Items.Add("Play Selection", null, (_, _) => { if (_menuIndex >= 0) PlaySpan?.Invoke(_menuIndex); });
        _menu.Items.Add("Remove", null, (_, _) => Remove(_menuIndex));
        _menu.Opening += (_, _) =>
        {
            _menu.Items[0].Text = L.T("Play Selection");
            _menu.Items[1].Text = L.T("Remove");
        };
    }

    private int Ruler => LogicalToDeviceUnits(18);
    private Rectangle WaveArea => new(0, Ruler, Width, Math.Max(1, Height - Ruler));
    private double T0 => S.ViewStart;
    private double T1 => S.ViewStart + S.ViewLength;
    private double TimeAt(int x) => T0 + Math.Clamp(x, 0, Width) / (double)Math.Max(1, Width) * S.ViewLength;
    private int XAt(double t) => (int)Math.Round((t - T0) / Math.Max(1e-9, S.ViewLength) * Width);

    public void Remove(int i)
    {
        if (i < 0 || i >= S.Spans.Count) return;
        S.Spans.RemoveAt(i);
        S.Selected = -1;
        S.Edited();
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(T.Input);
        var area = WaveArea;
        if (S.Track != null)
        {
            DrawRuler(g);
            DrawWave(g, area, T0, T1);
            DrawSpans(g, area, T0, T1, true);
            DrawPlayhead(g, new Rectangle(0, 0, Width, Height), T0, T1);
        }
        using var border = new Pen(T.Line);
        g.DrawRectangle(border, 0, 0, Width - 1, Height - 1);
        DrawMessage(g);
    }

    private void DrawRuler(Graphics g)
    {
        using var back = new SolidBrush(Theme.Blend(T.Input, T.Window, 0.5));
        g.FillRectangle(back, 0, 0, Width, Ruler);
        double[] steps = { 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600 };
        double want = S.ViewLength * LogicalToDeviceUnits(80) / Math.Max(1, Width);
        double step = steps.FirstOrDefault(s => s >= want, 600);
        using var tick = new Pen(T.Muted);
        for (double t = Math.Ceiling(T0 / step) * step; t <= T1; t += step)
        {
            int x = XAt(t);
            g.DrawLine(tick, x, Ruler - LogicalToDeviceUnits(5), x, Ruler - 1);
            TextRenderer.DrawText(g, SpanState.Format(t), Font, new Point(x + 2, 1), T.Muted, TextFormatFlags.NoPadding);
        }
    }

    private (Drag, int) HitTest(int x)
    {
        int tol = LogicalToDeviceUnits(4);
        for (int i = 0; i < S.Spans.Count; i++)
        {
            int a = XAt(S.Spans[i].A), b = XAt(S.Spans[i].B);
            if (Math.Abs(x - a) <= tol) return (Drag.Left, i);
            if (Math.Abs(x - b) <= tol) return (Drag.Right, i);
        }
        int hit = S.Hit(TimeAt(x));
        return hit >= 0 ? (Drag.Move, hit) : (Drag.Create, -1);
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        base.OnMouseDown(e);
        Focus();
        if (S.Track == null) return;
        if (e.Button == MouseButtons.Right)
        {
            _menuIndex = S.Hit(TimeAt(e.X));
            if (_menuIndex < 0) return;
            S.Selected = _menuIndex;
            S.Notify();
            _menu.Show(this, e.Location);
            return;
        }
        if (e.Button != MouseButtons.Left) return;
        (_drag, _index) = HitTest(e.X);
        _down = e.Location;
        _moved = false;
        _anchor = TimeAt(e.X);
        if (_drag == Drag.Move) _offset = _anchor - S.Spans[_index].A;
        Capture = true;
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        base.OnMouseMove(e);
        if (S.Track == null) return;
        if (_drag == Drag.None)
        {
            Cursor = HitTest(e.X).Item1 switch { Drag.Left or Drag.Right => Cursors.SizeWE, Drag.Move => Cursors.Hand, _ => Cursors.IBeam };
            return;
        }
        if (!_moved && Math.Abs(e.X - _down.X) < LogicalToDeviceUnits(3)) return;
        if (!_moved && _drag == Drag.Create)
        {
            S.Spans.Add((_anchor, _anchor));
            _index = S.Spans.Count - 1;
            S.Selected = _index;
        }
        _moved = true;
        double t = Math.Clamp(TimeAt(e.X), 0, S.Duration);
        var (a, b) = S.Spans[_index];
        S.Spans[_index] = _drag switch
        {
            Drag.Create => (Math.Min(_anchor, t), Math.Max(_anchor, t)),
            Drag.Left => (Math.Min(t, b - 0.05), b),
            Drag.Right => (a, Math.Max(t, a + 0.05)),
            _ => Shift(a, b, t - _offset),
        };
        if (e.X < 0) S.SetView(S.ViewStart - S.ViewLength * 0.02, S.ViewLength);
        else if (e.X > Width) S.SetView(S.ViewStart + S.ViewLength * 0.02, S.ViewLength);
        S.Notify();
    }

    private (double, double) Shift(double a, double b, double start)
    {
        double len = b - a, s = Math.Clamp(start, 0, Math.Max(0, S.Duration - len));
        return (s, s + len);
    }

    protected override void OnMouseUp(MouseEventArgs e)
    {
        base.OnMouseUp(e);
        if (_drag == Drag.None) return;
        Capture = false;
        if (_moved)
        {
            S.Selected = _index;
            S.Edited();
        }
        else
        {
            double t = TimeAt(e.X);
            S.Selected = S.Hit(t);
            S.Playhead = t;
            S.Notify();
            Seek?.Invoke(t);
        }
        _drag = Drag.None;
    }

    protected override void OnMouseWheel(MouseEventArgs e)
    {
        base.OnMouseWheel(e);
        if (S.Track == null) return;
        if ((ModifierKeys & Keys.Control) != 0) S.ZoomAround(TimeAt(e.X), e.Delta > 0 ? 0.8 : 1.25);
        else S.SetView(S.ViewStart - Math.Sign(e.Delta) * S.ViewLength * 0.1, S.ViewLength);
    }

    protected override bool IsInputKey(Keys keyData) => keyData is Keys.Delete or Keys.Left or Keys.Right || base.IsInputKey(keyData);

    protected override void OnKeyDown(KeyEventArgs e)
    {
        base.OnKeyDown(e);
        if (e.KeyCode == Keys.Delete) Remove(S.Selected);
        else if (e.KeyCode == Keys.Left) S.SetView(S.ViewStart - S.ViewLength * 0.1, S.ViewLength);
        else if (e.KeyCode == Keys.Right) S.SetView(S.ViewStart + S.ViewLength * 0.1, S.ViewLength);
    }
}
