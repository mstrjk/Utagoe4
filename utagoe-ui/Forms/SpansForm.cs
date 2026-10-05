using Utagoe.App;
using Utagoe.Native;
using Utagoe.Ui;

namespace Utagoe.Forms;

internal sealed class SpansForm : UiWindow
{
    private static WindowSpec Definition(string source) => new()
    {
        ClientSize = new Size(900, 360),
        MinimumSize = new Size(560, 300),
        Border = Border.Sizable,
        Buttons = TitleButtons.All,
        FontSize = 8.25F,
        Scaling = Scaling.Dpi,
        KeyPreview = true,
        Title = () => L.T("Vocal-Free Passages") + (source.Length > 0 ? " - " + Path.GetFileName(source) : ""),
    };

    private readonly string _source;
    private readonly Action<string> _apply;
    private readonly SpanState _state = new();
    private readonly WaveOverview _overview;
    private readonly WaveDetail _detail;
    private readonly HScrollBar _scroll = Element.HScroll(DockStyle.Bottom);
    private readonly BitBtn _play, _stop, _playSpan, _zoomIn, _zoomOut, _whole, _remove, _clear, _ok;
    private readonly ThemedLabel _time = Element.Status(new Size(150, 25), new Padding(10, 3, 3, 3));
    private readonly ThemedLabel _hint;
    private readonly System.Windows.Forms.Timer _timer = new() { Interval = 30 };
    private readonly MciPlayer _mci = new();
    private string? _wav;
    private double _stopAt = -1;
    private bool _syncingScroll;

    public SpansForm(string source, string spansText, Action<string> apply) : base(Definition(source))
    {
        _source = source;
        _apply = apply;
        foreach (var (a, b) in SpanState.Parse(spansText)) _state.Spans.Add((a, b));

        _overview = Element.Overview(_state, 56);
        _detail = Element.Detail(_state);
        _hint = Element.Note(() => L.T("Drag across the waveform to mark passages with no vocal. Drag an edge to adjust it, right-click to remove it, Ctrl + wheel to zoom."), DockStyle.Top, 34);

        var bar = Element.Row(DockStyle.Bottom, 38, new Padding(0, 6, 0, 0));
        _play = Element.RowButton(PlayText, 80);
        _stop = Element.RowButton(() => L.T("Stop"), 64);
        _playSpan = Element.RowButton(() => L.T("Play Selection"), 128);
        _zoomOut = Element.RowButton(() => "-", 30);
        _zoomIn = Element.RowButton(() => "+", 30);
        _whole = Element.RowButton(() => L.T("Whole Song"), 112);
        _remove = Element.RowButton(() => L.T("Remove"), 84);
        _clear = Element.RowButton(() => L.T("Clear"), 64);
        bar.Controls.AddRange(new Control[] { _play, _stop, _playSpan, _zoomOut, _zoomIn, _whole, _remove, _clear, _time });
        _ok = Element.Ok(new Rectangle(0, 0, 75, 25), 0, closes: true);
        _ok.Dock = DockStyle.Right;
        _ok.Click += (_, _) => Close();
        var okHost = Element.Area(DockStyle.Right, 79, new Padding(0, 6, 4, 7));
        okHost.Controls.Add(_ok);
        var bottom = Element.Area(DockStyle.Bottom, 38);
        bottom.Controls.Add(bar);
        bottom.Controls.Add(okHost);
        bar.Dock = DockStyle.Fill;

        var gap = Element.Area(DockStyle.Top, 6);
        var body = Element.Area(DockStyle.Fill, 0, new Padding(8, 4, 8, 4));
        body.Controls.Add(_detail);
        body.Controls.Add(_scroll);
        body.Controls.Add(gap);
        body.Controls.Add(_overview);
        body.Controls.Add(_hint);
        Controls.Add(body);
        Controls.Add(bottom);

        _play.Click += (_, _) => TogglePlay();
        _stop.Click += (_, _) => StopPlayback(true);
        _playSpan.Click += (_, _) => PlaySpan(_state.Selected);
        _zoomIn.Click += (_, _) => _state.ZoomAround(Center, 0.5);
        _zoomOut.Click += (_, _) => _state.ZoomAround(Center, 2.0);
        _whole.Click += (_, _) => _state.SetView(0, _state.Duration);
        _remove.Click += (_, _) => _detail.Remove(_state.Selected);
        _clear.Click += (_, _) =>
        {
            _state.Spans.Clear();
            _state.Selected = -1;
            _state.Edited();
        };
        _detail.Seek += t => { _stopAt = -1; if (_mci.IsOpen) SeekPlayback(t); };
        _detail.PlaySpan += PlaySpan;
        _state.SpansEdited += () => _apply(_state.ToText());
        _state.Changed += SyncControls;
        _scroll.ValueChanged += (_, _) =>
        {
            if (_syncingScroll || _state.Duration <= 0) return;
            _state.SetView(_scroll.Value / 1000.0 * _state.Duration, _state.ViewLength);
        };
        _timer.Tick += (_, _) => Tick();

        _state.Message = source.Length == 0 ? () => L.T("Choose the original file in the main window first.") : () => L.T("Loading the original...");
        UiText.OnChange(this, _state.Notify);
        SyncControls();
        Ready();
        Theme.Changed += Repaint;
    }

