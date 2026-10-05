using System.Globalization;
using System.Reflection;
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
    public static string Current { get; private set; } = "en";

    public static event Action? Changing;

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
        if (next == Current && _map.Count > 0) return;
        Changing?.Invoke();
        Current = next;
        _map = Load(Current);
        Changed?.Invoke();
    }

    private static Dictionary<string, string> Load(string code)
    {
        if (code == "en") return new Dictionary<string, string>();
        using var s = Assembly.GetExecutingAssembly().GetManifestResourceStream($"Utagoe.Resources.Lang.{code}.json");
        if (s == null) return new Dictionary<string, string>();
        return JsonSerializer.Deserialize<Dictionary<string, string>>(s) ?? new Dictionary<string, string>();
    }
}
