using Utagoe.App;

namespace Utagoe.Ui;

internal class UiWindow : Form
{
    protected readonly WindowSpec Spec;

    protected UiWindow(WindowSpec spec)
    {
        Spec = spec;
        if (spec.Scaling == Scaling.None) AutoScaleMode = AutoScaleMode.None;
        else
        {
            AutoScaleDimensions = new SizeF(96F, 96F);
            AutoScaleMode = AutoScaleMode.Dpi;
        }
        if (!spec.ClientSize.IsEmpty) ClientSize = spec.ClientSize;
        if (!spec.MinimumSize.IsEmpty) MinimumSize = spec.MinimumSize;
        Font = new Font("Tahoma", spec.FontSize);
        DoubleBuffered = true;
        FormBorderStyle = spec.Border switch
        {
            Border.Dialog => FormBorderStyle.FixedDialog,
            Border.Sizable => FormBorderStyle.Sizable,
            Border.Header => FormBorderStyle.None,
            _ => FormBorderStyle.FixedSingle,
        };
        ControlBox = spec.Buttons != TitleButtons.None;
        MaximizeBox = spec.Buttons == TitleButtons.All;
        MinimizeBox = spec.Buttons is TitleButtons.All or TitleButtons.CloseMinimize;
        ShowInTaskbar = spec.Taskbar;
        StartPosition = spec.Placement == Placement.CenterScreen ? FormStartPosition.CenterScreen : FormStartPosition.CenterParent;
        KeyPreview = spec.KeyPreview;
        if (spec.Title != null) UiText.Bind(this, spec.Title);
        Icon = Art.AppIcon;
        if (spec.Border != Border.Header) ThemedFrame.Attach(this);
        UiTheme.Bind(this, deep: false);
    }

    protected void Ready() => ScaleToFont();

    protected void ScaleToFont()
    {
        if (Spec.Scaling == Scaling.Vcl) VclScaling.Apply(this);
    }

    protected int S(int v) => (int)Math.Round(v * DeviceDpi / 96f);

    public virtual Rectangle MorphAnchor => RectangleToScreen(ClientRectangle);

    protected override void WndProc(ref Message m)
    {
        if (m.Msg == Win32.WM_SYSCOMMAND && WindowState == FormWindowState.Minimized && ((int)m.WParam & 0xFFF0) == Win32.SC_RESTORE)
        {
            Message copy = m;
            Morph.Appear(this, () => base.WndProc(ref copy));
            m.Result = copy.Result;
            return;
        }
        if (m.Msg == Win32.WM_SYSCOMMAND && WindowState != FormWindowState.Minimized)
        {
            int cmd = (int)m.WParam & 0xFFF0;
            if (cmd == Win32.SC_MINIMIZE && Visible)
            {
                Message later = m;
                Morph.Vanish(this, () => base.WndProc(ref later));
                m.Result = IntPtr.Zero;
                return;
            }
            bool grow = cmd == Win32.SC_MAXIMIZE && WindowState == FormWindowState.Normal;
            bool shrink = cmd == Win32.SC_RESTORE && WindowState == FormWindowState.Maximized;
            if (grow || shrink)
            {
                Message copy = m;
                Morph.Resize(this, () => base.WndProc(ref copy));
                m.Result = copy.Result;
                return;
            }
        }
        base.WndProc(ref m);
    }
}
