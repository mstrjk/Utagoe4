// Help の terminal 表示 (v3 にない)。LogHub の記録を XP 時代の console 風に、今の theme の色で描く。
// 進捗行 (同じ id で上書きされる行) は履歴の下、入力行の上にまとめて出し、本物の terminal のように同じ場所で更新する。
// 文字は Lucida Console (XP の console と同じ書体)。長い行は折り返す。下端にいる間は新しい行に追従する。

using System.Text;
using Utagoe.App;

namespace Utagoe.Ui;

internal sealed class TerminalView : Control
{
    // 色は App.Theme が決める (行の種類ごと)。theme が替わったらすぐ描き直す。

    private readonly VScrollBar _scroll = new() { Dock = DockStyle.Right };
    private readonly System.Windows.Forms.Timer _poll = new() { Interval = 33 };
    private readonly List<(string text, TermStyle style)> _logical = new();
    private readonly List<(string text, TermStyle style)> _rows = new();
    private List<string> _status = new();
    private long _seen = -1;
    private int _entryCount;
    private LogEntry? _first;
    private int _wrapped;
    private int _wrappedCols = -1;
    private int _cols = 80;
    private int _charW = 8, _lineH = 14;
    private bool _follow = true;
    private bool _caretOn = true;
    private int _caretTick;
    private string _input = "";
    private readonly List<string> _history = new();
    private int _historyAt;

    [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
    public string Prompt { get; set; } = "UTAGOE> ";

    [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
    public bool AcceptsInput { get; set; } = true;

    public event Action<string>? Command;

    public TerminalView()
    {
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint |
                 ControlStyles.ResizeRedraw | ControlStyles.Selectable, true);
        TabStop = true;
        BackColor = Theme.Current.TermBack;
        Theme.Changed += OnThemeChanged;
        Utagoe.Ui.DarkScroll.Apply(_scroll, Theme.Current.TermDark);
        Font = PickFont();
        Cursor = Cursors.IBeam;
        Controls.Add(_scroll);
        _scroll.Scroll += (_, _) => { _follow = _scroll.Value >= MaxScroll(); Invalidate(); };
        _poll.Tick += (_, _) => Poll();
        _poll.Start();

        var menu = new ContextMenuStrip();
        string[] labels = { "Copy all", "Copy last error", "Open logs folder", "", "Clear" };
        menu.Items.Add(labels[0], null, (_, _) => CopyAll());
        menu.Items.Add(labels[1], null, (_, _) => CopyLastError());
        menu.Items.Add(labels[2], null, (_, _) => OpenLogs());
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add(labels[4], null, (_, _) => LogHub.Clear());
        menu.Opening += (_, _) =>
        {
            for (int i = 0; i < labels.Length; i++)
                if (labels[i].Length > 0) menu.Items[i].Text = App.L.T(labels[i]);
        };
        ContextMenuStrip = menu;
    }

    private static Font PickFont()
    {
        // Lucida Console は XP 以降の Windows に必ずある。無ければ Consolas。
        using var probe = new Font("Lucida Console", 9.75f);
        return probe.Name == "Lucida Console" ? new Font("Lucida Console", 9.75f) : new Font("Consolas", 10f);
    }

    protected override void OnFontChanged(EventArgs e)
    {
        base.OnFontChanged(e);
        Size s = TextRenderer.MeasureText("MMMMMMMMMM", Font, Size.Empty, TextFormatFlags.NoPadding);
        _charW = Math.Max(1, s.Width / 10);
        _lineH = s.Height + 1;
        Relayout();
    }

    protected override void OnResize(EventArgs e)
    {
        base.OnResize(e);
        Relayout();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing) { _poll.Dispose(); Theme.Changed -= OnThemeChanged; }
        base.Dispose(disposing);
    }

    private void OnThemeChanged()
    {
        BackColor = Theme.Current.TermBack;
        Utagoe.Ui.DarkScroll.Apply(_scroll, Theme.Current.TermDark);
        Invalidate();
    }


