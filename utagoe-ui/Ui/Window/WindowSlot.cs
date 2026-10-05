namespace Utagoe.Ui;

internal sealed class WindowSlot<T> where T : Form
{
    private T? _open;

    public T? Current => _open is { IsDisposed: false } ? _open : null;

    public T Show(IWin32Window owner, Func<T> make)
    {
        if (Current is { } open)
        {
            if (open.WindowState == FormWindowState.Minimized) open.WindowState = FormWindowState.Normal;
            open.Activate();
            return open;
        }
        var f = Hold(make());
        f.Show(owner);
        return f;
    }

    public T Hold(T f)
    {
        _open = f;
        f.FormClosed += (_, _) => { if (_open == f) _open = null; };
        return f;
    }
}
