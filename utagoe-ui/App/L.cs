using System.Globalization;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text.Json;

namespace Utagoe.App;

internal static class L
{
    public static readonly (string Code, string Name)[] Languages =
    {
        ("en", "English"),
        ("zh", "中文（简体）"),
        ("es", "Español"),
        ("fr", "Français"),
        ("pt", "Português"),
        ("ru", "Русский"),
        ("de", "Deutsch"),
        ("ja", "日本語"),
    };

    private static Dictionary<string, string> _map = new();
    private static Dictionary<string, string> _reverse = new();
    private static readonly ConditionalWeakTable<Control, Source> Sources = new();

    private sealed class Source
    {
        public string Original = "";
        public string Shown = "";
        public string[]? Items;
        public string[]? ShownItems;
    }

    public static string Current { get; private set; } = "en";

    public static event Action? Changed;

    public static string T(string english) =>
        _map.TryGetValue(english, out var t) && t.Length > 0 ? t : english;

    public static string F(string english, params object?[] args) =>
        string.Format(CultureInfo.CurrentCulture, T(english), args);

    public static string[] A(params string[] english) => Array.ConvertAll(english, T);

    public static string Normalize(string? code)
    {
        if (string.IsNullOrWhiteSpace(code)) return Detect();
        code = code.Trim().ToLowerInvariant();
        return Languages.Any(l => l.Code == code) ? code : "en";
    }

    public static string Detect()
    {
        string two = CultureInfo.CurrentUICulture.TwoLetterISOLanguageName.ToLowerInvariant();
        return Languages.Any(l => l.Code == two) ? two : "en";
    }

    public static void Set(string? code)
    {
        string next = Normalize(code);
        var forms = Application.OpenForms.Cast<Form>().ToList();
        foreach (Form f in forms) Apply(f);
        Current = next;
        _map = Load(next);
        _reverse = new Dictionary<string, string>();
        foreach (var (en, tr) in _map)
            if (tr.Length > 0) _reverse.TryAdd(tr, en);
        foreach (Form f in forms) Apply(f);
        Changed?.Invoke();
    }

    private static Dictionary<string, string> Load(string code)
    {
        if (code == "en") return new Dictionary<string, string>();
        using var s = Assembly.GetExecutingAssembly().GetManifestResourceStream($"Utagoe.Resources.Lang.{code}.json");
        if (s == null) return new Dictionary<string, string>();
        return JsonSerializer.Deserialize<Dictionary<string, string>>(s) ?? new Dictionary<string, string>();
    }

    private static string? English(string shown) =>
        _map.ContainsKey(shown) ? shown : _reverse.TryGetValue(shown, out var en) ? en : null;

    public static void Apply(Control root)
    {
        if (root is TextBoxBase or UpDownBase || root.GetType().Name is "TerminalView") return;
        ApplyText(root);
        if (root is ComboBox cb) ApplyItems(cb);
        foreach (Control child in root.Controls) Apply(child);
    }

    private static void ApplyText(Control c)
    {
        string cur = c.Text;
        if (string.IsNullOrEmpty(cur)) return;
        Sources.TryGetValue(c, out var src);
        string? original = src != null && cur == src.Shown ? src.Original : English(cur);
        if (original == null) return;
        string shown = T(original);
        if (src == null) Sources.Add(c, src = new Source());
        src.Original = original;
        src.Shown = shown;
        if (cur != shown) c.Text = shown;
    }

    private static void ApplyItems(ComboBox cb)
    {
        if (cb.Items.Count == 0) return;
        var cur = cb.Items.Cast<object>().Select(o => o as string).ToArray();
        if (cur.Any(s => s == null)) return;
        Sources.TryGetValue(cb, out var src);
        string[]? original = src?.ShownItems != null && src.ShownItems.SequenceEqual(cur!) ? src.Items : null;
        if (original == null)
        {
            var mapped = cur.Select(s => English(s!) ?? s!).ToArray();
            if (mapped.SequenceEqual(cur!) && !cur.Any(s => _map.ContainsKey(s!))) return;
            original = mapped;
        }
        var shown = original.Select(T).ToArray();
        if (src == null) Sources.Add(cb, src = new Source());
        src.Items = original;
        src.ShownItems = shown;
        if (shown.SequenceEqual(cur!)) return;
        int index = cb.SelectedIndex;
        cb.BeginUpdate();
        for (int i = 0; i < shown.Length; i++)
            if (!Equals(cb.Items[i], shown[i])) cb.Items[i] = shown[i];
        if (cb.SelectedIndex != index) cb.SelectedIndex = index;
        cb.EndUpdate();
    }
}