    private void Poll()
    {
        // 入力行の caret を 500 ms ごとに点滅させる。caret は focus があるときだけ描くので、そのときだけ入力行だけを描き直す。
        if (++_caretTick % 15 == 0)
        {
            _caretOn = !_caretOn;
            if (AcceptsInput && Focused) Invalidate(PromptRow());
        }
        long v = LogHub.Version;
        if (v == _seen) return;
        _seen = v;
        var (entries, status) = LogHub.Snapshot();
        _status = status;
        // 消去された、または古い記録が切り捨てられたときは最初から作り直す。
        if (entries.Count < _entryCount || (entries.Count > 0 && !ReferenceEquals(entries[0], _first)) ||
            (entries.Count == 0 && _entryCount > 0))
        {
            _logical.Clear();
            _entryCount = 0;
            _wrapped = 0;
            _rows.Clear();
        }
        for (int i = _entryCount; i < entries.Count; i++) AddEntry(entries[i]);
        _entryCount = entries.Count;
        _first = entries.Count > 0 ? entries[0] : null;
        Relayout();
    }

    private void AddEntry(LogEntry e)
    {
        string time = e.Time.ToString("HH:mm:ss") + " ";
        (string prefix, TermStyle color) = e.Kind switch
        {
            LogKind.StageBegin => ("► ", TermStyle.Stage),
            LogKind.StageEnd => e.Id < 0 ? ("× ", TermStyle.Error) : ("√ ", TermStyle.Done),
            LogKind.Warning => ("‼ ", TermStyle.Warn),
            LogKind.Error => ("× ", TermStyle.Error),
            LogKind.Detail => ("  ", TermStyle.Detail),
            LogKind.Command => (Prompt, TermStyle.Prompt),
            _ => ("  ", TermStyle.Text),
        };
        string body = e.Kind == LogKind.StageEnd ? LogHub.StageEndText(e) : e.Text;
        string[] lines = body.Replace("\r", "").Split('\n');
        for (int i = 0; i < lines.Length; i++)
        {
            // stack trace など 2 行目以降は字下げして色を落とす。
            if (i == 0) _logical.Add((time + prefix + lines[i], color));
            else if (lines[i].Length > 0)
                _logical.Add((new string(' ', time.Length + 2) + lines[i], e.Kind == LogKind.Error ? TermStyle.Trace : color));
        }
    }


    private void Relayout()
    {
        int width = ClientSize.Width - _scroll.Width - 12;
        _cols = Math.Max(20, width / Math.Max(1, _charW));
        // 幅が変わったときだけ全部折り返し直す。それ以外は増えた行だけ。
        if (_cols != _wrappedCols)
        {
            _rows.Clear();
            _wrapped = 0;
            _wrappedCols = _cols;
        }
        for (; _wrapped < _logical.Count; _wrapped++) Wrap(_logical[_wrapped].text, _logical[_wrapped].style, _rows);
        int visible = Math.Max(1, (ClientSize.Height - 8) / _lineH);
        int total = TotalRows();
        _scroll.Maximum = Math.Max(0, total - 1);
        _scroll.LargeChange = visible;
        _scroll.SmallChange = 1;
        if (_follow) _scroll.Value = MaxScroll();
        else _scroll.Value = Math.Min(_scroll.Value, MaxScroll());
        Invalidate();
    }

    private void Wrap(string text, TermStyle color, List<(string, TermStyle)> into)
    {
        if (text.Length <= _cols)
        {
            into.Add((text, color));
            return;
        }
        // 折り返した続きは、時刻の後ろの本文の字下げにそろえる。
        int start = text.Length > 9 && text[2] == ':' && text[5] == ':' ? 9 : 0;
        while (start < text.Length && text[start] == ' ') start++;
        int indent = Math.Min(start + 2, _cols / 2);
        into.Add((text[.._cols], color));
        string rest = text[_cols..];
        while (rest.Length > 0)
        {
            int take = Math.Min(_cols - indent, rest.Length);
            into.Add((new string(' ', indent) + rest[..take], color));
            rest = rest[take..];
        }
    }

    private Rectangle PromptRow()
    {
        int index = _rows.Count + StatusRows().Count - _scroll.Value;
        int y = 4 + index * _lineH;
        return y < 0 || y >= ClientSize.Height ? Rectangle.Empty : new Rectangle(0, y, ClientSize.Width, _lineH);
    }

    private List<(string, TermStyle)> StatusRows()
    {
        var rows = new List<(string, TermStyle)>();
        foreach (string s in _status) Wrap(Blocks(s), TermStyle.Status, rows);
        return rows;
    }