    private double Center => _state.ViewStart + _state.ViewLength / 2;

    private string PlayText() => _mci.IsOpen && _mci.Playing ? L.T("Pause") : L.T("Play");

    private void Repaint()
    {
        BackColor = Theme.Current.Window;
        Invalidate(true);
    }

    protected override async void OnShown(EventArgs e)
    {
        base.OnShown(e);
        if (_source.Length == 0) return;
        if (!File.Exists(_source))
        {
            _state.Message = () => L.F("This file could not be read: {0}", _source);
            _state.Notify();
            return;
        }
        string wav = Path.Combine(Path.GetTempPath(), $"utagoe-spans-{Environment.ProcessId}-{Guid.NewGuid():N}.wav");
        WaveTrack? track = null;
        string error = "";
        await Task.Run(() =>
        {
            try
            {
                if (Core.TranscodeFile(_source, wav, OutputFormat.Wav, OutputDepth.Int16, 0, out error)) track = WaveTrack.Read(wav);
            }
            catch (Exception ex) { error = ex.Message; }
        });
        if (IsDisposed)
        {
            TryDelete(wav);
            return;
        }
        if (track == null)
        {
            TryDelete(wav);
            _state.Message = () => L.F("This file could not be read: {0}", error);
            _state.Notify();
            return;
        }
        _wav = wav;
        _state.Track = track;
        _state.Message = null;
        _state.Spans.RemoveAll(x => x.A >= track.Duration);
        _state.SetView(0, track.Duration);
        _state.Edited();
        _detail.Focus();
    }

    private void SyncControls()
    {
        bool ready = _state.Track != null;
        foreach (var c in new Control[] { _play, _stop, _zoomIn, _zoomOut, _whole, _clear, _scroll }) c.Enabled = ready;
        _playSpan.Enabled = _remove.Enabled = ready && _state.Selected >= 0;
        _clear.Enabled = ready && _state.Spans.Count > 0;
        _play.Text = PlayText();
        _time.Text = ready ? SpanState.Format(_state.Playhead) + " / " + SpanState.Format(_state.Duration) : "";
        if (ready)
        {
            _syncingScroll = true;
            int large = Math.Max(1, (int)Math.Round(_state.ViewLength / _state.Duration * 1000));
            _scroll.Minimum = 0;
            _scroll.LargeChange = large;
            _scroll.SmallChange = Math.Max(1, large / 10);
            _scroll.Maximum = 1000 + large - 1;
            _scroll.Value = Math.Clamp((int)Math.Round(_state.ViewStart / _state.Duration * 1000), 0, 1000);
            _syncingScroll = false;
        }
    }

    private bool EnsureLoaded() => _wav != null && (_mci.IsOpen || _mci.Load(_wav));

    private void SeekPlayback(double t)
    {
        bool playing = _mci.Playing;
        _mci.Seek((int)Math.Round(t * 1000));
        if (playing) _mci.Play();
    }

    private void TogglePlay()
    {
        if (!EnsureLoaded()) return;
        if (_mci.Playing)
        {
            _mci.TogglePause();
            _timer.Stop();
        }
        else
        {
            _mci.Seek((int)Math.Round(_state.Playhead * 1000));
            _mci.Play();
            _timer.Start();
        }
        SyncControls();
    }

    private void PlaySpan(int i)
    {
        if (i < 0 || i >= _state.Spans.Count || !EnsureLoaded()) return;
        var (a, b) = _state.Spans[i];
        _state.Playhead = a;
        _stopAt = b;
        _mci.Seek((int)Math.Round(a * 1000));
        _mci.Play();
        _timer.Start();
        if (a < _state.ViewStart || b > _state.ViewStart + _state.ViewLength) _state.SetView(a - (_state.ViewLength - (b - a)) / 2, _state.ViewLength);
        SyncControls();
    }

    private void StopPlayback(bool rewindToStart)
    {
        if (_mci.IsOpen) _mci.Stop();
        _timer.Stop();
        _stopAt = -1;
        if (rewindToStart) _state.Playhead = 0;
        _state.Notify();
    }

    private void Tick()
    {
        if (!_mci.IsOpen) return;
        double t = _mci.Position / 1000.0;
        _state.Playhead = t;
        if (_stopAt >= 0 && t >= _stopAt)
        {
            _state.Playhead = _stopAt;
            StopPlayback(false);
            return;
        }
        if (!_mci.Playing) _timer.Stop();
        if (t > _state.ViewStart + _state.ViewLength || t < _state.ViewStart) _state.SetView(t, _state.ViewLength);
        else _state.Notify();
    }

    protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
    {
        if (keyData == Keys.Space && _state.Track != null)
        {
            TogglePlay();
            return true;
        }
        if (keyData == Keys.Escape)
        {
            Close();
            return true;
        }
        return base.ProcessCmdKey(ref msg, keyData);
    }

    protected override void OnFormClosed(FormClosedEventArgs e)
    {
        Theme.Changed -= Repaint;
        _timer.Stop();
        _timer.Dispose();
        _mci.Dispose();
        if (_wav != null) TryDelete(_wav);
        base.OnFormClosed(e);
    }

    private static void TryDelete(string path)
    {
        try { File.Delete(path); } catch (IOException) { } catch (UnauthorizedAccessException) { }
    }
}
