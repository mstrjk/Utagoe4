namespace Utagoe.Ui;

internal enum Border { Fixed, Dialog, Sizable, Header }

internal enum TitleButtons { None, Close, CloseMinimize, All }

internal enum Placement { CenterParent, CenterScreen }

internal enum Scaling { Vcl, Dpi, None }

internal sealed record WindowSpec
{
    public Size ClientSize { get; init; }
    public Size MinimumSize { get; init; }
    public Border Border { get; init; } = Border.Fixed;
    public TitleButtons Buttons { get; init; } = TitleButtons.Close;
    public bool Taskbar { get; init; }
    public Placement Placement { get; init; } = Placement.CenterParent;
    public float FontSize { get; init; } = 9f;
    public Scaling Scaling { get; init; } = Scaling.Vcl;
    public Func<string>? Title { get; init; }
    public bool KeyPreview { get; init; }
}
