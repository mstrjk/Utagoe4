using Utagoe.App;

namespace Utagoe.Ui;

internal static class Element
{
    public static ThemedLabel Label(Func<string> text, int left, int top, bool transparent = false) =>
        UiTheme.Bind(UiText.Bind(new ThemedLabel { Location = new Point(left, top), AutoSize = true, BackColor = transparent ? Color.Transparent : default }, text), deep: false);

    public static ThemedLabel Label(string key, int left, int top, bool transparent = false) => Label(() => L.T(key), left, top, transparent);

    public static ThemedLabel Text(Func<string> text, Rectangle bounds, bool transparent = false) =>
        UiText.Bind(new ThemedLabel { Bounds = bounds, UseMnemonic = false, BackColor = transparent ? Color.Transparent : default }, text);

    public static VclLabel Value(string initial, int left, int top, int width, int height) =>
        new(rightJustify: true) { Text = initial, Bounds = new Rectangle(left, top, width, height) };

    public static ThemedLabel Dynamic(int left, int top, bool transparent = false) =>
        new() { Text = "", Location = new Point(left, top), AutoSize = true, BackColor = transparent ? Color.Transparent : default };

    public static ThemedRadioButton Option(Func<string> text, Point at, int tab) =>
        UiText.Bind(new ThemedRadioButton { Location = at, AutoSize = true, TabIndex = tab }, text);

    public static ThemedTabControl InnerTabs(int tab) => new() { Dock = DockStyle.Fill, TabIndex = tab, SizeMode = TabSizeMode.Normal };

    public static BitBtn Button(Func<string> text, Rectangle bounds, int tab, string? glyph = null)
    {
        var b = UiTheme.Bind(UiText.Bind(new BitBtn { Bounds = bounds, TabIndex = tab }, text));
        if (glyph != null) b.SetGlyph(glyph);
        return b;
    }

    public static BitBtn Button(string key, Rectangle bounds, int tab, string? glyph = null) => Button(() => L.T(key), bounds, tab, glyph);

    public static BitBtn Glyph(string glyph, Rectangle bounds, int tab)
    {
        var b = UiTheme.Bind(new BitBtn { Bounds = bounds, TabIndex = tab });
        b.SetGlyph(glyph);
        return b;
    }

    public static BitBtn Ok(Rectangle bounds, int tab, bool closes = false)
    {
        var b = UiTheme.Bind(new BitBtn { Bounds = bounds, TabIndex = tab });
        b.SetKind(BitBtnKind.OK);
        UiText.Key(b, "OK");
        if (closes) b.DialogResult = DialogResult.None;
        return b;
    }

    public static BitBtn Close(Rectangle bounds, int tab)
    {
        var b = UiTheme.Bind(new BitBtn { Bounds = bounds, TabIndex = tab });
        b.SetKind(BitBtnKind.OK);
        UiText.Key(b, "Close");
        b.DialogResult = DialogResult.None;
        return b;
    }

    public static BitBtn Cancel(Rectangle bounds, int tab)
    {
        var b = UiTheme.Bind(UiText.Key(new BitBtn { Bounds = bounds, TabIndex = tab }, "Cancel"));
        b.SetKind(BitBtnKind.Cancel);
        return b;
    }

    public static ThemedCheckBox Check(Func<string> text, Rectangle bounds, int tab) =>
        UiText.Bind(new ThemedCheckBox { Bounds = bounds, TabIndex = tab }, text);

    public static ThemedCheckBox Check(string key, Rectangle bounds, int tab) => Check(() => L.T(key), bounds, tab);

    public static ThemedRadioButton Radio(string key, Rectangle bounds, int tab) =>
        UiText.Key(new ThemedRadioButton { Bounds = bounds, TabIndex = tab }, key);

    public static VclGroupBox Group(Func<string> caption, Rectangle bounds, int tab) =>
        UiText.Bind(new VclGroupBox { Bounds = bounds, TabIndex = tab }, caption);

    public static VclGroupBox Group(string key, Rectangle bounds, int tab) => Group(() => L.T(key), bounds, tab);

    public static VclRadioGroup Choices(Func<string> caption, Rectangle bounds, int tab, Func<string[]> items)
    {
        var g = UiText.Bind(new VclRadioGroup { Bounds = bounds, TabIndex = tab }, caption);
        g.Items = items();
        UiText.OnChange(g, () =>
        {
            var now = items();
            for (int i = 0; i < g.Buttons.Count && i < now.Length; i++) g.Buttons[i].Text = now[i];
        });
        return g;
    }

