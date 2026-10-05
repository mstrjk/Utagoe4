// 元実装の main window の挙動を再現する。
// handler 名は RTTI から復元した名前を維持し、NOTES.md と突き合わせやすくしている。

using System.Diagnostics;
using Utagoe.App;
using Utagoe.Native;
using Utagoe.Ui;

namespace Utagoe.Forms;

internal sealed partial class MainForm : UiWindow
{
    private readonly AppSettings _settings;
    private bool _debugMode;

    private CancellationTokenSource? _cancel;
    private bool _closeWhenIdle;
    private TaskCompletionSource? _stopNow;
    private Task? _winding;
    private int _elapsed;

    private bool IsRunning => _cancel != null;

    // 「GPU が使えない」通知はこの起動中に一度だけ出す。
    private bool _gpuNoticeShown;

    public MainForm(AppSettings settings) : base(Definition)
    {
        _settings = settings;
        InitializeComponent();

        // TBevel は windowed control の下で form surface に直接描く。VCL と同じ重なり順にする。
        BevelPainter.Attach(this,
            new Bevel(8, 8, 553, 257),
            new Bevel(16, 168, 433, 9, BevelShape.TopLine),
            new Bevel(456, 16, 9, 241, BevelShape.LeftLine),
            new Bevel(458, 270, 103, 17));
        ScaleToFont();
        InitPanels();

        // GPU の初期化には時間がかかることがあるので、起動直後に裏で済ませておく。
        if (_settings.Values.UseGpu != 0) _ = Task.Run(() => Core.Gpu);

        BitBtn1.Click += (_, _) => BrowseInput(Edit1, isOriginal: true);
        BitBtn2.Click += (_, _) => BrowseInput(Edit2, isOriginal: false);
        BitBtn3.Click += BitBtn3Click;
        PlayBtn1.Click += (_, _) => Play(Edit1.Text);
        PlayBtn2.Click += (_, _) => Play(Edit2.Text);
        Edit1.TextChanged += (_, _) => UpdatePlayButtons();
        Edit2.TextChanged += (_, _) => UpdatePlayButtons();
        Edit3.Text = FileNaming.Clean(OutputFolder);
        Edit3.Leave += (_, _) => RememberFolder(Edit3.Text.Trim());
        OverwriteCheckBox.Checked = _settings.Values.OverwriteOutput != 0;
        OverwriteCheckBox.CheckedChanged += (_, _) =>
        {
            int value = OverwriteCheckBox.Checked ? 1 : 0;
            if (_settings.Values.OverwriteOutput == value) return;
            var v = _settings.Values;
            v.OverwriteOutput = value;
            _settings.Values = v;
            _settings.Save();
        };
        NormaliseCheckBox.Checked = _settings.Values.NormalizeOutput != 0;
        NormaliseCheckBox.CheckedChanged += (_, _) =>
        {
            int value = NormaliseCheckBox.Checked ? 1 : 0;
            if (_settings.Values.NormalizeOutput == value) return;
            var v = _settings.Values;
            v.NormalizeOutput = value;
            _settings.Values = v;
            _settings.Save();
        };
        CacheCheckBox.Checked = _settings.Values.CacheSteps != 0;
        CacheCheckBox.CheckedChanged += (_, _) =>
        {
            int value = CacheCheckBox.Checked ? 1 : 0;
            if (_settings.Values.CacheSteps == value) return;
            var v = _settings.Values;
            v.CacheSteps = value;
            _settings.Values = v;
            _settings.Save();
            if (value == 0) Core.ClearStepCache();
        };
        FitOutputChecks();
        UiText.OnChange(this, FitOutputChecks);

        // DFM 上では Edit1 / Edit3 が同じ KeyPress handler、Edit2 は別 handler。
        Edit1.KeyPress += Edit1KeyPress;
        Edit3.KeyPress += Edit1KeyPress;
        Edit2.KeyPress += Edit2KeyPress;

        StartBtn.Click += StartBtnClick;
        SetBitBtn.Click += SetBitBtnClick;
        HelpBtn.Click += HelpBtnClick;
        FaqBtn.Click += (_, _) => FaqForm.ShowFor(this);
        AboutBtn.Click += (_, _) => ShowAbout();
        CloseBtn.Click += (_, _) => Close();
        DbgPanel.DoubleClick += DbgPanelDblClick;
        ElapsedTimer.Tick += (_, _) => InfoLabel.Text = Messages.Elapsed(++_elapsed);

        foreach (Control c in new Control[] { this, Edit1, Edit2, Edit3 })
        {
            c.DragEnter += OnDragEnter;
            c.DragDrop += OnDragDrop;
        }
        ApplyOutputKind();
        UiText.OnChange(this, () => _infoPane?.ShowText(Messages.Welcome));
    }

