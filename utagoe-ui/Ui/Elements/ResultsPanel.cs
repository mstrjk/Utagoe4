using Utagoe.App;

namespace Utagoe.Ui;

internal sealed class ResultsPanel : Control
{
    private readonly ListBox _list;
    private readonly MediaPlayerStrip _player;
    private readonly VclTrackBar _seek;
    private readonly Label _time;
    private readonly PlayerLink _link;
    private MciPlayer _mci => _link.Mci;

    public ResultsPanel()
    {
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
        _list = new ListBox
        {
            BorderStyle = BorderStyle.None,
            DrawMode = DrawMode.OwnerDrawFixed,
            IntegralHeight = false,
        };
        _list.DrawItem += DrawItem;
        _list.DoubleClick += (_, _) => PlaySelected();
        _player = new MediaPlayerStrip("MPPLAY", "MPPAUSE", "MPSTOP", "MPPREV");
        _player.ButtonClick += OnPlayerButton;
        _seek = new VclTrackBar { TickStyle = TickStyle.None, Minimum = 0, Maximum = 1 };
        _time = new ThemedLabel { AutoSize = false, TextAlign = ContentAlignment.MiddleRight };
        _link = new PlayerLink(_seek, 250);
        _link.Synced += ShowTime;
        _mci.Released += Release;
        Controls.AddRange(new Control[] { _list, _player, _seek, _time });
        SetFiles(Array.Empty<string>());
    }

    public void SetFiles(IEnumerable<string> files)
    {
        _link.StopTimer();
        _mci.Close();
        _list.BeginUpdate();
        _list.Items.Clear();
        foreach (string f in files) _list.Items.Add(f);
        _list.EndUpdate();
        if (_list.Items.Count > 0) _list.SelectedIndex = 0;
        bool any = _list.Items.Count > 0;
        _list.Visible = any;
        _player.Enabled = _seek.Enabled = any;
        _link.Quietly(() =>
        {
            _seek.Maximum = 1;
            _seek.Value = 0;
        });
        _time.Text = "";
        Invalidate();
    }

    public void Release()
    {
        _link.StopTimer();
        _mci.Close();
        _link.Quietly(() => _seek.Value = 0);
        _time.Text = "";
        _list.Invalidate();
    }

    private string? Selected => _list.SelectedItem as string;

    private bool EnsureLoaded()
    {
        string? path = Selected;
        if (path == null) return false;
        if (_mci.IsOpen && _mci.Path == path) return true;
        if (!_mci.Load(path)) return false;
        _link.Quietly(() =>
        {
            _seek.Maximum = Math.Max(1, _mci.Length);
            _seek.LargeChange = Math.Max(1, _seek.Maximum / 10);
            _seek.SmallChange = Math.Max(1, _seek.Maximum / 100);
            _seek.Value = 0;
        });
        _link.StartTimer();
        _list.Invalidate();
        return true;
    }

    private void PlaySelected()
    {
        if (!EnsureLoaded()) return;
        _mci.Rewind();
        _mci.Play();
        _link.Sync();
    }

    private void OnPlayerButton(int button)
    {
        if (button == PlayerLink.Play)
        {
            if (EnsureLoaded()) _mci.Play();
        }
        else if (_mci.IsOpen) _link.Press(button);
        _link.Sync();
    }

    private void ShowTime() => _time.Text = _mci.IsOpen ? $"{Clock(_mci.Position)} / {Clock(_mci.Length)}" : "";

    private static string Clock(int ms)
    {
        var t = TimeSpan.FromMilliseconds(Math.Max(0, ms));
        return t.TotalHours >= 1 ? t.ToString(@"h\:mm\:ss") : t.ToString(@"m\:ss");
    }

    protected override void OnFontChanged(EventArgs e)
    {
        base.OnFontChanged(e);
        foreach (Control c in Controls) c.Font = Font;
        _list.ItemHeight = Font.Height + 4;
        PerformLayout();
    }

    protected override void OnLayout(LayoutEventArgs e)
    {
        base.OnLayout(e);
        float k = Font.Height / 14f;
        int stripW = (int)Math.Round(97 * k), stripH = (int)Math.Round(30 * k);
        int timeW = (int)Math.Round(100 * k), gap = (int)Math.Round(6 * k);
        int rowTop = Math.Max(0, Height - stripH);
        _player.Bounds = new Rectangle(0, rowTop, stripW, stripH);
        _time.Bounds = new Rectangle(Width - timeW, rowTop, timeW, stripH);
        _seek.Bounds = new Rectangle(stripW + gap, rowTop, Math.Max(10, Width - stripW - timeW - gap * 2), stripH);
        _list.Bounds = new Rectangle(1, 1, Math.Max(0, Width - 2), Math.Max(0, rowTop - gap - 2));
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var t = Theme.Current;
        var g = e.Graphics;
        using (var back = new SolidBrush(Parent?.BackColor ?? t.Window)) g.FillRectangle(back, ClientRectangle);
        var box = new Rectangle(0, 0, Width - 1, Math.Max(0, _list.Bottom));
        using (var input = new SolidBrush(t.Input)) g.FillRectangle(input, box);
        using (var pen = new Pen(t.Line)) g.DrawRectangle(pen, box);
        _list.BackColor = t.Input;
        _list.ForeColor = t.InputText;
        if (_list.Items.Count == 0)
            TextRenderer.DrawText(g, L.T("Files from the last run show up here."), Font, Rectangle.Inflate(box, -6, 0), t.Muted,
                                  TextFormatFlags.VerticalCenter | TextFormatFlags.Left | TextFormatFlags.EndEllipsis);
    }

    private void DrawItem(object? sender, DrawItemEventArgs e)
    {
        if (e.Index < 0) return;
        var t = Theme.Current;
        bool selected = (e.State & DrawItemState.Selected) != 0;
        bool playing = _mci.IsOpen && _mci.Path == (string)_list.Items[e.Index];
        using (var back = new SolidBrush(selected ? Theme.Blend(t.Input, t.Progress, 0.35) : t.Input)) e.Graphics.FillRectangle(back, e.Bounds);
        string name = Path.GetFileName((string)_list.Items[e.Index]);
        var text = Rectangle.Inflate(e.Bounds, -6, 0);
        TextRenderer.DrawText(e.Graphics, (playing ? "▶ " : "") + name, e.Font ?? Font, text, t.InputText,
                              TextFormatFlags.VerticalCenter | TextFormatFlags.Left | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPrefix);
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _link.Dispose();
        }
        base.Dispose(disposing);
    }
}