    public static VclRadioGroup Choices(string key, Rectangle bounds, int tab, params string[] itemKeys) =>
        Choices(() => L.T(key), bounds, tab, () => L.A(itemKeys));

    public static VclTrackBar Slider(Rectangle bounds, int tab) => UiTheme.Bind(new VclTrackBar(ticksBoth: true, thumbLength: 15)
    {
        Bounds = bounds,
        Minimum = 0,
        Maximum = 20,
        SmallChange = 1,
        LargeChange = 1,
        TabIndex = tab,
    });

    public static VclTrackBar Seek(Rectangle bounds, int tab) => UiTheme.Bind(new VclTrackBar { Bounds = bounds, TabIndex = tab });

    public static TextBox Field(Rectangle bounds, int tab, int maxLength = 32767, bool drop = false) =>
        UiTheme.Bind(new TextBox { Bounds = bounds, TabIndex = tab, MaxLength = maxLength, AllowDrop = drop });

    public static NumericUpDown Number(Rectangle bounds, int tab, int min, int max) =>
        UiTheme.Bind(new ThemedNumber { Bounds = bounds, Minimum = min, Maximum = max, TabIndex = tab });

    public static ThemedComboBox List(Rectangle bounds, int tab, Func<string[]> items)
    {
        var c = new ThemedComboBox { Bounds = bounds, DropDownStyle = ComboBoxStyle.DropDownList, TabIndex = tab };
        UiText.Items(c, items);
        return UiTheme.Bind(c);
    }

    public static ThemedComboBox List(Rectangle bounds, int tab, params string[] fixedItems) => List(bounds, tab, () => fixedItems);

    public static ThemedComboBox Editable(Rectangle bounds, int tab, int maxLength, params string[] items)
    {
        var c = new ThemedComboBox { Bounds = bounds, MaxLength = maxLength, TabIndex = tab };
        c.Items.AddRange(items);
        return UiTheme.Bind(c);
    }

    public static CheckDropDown MultiList(Rectangle bounds, int tab, Func<string[]> choices)
    {
        var c = new CheckDropDown { Bounds = bounds, TabIndex = tab, Choices = choices() };
        UiText.OnChange(c, () => c.Choices = choices());
        return UiTheme.Bind(c);
    }

    public static ThemedTabControl Tabs(Rectangle bounds, int tab, bool fillToRight, int minRows = 1) =>
        new() { Bounds = bounds, TabIndex = tab, Multiline = fillToRight, SizeMode = fillToRight ? TabSizeMode.FillToRight : TabSizeMode.Normal, MinRows = minRows };

    public static TabPage Page(Func<string> text)
    {
        var p = new TabPage { UseVisualStyleBackColor = true };
        UiText.Page(p, text);
        return UiTheme.Bind(p, deep: false);
    }

    public static TabPage Page(string key) => Page(() => L.T(key));

    public static ThemedProgressBar Progress(Rectangle bounds, int tab) => new() { Bounds = bounds, TabIndex = tab };

    public static Panel Box(Rectangle bounds, int tab = 0) => new() { Bounds = bounds, TabIndex = tab };

    public static Panel Area(DockStyle dock, int size = 0, Padding padding = default)
    {
        var p = new Panel { Dock = dock, Padding = padding };
        if (dock is DockStyle.Top or DockStyle.Bottom) p.Height = size;
        else if (dock is DockStyle.Left or DockStyle.Right) p.Width = size;
        return p;
    }

    public static FlowLayoutPanel Row(DockStyle dock, int height, Padding padding) =>
        new() { Dock = dock, Height = height, Padding = padding, WrapContents = false };

    public static HScrollBar HScroll(DockStyle dock) => new() { Dock = dock };

    public static ThemedLabel Status(Size size, Padding margin) =>
        new() { AutoSize = false, TextAlign = ContentAlignment.MiddleLeft, Size = size, Margin = margin };

    public static ThemedLabel Note(Func<string> text, DockStyle dock, int height) =>
        UiText.Bind(new ThemedLabel { AutoSize = false, TextAlign = ContentAlignment.MiddleLeft, Dock = dock, Height = height }, text);

