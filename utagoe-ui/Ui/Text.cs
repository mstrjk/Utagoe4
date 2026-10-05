using Utagoe.App;

namespace Utagoe.Ui;

internal static class UiText
{
    private static readonly List<(WeakReference<object> Target, Action<object> Apply)> Bound = new();

    private static Transition? s_switch;

    static UiText()
    {
        L.Changing += () => s_switch = Transition.Begin(Transition.OpenWindows());
        L.Changed += Refresh;
    }

    public static T Bind<T>(T control, Func<string> text) where T : Control
    {
        control.Text = text();
        Add(control, o => ((Control)o).Text = text());
        return control;
    }

    public static T Key<T>(T control, string key) where T : Control => Bind(control, () => L.T(key));

    public static void Tip(ToolTip tips, Control control, Func<string> text)
    {
        tips.SetToolTip(control, text());
        Add(control, o => tips.SetToolTip((Control)o, text()));
    }

    public static void Items(ComboBox box, Func<string[]> items)
    {
        Fill(box, items());
        Add(box, o => Fill((ComboBox)o, items()));
    }

    public static void Page(TabPage page, Func<string> text)
    {
        page.Text = text();
        Add(page, o =>
        {
            var p = (TabPage)o;
            if (p.Text == text()) return;
            p.Text = text();
            (p.Parent as ThemedTabControl)?.Refit();
        });
    }

    public static void OnChange(Control owner, Action refresh)
    {
        Add(owner, _ => refresh());
    }

    private static void Fill(ComboBox box, string[] items)
    {
        int index = box.SelectedIndex;
        box.BeginUpdate();
        if (box.Items.Count == items.Length)
            for (int i = 0; i < items.Length; i++) { if (!Equals(box.Items[i], items[i])) box.Items[i] = items[i]; }
        else
        {
            box.Items.Clear();
            box.Items.AddRange(items);
        }
        if (index < box.Items.Count && box.SelectedIndex != index) box.SelectedIndex = index;
        box.EndUpdate();
    }

    private static void Add(object target, Action<object> apply) => Bound.Add((new WeakReference<object>(target), apply));

    private static void Refresh()
    {
        Bound.RemoveAll(b => !b.Target.TryGetTarget(out var t) || t is Control { IsDisposed: true });
        foreach (var (target, apply) in Bound.ToArray())
            if (target.TryGetTarget(out var t)) apply(t);
        s_switch?.End(fade: false);
        s_switch = null;
    }
}