    // core の進捗棒 "[####....]" を塗りつぶしの block 文字で描く。
    private static string Blocks(string s)
    {
        int a = s.IndexOf('['), b = a >= 0 ? s.IndexOf(']', a) : -1;
        if (a < 0 || b < 0) return s;
        string inner = s.Substring(a + 1, b - a - 1);
        if (inner.Any(ch => ch != '#' && ch != '.')) return s;
        return s[..(a + 1)] + inner.Replace('#', '█').Replace('.', '░') + s[b..];
    }

    private int TotalRows() => _rows.Count + StatusRows().Count + (AcceptsInput ? 1 : 0);

    private int MaxScroll()
    {
        int visible = Math.Max(1, (ClientSize.Height - 8) / _lineH);
        return Math.Max(0, TotalRows() - visible);
    }

    protected override void OnMouseWheel(MouseEventArgs e)
    {
        int v = Math.Clamp(_scroll.Value - Math.Sign(e.Delta) * 3, 0, MaxScroll());
        _scroll.Value = v;
        _follow = v >= MaxScroll();
        Invalidate();
    }


    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        var theme = Theme.Current;
        g.Clear(theme.TermBack);
        var all = new List<(string text, TermStyle style)>(_rows);
        all.AddRange(StatusRows());
        if (AcceptsInput)
        {
            string caret = Focused && _caretOn ? "█" : " ";
            all.Add((Prompt + _input + caret, TermStyle.Prompt));
        }
        int first = _scroll.Value;
        int y = 4;
        for (int i = first; i < all.Count && y < ClientSize.Height; i++, y += _lineH)
            TextRenderer.DrawText(g, all[i].text, Font, new Point(6, y), theme.Term(all[i].style), TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix);
    }


    protected override bool IsInputKey(Keys keyData) =>
        keyData is Keys.Up or Keys.Down or Keys.Enter or Keys.Back || base.IsInputKey(keyData);

    protected override void OnKeyPress(KeyPressEventArgs e)
    {
        if (!AcceptsInput || char.IsControl(e.KeyChar)) return;
        _input += e.KeyChar;
        FollowAndRedraw();
        e.Handled = true;
    }

    protected override void OnKeyDown(KeyEventArgs e)
    {
        if (e.Control && e.KeyCode == Keys.C) { CopyAll(); e.Handled = true; return; }
        if (e.Control && e.KeyCode == Keys.L) { LogHub.Clear(); e.Handled = true; return; }
        if (!AcceptsInput) return;
        switch (e.KeyCode)
        {
            case Keys.Back:
                if (_input.Length > 0) _input = _input[..^1];
                break;
            case Keys.Enter:
                string cmd = _input.Trim();
                _input = "";
                if (cmd.Length > 0)
                {
                    _history.Add(cmd);
                    _historyAt = _history.Count;
                    LogHub.Add(LogKind.Command, 0, cmd);
                    Command?.Invoke(cmd);
                }
                break;
            case Keys.Up:
                if (_historyAt > 0) _input = _history[--_historyAt];
                break;
            case Keys.Down:
                if (_historyAt < _history.Count - 1) _input = _history[++_historyAt];
                else { _historyAt = _history.Count; _input = ""; }
                break;
            default:
                return;
        }
        e.Handled = true;
        FollowAndRedraw();
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        base.OnMouseDown(e);
        Focus();
    }

    protected override void OnGotFocus(EventArgs e) { base.OnGotFocus(e); Invalidate(); }
    protected override void OnLostFocus(EventArgs e) { base.OnLostFocus(e); Invalidate(); }

    private void FollowAndRedraw()
    {
        _caretOn = true;
        _follow = true;
        Relayout();
    }


    public static string AllText()
    {
        var sb = new StringBuilder();
        foreach (var e in LogHub.Snapshot().entries) sb.AppendLine(LogHub.Format(e));
        return sb.ToString();
    }

    public static void CopyAll()
    {
        string text = AllText();
        if (text.Length > 0) Clipboard.SetText(text);
    }

    public static void CopyLastError()
    {
        var last = LogHub.Snapshot().entries.LastOrDefault(e => e.Kind == LogKind.Error);
        if (last != null) Clipboard.SetText(LogHub.Format(last));
    }

    public static void OpenLogs()
    {
        try
        {
            Directory.CreateDirectory(AppPaths.Logs);
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo("explorer.exe", $"\"{AppPaths.Logs}\"") { UseShellExecute = true });
        }
        catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or IOException)
        {
            LogHub.Exception("cannot open the logs folder", ex);
        }
    }
}
