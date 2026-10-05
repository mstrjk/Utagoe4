namespace Utagoe.Ui;

internal sealed class CheckDropDown : ThemedComboBox
{
    private string[] _items = Array.Empty<string>();
    private bool[] _checked = Array.Empty<bool>();
    private bool[] _available = Array.Empty<bool>();
    private ToolStripDropDown? _popup;
    private long _closedAt;

    public event EventHandler? CheckedChanged;

    public CheckDropDown()
    {
        DropDownStyle = ComboBoxStyle.DropDownList;
        base.Items.Add("");
        SelectedIndex = 0;
    }

    [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
    public string[] Choices
    {
        get => _items;
        set
        {
            int n = value.Length;
            Array.Resize(ref _checked, n);
            int old = _available.Length;
            Array.Resize(ref _available, n);
            for (int i = old; i < n; ++i) _available[i] = true;
            _items = value;
            UpdateText();
        }
    }

    [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
    public int Mask
    {
        get
        {
            int m = 0;
            for (int i = 0; i < _checked.Length; ++i) if (_checked[i]) m |= 1 << i;
            return m;
        }
        set
        {
            for (int i = 0; i < _checked.Length; ++i) _checked[i] = (value & (1 << i)) != 0;
            UpdateText();
        }
    }

    public void SetAvailable(int index, bool available)
    {
        if (index < 0 || index >= _available.Length || _available[index] == available) return;
        _available[index] = available;
        UpdateText();
    }

    public int EffectiveMask
    {
        get
        {
            int m = 0;
            for (int i = 0; i < _checked.Length; ++i) if (_checked[i] && _available[i]) m |= 1 << i;
            return m == 0 ? 1 : m;
        }
    }

    private void UpdateText()
    {
        if (base.Items.Count == 0) return;
        int m = EffectiveMask;
        var names = new List<string>();
        for (int i = 0; i < _items.Length; ++i) if ((m & (1 << i)) != 0) names.Add(_items[i]);
        string text = string.Join(", ", names);
        int room = Width - SystemInformation.VerticalScrollBarWidth - LogicalToDeviceUnits(8);
        if (names.Count > 1 && TextRenderer.MeasureText(text, Font).Width > room) text = $"{names[0]} +{names.Count - 1}";
        if (Equals(base.Items[0], text)) return;
        base.Items[0] = text;
        SelectedIndex = 0;
    }

    protected override void OnSizeChanged(EventArgs e) { base.OnSizeChanged(e); UpdateText(); }
    protected override void OnFontChanged(EventArgs e) { base.OnFontChanged(e); UpdateText(); }

    protected override void WndProc(ref Message m)
    {
        if (m.Msg is Win32.WM_LBUTTONDOWN or Win32.WM_LBUTTONDBLCLK)
        {
            if (Enabled)
            {
                Focus();
                ShowPopup();
            }
            return;
        }
        base.WndProc(ref m);
    }

    protected override void OnKeyDown(KeyEventArgs e)
    {
        if (e.KeyCode is Keys.F4 or Keys.Space or Keys.Up or Keys.Down or Keys.PageUp or Keys.PageDown or Keys.Home or Keys.End)
        {
            e.Handled = e.SuppressKeyPress = true;
            if (e.KeyCode is Keys.F4 or Keys.Space || e.Alt) ShowPopup();
            return;
        }
        base.OnKeyDown(e);
    }

    protected override void OnMouseWheel(MouseEventArgs e)
    {
        if (e is HandledMouseEventArgs h) h.Handled = true;
    }

    private void ShowPopup()
    {
        if (_popup != null || Environment.TickCount64 - _closedAt < 250) return;
        var theme = App.Theme.Current;
        var panel = new Panel { BackColor = theme.Window, Font = Font };
        int line = Font.Height + LogicalToDeviceUnits(8);
        int pad = LogicalToDeviceUnits(4);
        int width = Width - 2;
        var boxes = new List<ThemedCheckBox>();
        for (int i = 0; i < _items.Length; ++i)
        {
            int k = i;
            var box = new ThemedCheckBox
            {
                Text = _items[i],
                Checked = _checked[i],
                Enabled = _available[i],
                AutoSize = false,
                Location = new Point(pad + 2, pad + i * line),
                Height = line,
                BackColor = theme.Window,
                ForeColor = theme.Text,
            };
            width = Math.Max(width, TextRenderer.MeasureText(_items[i], Font).Width + LogicalToDeviceUnits(30) + pad * 2);
            box.CheckedChanged += (_, _) =>
            {
                _checked[k] = box.Checked;
                UpdateText();
                CheckedChanged?.Invoke(this, EventArgs.Empty);
            };
            boxes.Add(box);
            panel.Controls.Add(box);
        }
        foreach (var box in boxes) box.Width = width - pad * 2 - 2;
        panel.Size = new Size(width, pad * 2 + _items.Length * line);
        var host = new ToolStripControlHost(panel) { Margin = Padding.Empty, Padding = Padding.Empty, AutoSize = false, Size = panel.Size };
        _popup = new ToolStripDropDown { Padding = new Padding(1), AutoClose = true, DropShadowEnabled = false, BackColor = theme.Line };
        _popup.Items.Add(host);
        _popup.Closed += (_, _) =>
        {
            _closedAt = Environment.TickCount64;
            var p = _popup;
            _popup = null;
            BeginInvoke(() => p?.Dispose());
        };
        _popup.Show(this, new Point(0, Height));
        boxes.FirstOrDefault(b => b.Enabled)?.Focus();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing) _popup?.Dispose();
        base.Dispose(disposing);
    }
}