    private void BrowseInput(TextBox target, bool isOriginal)
    {
        using var dlg = new OpenFileDialog { Filter = Messages.OpenFilter };
        SeedDialog(dlg, target.Text);
        if (dlg.ShowDialog(this) != DialogResult.OK) return;

        if (isOriginal) SetOriginal(dlg.FileName);
        else SetInstrumental(dlg.FileName);
    }

    // v4: 出力は folder を選ぶ。形式は Settings の Output で決める。
    private void BitBtn3Click(object? sender, EventArgs e)
    {
        using var dlg = new FolderBrowserDialog
        {
            Description = Messages.ChooseFolder,
            UseDescriptionForTitle = true,
            ShowNewFolderButton = true,
            InitialDirectory = Directory.Exists(Edit3.Text.Trim()) ? Edit3.Text.Trim() : OutputFolder,
        };
        if (dlg.ShowDialog(this) != DialogResult.OK) return;
        Edit3.Text = FileNaming.Clean(dlg.SelectedPath);
        RememberFolder(Edit3.Text);
    }

    /// 設定に覚えている出力先。未設定なら Music\Utagoe。
    private string OutputFolder =>
        _settings.Values.OutputFolder is { Length: > 0 } f ? f : AppPaths.DefaultOutputFolder;

    private void RememberFolder(string folder)
    {
        if (folder.Length == 0 || folder == _settings.Values.OutputFolder) return;
        _settings.Values.OutputFolder = folder;
        _settings.Save();
    }

    /// 原曲から出力 file の path を作る。名前は原曲の名前 (+ 自動命名の文字)、拡張子は出力形式のもの。
    private string OutputFileFor(string original, string folder)
    {
        string suffix = SaveFiles.Kind(_settings.Values) == 1 ? Messages.PairSuffix
                      : _settings.Values.AutoNameOutput != 0 ? _settings.Values.OutputSuffix : "";
        string name = Path.GetFileName(FileNaming.AutoOutputName(original, suffix,
                                       Core.FormatExtension((OutputFormat)_settings.Values.OutputFormat)));
        return Path.Combine(folder, name);
    }

    private static void SeedDialog(FileDialog dlg, string current)
    {
        if (string.IsNullOrWhiteSpace(current)) return;
        try
        {
            dlg.InitialDirectory = Path.GetDirectoryName(current) ?? "";
            dlg.FileName = Path.GetFileName(current);
        }
        catch (ArgumentException) { /* 手入力で壊れた path は無視する。 */ }
    }

    /// original を設定したら Misc 設定に応じて matching instrumental 検索と output 自動命名も走らせる。
    private void SetOriginal(string path)
    {
        path = FileNaming.Clean(path);
        Edit1.Text = path;
        WformLbl1.Text = DescribeAudio(path);

        if (_settings.Values.SearchInstFile != 0)
        {
            string? inst = FileNaming.FindInstrumental(path);
            if (inst != null) SetInstrumental(inst);
        }
    }

    private void SetInstrumental(string path)
    {
        path = FileNaming.Clean(path);
        Edit2.Text = path;
        WformLbl2.Text = DescribeAudio(path);
    }

    private static string DescribeAudio(string path) =>
        Core.TryProbeAudio(path, out var info, out _) ? Messages.AudioFormat(info) : "";

    private static bool IsAudioFile(string path) =>
        Messages.AudioExtensions.Contains(Path.GetExtension(path), StringComparer.OrdinalIgnoreCase);

    // path box で Enter を押すと browse 済みと同じ扱いで確定する。
    private void Edit1KeyPress(object? sender, KeyPressEventArgs e)
    {
        if (e.KeyChar != (char)Keys.Enter) return;
        e.Handled = true;
        if (sender == Edit1) SetOriginal(Edit1.Text.Trim());
    }

