using Utagoe.App;

namespace Utagoe.Ui;

internal static class UiTheme
{
    private static readonly List<(WeakReference<Control> Target, bool Deep)> Bound = new();

    private static Transition? s_switch;

    static UiTheme()
    {
        Theme.Changing += () => s_switch = Transition.Begin(Transition.OpenWindows());
        Theme.Changed += Refresh;
    }

    public static T Bind<T>(T control, bool deep = true) where T : Control
    {
        Style(control, Theme.Current, deep);
        Bound.Add((new WeakReference<Control>(control), deep));
        return control;
    }

    private static void Refresh()
    {
        Bound.RemoveAll(b => !b.Target.TryGetTarget(out var c) || c.IsDisposed);
        var t = Theme.Current;
        foreach (var (target, deep) in Bound.ToArray())
            if (target.TryGetTarget(out var c)) Style(c, t, deep);
        foreach (Form f in Application.OpenForms.Cast<Form>().ToList()) f.Invalidate(true);
        s_switch?.End(fade: true);
        s_switch = null;
    }

    private static void Style(Control c, Theme t, bool deep)
    {
        switch (c)
        {
            case Form f when f is not XpTerminalWindow and not InfoTip:
                f.BackColor = t.Window;
                f.ForeColor = t.Text;
                break;
            case TextBoxBase or ComboBox or NumericUpDown or ListBox:
                c.BackColor = t.Input;
                c.ForeColor = t.InputText;
                if (c is TextBox tbx) tbx.BorderStyle = tbx.Parent is UpDownBase ? BorderStyle.None : BorderStyle.FixedSingle;
                if (c is UpDownBase ud) ud.BorderStyle = BorderStyle.FixedSingle;
                if (c is ComboBox cb) cb.FlatStyle = FlatStyle.Flat;
                if (c is TextBox or ComboBox or NumericUpDown) FocusLine.Attach(c);
                break;
            case TabPage tp:
                tp.UseVisualStyleBackColor = false;
                tp.BackColor = t.Window;
                tp.ForeColor = t.Text;
                break;
            case Button b:
                b.ForeColor = t.Text;
                break;
            case LinkLabel ll:
                ll.LinkColor = ll.ActiveLinkColor = ll.VisitedLinkColor = t.Link;
                ll.DisabledLinkColor = t.DisabledLink;
                break;
            case TrackBar tb:
                tb.BackColor = t.Window;
                break;
            case Control tagged when "muted".Equals(tagged.Tag):
                tagged.ForeColor = t.Muted;
                break;
            case Control linked when "link".Equals(linked.Tag):
                linked.ForeColor = t.Link;
                break;
        }
        if (c is TerminalView or XpTerminalWindow) { if (c is XpTerminalWindow x) x.ApplyTheme(); return; }
        if (deep) foreach (Control child in c.Controls) Style(child, t, true);
    }
}