    public static BitBtn RowButton(Func<string> text, int width)
    {
        var b = UiTheme.Bind(UiText.Bind(new BitBtn(), text));
        b.Size = new Size(width, 25);
        b.Margin = new Padding(3, 0, 3, 0);
        return b;
    }

    public static Panel ScrollArea(int tab, bool visible = true) =>
        new() { AutoScroll = true, Visible = visible, BackColor = Color.Transparent, TabIndex = tab };

    public static ThemedLabel Para(string text, Font? bold = null, string? tag = null, bool mnemonic = false)
    {
        var l = new ThemedLabel { Text = text, AutoSize = true, UseMnemonic = mnemonic, BackColor = Color.Transparent, Tag = tag };
        if (bold != null) l.Font = new Font(bold, FontStyle.Bold);
        UiTheme.Bind(l, deep: false);
        return bold != null ? l.WithOwnFont() : l;
    }

    public static ThemedRadioButton Radio(Func<string> text, Rectangle bounds, int tab) =>
        UiText.Bind(new ThemedRadioButton { Bounds = bounds, TabIndex = tab }, text);

    public static ThemedLabel DynamicText(string initial, Rectangle bounds, bool ellipsis = false) =>
        new() { Text = initial, Bounds = bounds, AutoEllipsis = ellipsis };

    public static MediaPlayerStrip Player(Rectangle bounds, int tab) =>
        new("MPPLAY", "MPPAUSE", "MPSTOP", "MPPREV") { Bounds = bounds, TabIndex = tab };

    public static IconChoice Swatch(string name, Size size) => new(name) { Size = size, TabStop = true };

    public static WaveOverview Overview(SpanState state, int height) => new(state) { Dock = DockStyle.Top, Height = height };

    public static WaveDetail Detail(SpanState state) => new(state) { Dock = DockStyle.Fill };

    public static ResultsPanel Results() => UiTheme.Bind(new ResultsPanel());

    public static InfoPane Info(string text)
    {
        var pane = new InfoPane();
        pane.ShowText(text);
        return pane;
    }

    public static PictureBox Picture(Image image, Rectangle bounds, PictureBoxSizeMode mode) =>
        new() { Bounds = bounds, Image = image, SizeMode = mode, BackColor = Color.Transparent };

    public static LinkLabel Link(string text, string? url, int start = 0, int length = -1) =>
        Links(text, (start, length < 0 ? text.Length - start : length, url));

    public static LinkLabel Links(string text, params (int Start, int Length, string? Url)[] ranges)
    {
        var l = new LinkLabel
        {
            Text = text,
            AutoSize = true,
            UseMnemonic = false,
            BackColor = Color.Transparent,
            LinkColor = HelpLinks.LinkColor,
            ActiveLinkColor = HelpLinks.LinkColor,
            VisitedLinkColor = HelpLinks.LinkColor,
            LinkBehavior = LinkBehavior.AlwaysUnderline,
        };
        l.Links.Clear();
        foreach (var (start, length, url) in ranges)
            if (length > 0) l.Links.Add(start, length, url);
        l.LinkClicked += (_, e) => Open(e.Link?.LinkData as string);
        return UiTheme.Bind(l);
    }

    public static Label Plain(Func<string> text, Rectangle bounds) =>
        UiText.Bind(new Label { Bounds = bounds, BackColor = Color.Transparent }, text);

    public static ThemedLabel Caption(Func<string> text, Point at, float? fontSize = null)
    {
        var l = UiText.Bind(new ThemedLabel { Location = at, AutoSize = true, UseMnemonic = false, BackColor = Color.Transparent }, text);
        if (fontSize is float size) l.Font = new Font("Tahoma", size);
        return fontSize != null ? l.WithOwnFont() : l;
    }

    public static void Open(string? target)
    {
        if (string.IsNullOrEmpty(target)) return;
        try
        {
            if (target.StartsWith("https://", StringComparison.Ordinal))
            {
                System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(target) { UseShellExecute = true });
                return;
            }
            string file = Path.Combine(AppPaths.Licenses, target);
            if (File.Exists(file)) System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo("notepad.exe", $"\"{file}\"") { UseShellExecute = true });
            else LogHub.Warning($"license file not found: {file}");
        }
        catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or InvalidOperationException)
        {
            LogHub.Exception("could not open " + target, ex);
        }
    }
}