    private void Edit2KeyPress(object? sender, KeyPressEventArgs e)
    {
        if (e.KeyChar != (char)Keys.Enter) return;
        e.Handled = true;
        SetInstrumental(Edit2.Text.Trim());
    }

    // drag&drop は window 全体で受け、drop 座標からどの欄へ入れるか決める。

    private void OnDragEnter(object? sender, DragEventArgs e)
    {
        e.Effect = !IsRunning && e.Data?.GetDataPresent(DataFormats.FileDrop) == true
            ? DragDropEffects.Copy : DragDropEffects.None;
    }

    private void OnDragDrop(object? sender, DragEventArgs e)
    {
        if (e.Data?.GetData(DataFormats.FileDrop) is not string[] { Length: > 0 } files) return;
        string file = FileNaming.Clean(files[0]);
        // 欄の境目は scaling / 拡大に合わせて、各見出しの位置で決める。
        int y = PointToClient(new Point(e.X, e.Y)).Y;

        // 出力欄には folder を入れる。file を落としたらその file のある folder。
        if (y >= Label3.Top - Label3.Height)
        {
            string folder = Directory.Exists(file) ? file : Path.GetDirectoryName(file) ?? "";
            if (folder.Length == 0) return;
            Edit3.Text = folder;
            RememberFolder(folder);
            return;
        }

        if (!IsAudioFile(file))
        {
            Utagoe.Forms.MessageForm.Show(this, Messages.DropWave, Messages.Title,
                            MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }

        if (y < Label2.Top - Label2.Height) SetOriginal(file);
        else SetInstrumental(file);
    }

    private void Play(string path)
    {
        path = path.Trim();
        if (path.Length == 0 || !File.Exists(path)) return;
        // v4: 再生中も main window を使え、複数の再生 window を同時に開ける。
        new PlaybackForm(path) { StartPosition = FormStartPosition.CenterParent }.Show(this);
    }

    private async void StartBtnClick(object? sender, EventArgs e)
    {
        if (IsRunning)
        {
            if (ConfirmHalt()) HaltNow();
            return;
        }

        if (_winding is { IsCompleted: false })
        {
            StartBtn.Enabled = false;
            LogHub.Detail("waiting for the cancelled run to let go of its files");
            await _winding;
            StartBtn.Enabled = true;
        }

        string original = FileNaming.Clean(Edit1.Text);
        string instrumental = FileNaming.Clean(Edit2.Text);
        string folder = FileNaming.Clean(Edit3.Text);

        if (!ValidateInputs(original, instrumental, folder, out string output)) return;
        await NoticeGpuOnce();

        _cancel = new CancellationTokenSource();
        var token = _cancel.Token;
        var stop = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        _stopNow = stop;
        var settings = _settings.Values;   // 処理開始時に settings を値コピーして snapshot 化する。
        settings.OutputKind = SaveFiles.Kind(settings);

        SetRunning(true);
        LogHub.Busy = true;
        LogHub.Line(new string('-', 72));
        LogHub.Line($"start  {DateTime.Now:yyyy-MM-dd HH:mm:ss}");
        var clock = System.Diagnostics.Stopwatch.StartNew();

        int rc = 0;
        string error = "";
        CoreResult result = default;
        int lastPercent = -1;

        try
        {
            var work = Task.Run(() =>
            {
                int code = Core.ExtractFile(
                    original, instrumental, output, settings,
                    fraction =>
                    {
                        if (token.IsCancellationRequested || IsDisposed) return false;
                        if (fraction < 0)
                        {
                            Invoke(() =>
                            {
                                foreach (string stopped in App.MciPlayer.ReleaseWhere(path => Targets(output, settings, path)))
                                    LogHub.Detail($"stopped playing {Path.GetFileName(stopped)} so it can be rewritten");
                            });
                            return !token.IsCancellationRequested;
                        }
                        int pct = (int)(fraction * 100);
                        if (pct != lastPercent)
                        {
                            lastPercent = pct;
                            BeginInvoke(() =>
                            {
                                if (!token.IsCancellationRequested) ProgBar1.Value = Math.Clamp(pct, 0, 100);
                            });
                        }
                        return !token.IsCancellationRequested;
                    },
                    out CoreResult r, out string err);
                return (code, r, err);
            });
            if (await Task.WhenAny(work, stop.Task) == work)
            {
                (rc, result, error) = await work;
            }
            else
            {
                rc = 4;
                _winding = work.ContinueWith(t => _ = t.Exception, TaskScheduler.Default);
            }
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException)
        {
            rc = -1;
            error = ex.Message;
            LogHub.Exception("the native core could not be called", ex);
        }
        catch (Exception ex)
        {
            // 想定外の .NET 側の失敗。本当の stack trace を terminal と log に残す。
            rc = -1;
            error = $"{ex.GetType().Name}: {ex.Message}";
            LogHub.Exception("unexpected error while processing", ex);
        }
        finally
        {
            _stopNow = null;
            _cancel = null;
            SetRunning(false);
            LogHub.Busy = false;
        }
        LogHub.Line(rc switch
        {
            0 when settings.OutputKind == 1 => $"finished in {clock.Elapsed:mm\\:ss\\.f} -> {Core.AlignedPairPaths(output).Main} + {Core.AlignedPairPaths(output).Inst}",
            0 => $"finished in {clock.Elapsed:mm\\:ss\\.f} -> {output}",
            4 => "cancelled",
            _ => $"failed (code {rc}) after {clock.Elapsed:mm\\:ss\\.f}",
        });

        if (rc == 0)
        {
            ProgBar1.Value = 100;
            string[] written = result.Written;
            _lastOutputs = written.Length > 0 ? written.Where(File.Exists).ToList() : ProducedFiles(output, settings.OutputKind);
            _results?.SetFiles(_lastOutputs);
            string note = ConversionNote(result);
            if (result.GpuUsed != 0) note = (note.Length > 0 ? note + "\n" : "") + Messages.GpuUsed(result.GpuAdapter);
            Hints.SetToolTip(InfoLabel, note);
            if (_debugMode) ReportDebug(output, result);
        }
        else if (rc == 4)
        {
            ProgBar1.Value = 0;
        }
        else
        {
            ProgBar1.Value = 0;
            Utagoe.Forms.MessageForm.Show(this, (rc == 6 ? $"{Messages.FileCreateError}\n\n{error}" : error) + Messages.SeeTerminal,
                            Messages.Title, MessageBoxButtons.OK, MessageBoxIcon.Error);
        }

        if (_closeWhenIdle) Close();
    }

    /// GPU を使う設定なのに使えないとき、最初の Start で一度だけ知らせる。「今後表示しない」を選べる。
    private async Task NoticeGpuOnce()
    {
        if (_gpuNoticeShown || _settings.Values.UseGpu == 0 || _settings.Values.GpuNoticeHidden != 0) return;
        _gpuNoticeShown = true;
        var gpu = await Task.Run(() => Core.Gpu);
        if (gpu.Available) return;
        using var notice = new GpuNoticeForm(gpu.Reason);
        notice.ShowDialog(this);
        if (notice.DontShowAgain)
        {
            _settings.Values.GpuNoticeHidden = 1;
            _settings.Save();
        }
    }

    /// 処理前 check。形式・bit depth・sample rate の違いは core 側で吸収するので、ここでは読めるかだけ確かめる。
    private bool ValidateInputs(string original, string instrumental, string folder, out string output)
    {
        output = "";
        if (original.Length == 0 || instrumental.Length == 0 || folder.Length == 0)
            return Fail(Messages.NeedFiles);

        // 出力先の folder は無ければ作る (既定の Music\Utagoe も初回はまだ無い)。
        try { Directory.CreateDirectory(folder); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
        {
            return Fail(Messages.FolderError(folder, ex.Message));
        }
        RememberFolder(folder);
        output = OutputFileFor(original, folder);

        static string Full(string p) { try { return Path.GetFullPath(p); } catch { return p; } }
        // 揃えた組は出力欄の名前から作る 2 ファイルを書くので、その 2 つを入力と比べる。
        int runKind = SaveFiles.Kind(_settings.Values);
        bool pair = runKind == 1;
        if (pair && string.Equals(Full(original), Full(instrumental), StringComparison.OrdinalIgnoreCase))
            return Fail(Messages.PairNeedsTwo);
        var (pairMain, pairInst) = pair ? Core.AlignedPairPaths(output) : (output, output);
        foreach (string written in new[] { pairMain, pairInst })
            if (string.Equals(Full(written), Full(original), StringComparison.OrdinalIgnoreCase) ||
                string.Equals(Full(written), Full(instrumental), StringComparison.OrdinalIgnoreCase))
                return Fail(Messages.SameInputOutput);

        if (!Core.TryProbeAudio(original, out _, out string e1))
            return Fail(Messages.Unreadable("original", e1));
        if (!Core.TryProbeAudio(instrumental, out _, out string e2))
            return Fail(Messages.Unreadable("instrumental", e2));

        if (_settings.Values.OverwriteOutput == 0 && Written(output).FirstOrDefault(Taken) is { } existing)
        {
            var answer = Utagoe.Forms.MessageForm.Show(this, Messages.OverwritePrompt(Path.GetFileName(existing)), Messages.Title,
                                                       MessageBoxButtons.YesNo, MessageBoxIcon.Question);
            if (answer != DialogResult.Yes) output = NextFree(output);
        }

        return true;

        IEnumerable<string> Written(string path)
        {
            int kind = SaveFiles.Kind(_settings.Values);
            if (kind == 0) return new[] { path }.Concat(SaveFiles.Extras(path, _settings.Values));
            var (first, second) = Core.AlignedPairPaths(path);
            return new[] { first, second };
        }

        static bool Taken(string path) => File.Exists(path) || Directory.Exists(path);

        string NextFree(string path)
        {
            string dir = Path.GetDirectoryName(path) ?? "";
            string stem = Path.GetFileNameWithoutExtension(path), ext = Path.GetExtension(path);
            for (int n = 1; ; n++)
            {
                string candidate = Path.Combine(dir, $"{stem}{n:00}{ext}");
                if (!Written(candidate).Any(Taken)) return candidate;
            }
        }

        bool Fail(string msg)
        {
            Utagoe.Forms.MessageForm.Show(this, msg, Messages.Title, MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return false;
        }
    }

    /// インストを原曲の形式へ変換した場合は、その内容を status 欄の tooltip に出す。
    private static string ConversionNote(in CoreResult r)
    {
        if (r.InstrumentalRateIn == r.OutputRate && r.InstrumentalChannelsIn == r.OutputChannels) return "";
        static string Ch(int n) => n switch { 1 => L.T("Mono"), 2 => L.T("Stereo"), _ => $"{n}ch" };
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        return string.Format(inv, L.T("Instrumental converted: {0:F3}kHz {1} → {2:F3}kHz {3}"),
                             r.InstrumentalRateIn / 1000.0, Ch(r.InstrumentalChannelsIn),
                             r.OutputRate / 1000.0, Ch(r.OutputChannels));
    }

    private void ApplyOutputKind()
    {
        foreach (Control c in new Control[] { Label2, Edit2, BitBtn2 })
            c.Enabled = !IsRunning;
        UpdatePlayButtons();
    }

    private void FitOutputChecks()
    {
        float k = _mainZoom?.Factor ?? 1f;
        int Width(Control c) => (int)Math.Ceiling(c.PreferredSize.Width / k);
        Rectangle Base(Control c) => _mainZoom?.BaseBounds(c) is { Width: > 0 } b ? b : c.Bounds;
        void Place(Control c, Rectangle b)
        {
            if (_mainZoom != null) _mainZoom.SetBase(c, b);
            else c.Bounds = b;
        }
        int gap = LogicalToDeviceUnits(16);
        var overwrite = Base(OverwriteCheckBox);
        overwrite.Width = Width(OverwriteCheckBox);
        var normalise = Base(NormaliseCheckBox);
        normalise.X = overwrite.Right + gap;
        normalise.Width = Width(NormaliseCheckBox);
        var cache = Base(CacheCheckBox);
        cache.X = normalise.Right + gap;
        cache.Width = Math.Min(Width(CacheCheckBox), LogicalToDeviceUnits(452) - cache.X);
        Place(OverwriteCheckBox, overwrite);
        Place(NormaliseCheckBox, normalise);
        Place(CacheCheckBox, cache);
    }

    private static bool Targets(string output, in CoreSettings settings, string path)
    {
        static string Full(string p) { try { return Path.GetFullPath(p); } catch { return p; } }
        string candidate = Full(path);
        int kind = settings.OutputKind;
        if (kind == 0)
            return new[] { output }.Concat(SaveFiles.Extras(output, settings))
                .Any(f => string.Equals(candidate, Full(f), StringComparison.OrdinalIgnoreCase));
        var (first, second) = Core.AlignedPairPaths(output);
        return string.Equals(candidate, Full(first), StringComparison.OrdinalIgnoreCase) ||
               string.Equals(candidate, Full(second), StringComparison.OrdinalIgnoreCase);
    }

    private static List<string> ProducedFiles(string output, int kind)
    {
        var files = new List<string>();
        if (kind == 0) files.Add(output);
        else
        {
            var (first, second) = Core.AlignedPairPaths(output);
            files.Add(first);
            files.Add(second);
        }
        return files.Where(File.Exists).ToList();
    }

    private void UpdatePlayButtons()
    {
        static bool Playable(string text)
        {
            string path = FileNaming.Clean(text.Trim());
            try { return path.Length > 0 && File.Exists(path); }
            catch (Exception ex) when (ex is ArgumentException or NotSupportedException or IOException) { return false; }
        }
        PlayBtn1.Enabled = !IsRunning && Playable(Edit1.Text);
        PlayBtn2.Enabled = !IsRunning && Playable(Edit2.Text);
    }

    private void SetRunning(bool running)
    {
        StartBtn.Text = running ? Messages.Running : Messages.Start;

        foreach (Control c in new Control[]
                 { Edit1, Edit2, Edit3, BitBtn1, BitBtn2, BitBtn3, OverwriteCheckBox, NormaliseCheckBox, CacheCheckBox,
                   PlayBtn1, PlayBtn2, SetBitBtn })
            c.Enabled = !running;
        if (!running) ApplyOutputKind();

        if (running)
        {
            ProgBar1.Value = 0;
            _elapsed = 0;
            InfoLabel.Text = Messages.Elapsed(0);
            ElapsedTimer.Start();
        }
        else
        {
            ElapsedTimer.Stop();
        }
    }

    private bool ConfirmHalt() =>
        Utagoe.Forms.MessageForm.Show(this, Messages.HaltProcess, Messages.Title,
                        MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes;

    /// debug mode では output 横に .txt を書く。元実装の中身は未追跡なので alignment debug line のみ出す。
    private void ReportDebug(string output, CoreResult result)
    {
        string line = result.Debug;
        try { File.WriteAllText(Path.ChangeExtension(output, ".txt"), line + Environment.NewLine); }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
        InfoLabel.Text = $"ofs {result.Offset}";
        Hints.SetToolTip(InfoLabel, line);
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        // 処理中の close は確認を出し、worker が止まってから実際に window を閉じる。
        if (IsRunning)
        {
            e.Cancel = true;
            if (ConfirmHalt())
            {
                _closeWhenIdle = true;
                HaltNow();
            }
            return;
        }
        base.OnFormClosing(e);
    }

    private void HaltNow()
    {
        _cancel?.Cancel();
        _stopNow?.TrySetResult();
    }

    protected override void OnFormClosed(FormClosedEventArgs e)
    {
        base.OnFormClosed(e);
        if (_winding is { IsCompleted: false }) Environment.Exit(0);
    }

    // v4: Settings は modal にしない (MainForm.Panels.cs)。
    private void SetBitBtnClick(object? sender, EventArgs e) => ShowSettings();

    // Help は元の PDF の代わりに terminal を開く。
    private void HelpBtnClick(object? sender, EventArgs e) => ShowTerminal();

    private void DbgPanelDblClick(object? sender, EventArgs e)
    {
        _debugMode = !_debugMode;
        Utagoe.Forms.MessageForm.Show(this, Messages.DebugModeIs + (_debugMode ? "ON" : "OFF"), Messages.Title,
                        MessageBoxButtons.OK, MessageBoxIcon.Information);
    }
}
