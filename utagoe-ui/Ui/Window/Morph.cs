using static Utagoe.Ui.Win32;

namespace Utagoe.Ui;

internal static class Morph
{
    public static int LastFrames { get; private set; }
    public static double LastWorstFrameMs { get; private set; }
    public static double LastTotalMs { get; private set; }
    public static double LastChangeMs { get; private set; }
    public static double LastAnimationCpuMs { get; private set; }

    private static readonly float[] WindowsCurve =
        { 0f, 0f, 0f, .007f, .026f, .063f, .147f, .362f, .717f, .850f, .914f, .950f, .976f, .991f, 1f, 1f };
    private const int CurveStepMs = 20;
    private const int FlatMs = 60;
    private const float SmallFade = 0.3f;
    private static int Duration => (WindowsCurve.Length - 1) * CurveStepMs;

    private static float Progress(double ms)
    {
        double x = Math.Max(0, ms) / CurveStepMs;
        int i = (int)x;
        if (i >= WindowsCurve.Length - 1) return 1f;
        return WindowsCurve[i] + (WindowsCurve[i + 1] - WindowsCurve[i]) * (float)(x - i);
    }

    private sealed class Shape
    {
        public Rectangle SmallBounds, SmallAnchor, BigBounds, BigAnchor, SmallSource;

        private Rectangle Map(Rectangle r, Rectangle a)
        {
            float sx = a.Width / (float)BigAnchor.Width, sy = a.Height / (float)BigAnchor.Height;
            return Rectangle.FromLTRB(
                (int)Math.Round(a.X + (r.Left - BigAnchor.X) * sx), (int)Math.Round(a.Y + (r.Top - BigAnchor.Y) * sy),
                (int)Math.Round(a.X + (r.Right - BigAnchor.X) * sx), (int)Math.Round(a.Y + (r.Bottom - BigAnchor.Y) * sy));
        }

        public Rectangle SmallInBig()
        {
            float kx = BigAnchor.Width / (float)SmallAnchor.Width, ky = BigAnchor.Height / (float)SmallAnchor.Height;
            return Rectangle.FromLTRB(
                (int)Math.Round(BigAnchor.X + (SmallBounds.Left - SmallAnchor.X) * kx), (int)Math.Round(BigAnchor.Y + (SmallBounds.Top - SmallAnchor.Y) * ky),
                (int)Math.Round(BigAnchor.X + (SmallBounds.Right - SmallAnchor.X) * kx), (int)Math.Round(BigAnchor.Y + (SmallBounds.Bottom - SmallAnchor.Y) * ky));
        }

        public void Draw(Stage stage, IntPtr big, IntPtr small, float p)
        {
            Rectangle anchor = Lerp(SmallAnchor, BigAnchor, p);
            Rectangle smallInBig = SmallInBig();
            Rectangle start = Rectangle.Intersect(smallInBig, BigBounds);
            Rectangle crop = Lerp(start, BigBounds, p);
            var source = new Rectangle(crop.X - BigBounds.X, crop.Y - BigBounds.Y, crop.Width, crop.Height);
            stage.Crop(big, source, Map(crop, anchor), 255);
            float fade = Math.Clamp(1f - p / SmallFade, 0f, 1f);
            if (small == IntPtr.Zero) return;
            stage.Crop(small, SmallSource.IsEmpty ? new Rectangle(Point.Empty, SmallBounds.Size) : SmallSource, Map(smallInBig, anchor), (byte)Math.Round(255 * fade));
        }
    }

    private sealed class SmallLook
    {
        public Rectangle Visible, Anchor;
    }

    private static readonly System.Runtime.CompilerServices.ConditionalWeakTable<Form, SmallLook> s_small = new();

    private sealed class BigLook
    {
        public required Picture Picture;
        public Rectangle Visible, Anchor, Work;
        public App.Theme? Theme;
        public string? Language;

        public bool Fits(Rectangle work) => Work == work && ReferenceEquals(Theme, App.Theme.Current) && Language == App.L.Current;
    }

    private static readonly System.Runtime.CompilerServices.ConditionalWeakTable<Form, BigLook> s_big = new();

    private static void Keep(Form form, Picture picture, Rectangle visible, Rectangle anchor, Rectangle work)
    {
        if (s_big.TryGetValue(form, out BigLook? old)) old.Picture.Dispose();
        if (form.IsDisposed) { picture.Dispose(); s_big.Remove(form); return; }
        s_big.AddOrUpdate(form, new BigLook { Picture = picture, Visible = visible, Anchor = anchor, Work = work, Theme = App.Theme.Current, Language = App.L.Current });
        form.Disposed -= Forget;
        form.Disposed += Forget;
    }

    private static void Forget(object? sender, EventArgs e)
    {
        if (sender is Form form && s_big.TryGetValue(form, out BigLook? old))
        {
            old.Picture.Dispose();
            s_big.Remove(form);
        }
    }

    public static void Resize(UiWindow form, Action change)
    {
        if (form.WindowState == FormWindowState.Normal) Grow(form, change);
        else Shrink(form, change);
    }

    private static void Grow(UiWindow form, Action change)
    {
        var clock = System.Diagnostics.Stopwatch.StartNew();
        Rectangle bounds = form.Bounds, anchor = form.MorphAnchor, visible = VisibleBounds(form.Handle);
        if (visible.IsEmpty) visible = bounds;
        using Bitmap? shot = Capture(form, visible);
        if (shot == null || anchor.Width <= 0 || anchor.Height <= 0)
        {
            Plain(form, change);
            return;
        }
        s_small.AddOrUpdate(form, new SmallLook { Visible = Relative(visible, bounds.Location), Anchor = Relative(anchor, bounds.Location) });
        var picture = new Picture(shot, visible);
        Rectangle work = Screen.FromHandle(form.Handle).WorkingArea;
        var stage = new Stage(work);
        if (s_big.TryGetValue(form, out BigLook? cached))
        {
            if (cached.Fits(work))
            {
                GrowFromPicture(form, change, cached, stage, picture, visible, anchor);
                return;
            }
            Forget(form, EventArgs.Empty);
        }
        GatherBelow(form, stage);
        IntPtr big = stage.Add(form.Handle, visible, 0);
        IntPtr small = stage.Add(picture.Handle, visible, 255);
        GatherAbove(stage);
        stage.Show();
        DwmFlush();
        Change(form, change);
        {
            if (form.IsDisposed || form.WindowState != FormWindowState.Maximized) { Finish(form, stage, picture); return; }
            var shape = new Shape { SmallBounds = visible, SmallAnchor = anchor, BigBounds = form.Bounds, BigAnchor = form.MorphAnchor };
            if (shape.BigAnchor.Width <= 0 || shape.BigAnchor.Height <= 0) { Finish(form, stage, picture); return; }
            Run(form, Math.Min(clock.Elapsed.TotalMilliseconds, FlatMs), Duration,
                t =>
                {
                    float p = Progress(t);
                    shape.Draw(stage, big, small, p);
                    DrawCompanions(stage, p);
                },
                () => Finish(form, stage, picture));
        }
    }

    private static void GrowFromPicture(UiWindow form, Action change, BigLook cached, Stage stage, Picture picture, Rectangle visible, Rectangle anchor)
    {
        GatherBelow(form, stage);
        IntPtr old = stage.Add(cached.Picture.Handle, visible, 0);
        IntPtr real = stage.Add(form.Handle, visible, 0);
        IntPtr small = stage.Add(picture.Handle, visible, 255);
        GatherAbove(stage);
        stage.Show();
        DwmFlush();
        SetDwm(form.Handle, DWMWA_CLOAK, 1);
        var guess = new Shape { SmallBounds = visible, SmallAnchor = anchor, BigBounds = cached.Visible, BigAnchor = cached.Anchor };
        var live = new Live { Shape = guess };
        Run(form, 0, Duration, t =>
        {
            float p = Progress(t);
            guess.Draw(stage, old, small, p);
            if (live.Ready) live.Shape.Draw(stage, real, IntPtr.Zero, p);
            DrawCompanions(stage, p);
        }, () => Finish(form, stage, picture), next => !live.Ready && next >= Duration);
        Change(form, change);
        if (!form.IsDisposed && form.WindowState == FormWindowState.Maximized)
        {
            Rectangle a = form.MorphAnchor;
            if (a.Width > 0 && a.Height > 0)
                live.Shape = new Shape { SmallBounds = visible, SmallAnchor = anchor, BigBounds = form.Bounds, BigAnchor = a };
        }
        live.Ready = true;
    }

    private static void Shrink(UiWindow form, Action change)
    {
        Rectangle restore = form.RestoreBounds, bigVisible = Rectangle.Intersect(VisibleBounds(form.Handle), Screen.FromHandle(form.Handle).WorkingArea);
        if (!s_small.TryGetValue(form, out SmallLook? look) || restore.IsEmpty || bigVisible.IsEmpty)
        {
            Plain(form, change);
            return;
        }
        Rectangle smallBounds = Absolute(look.Visible, restore.Location);
        var shape = new Shape
        {
            BigBounds = bigVisible,
            BigAnchor = form.MorphAnchor,
            SmallBounds = smallBounds,
            SmallAnchor = Absolute(look.Anchor, restore.Location),
            SmallSource = new Rectangle(look.Visible.Location, smallBounds.Size),
        };
        if (shape.BigAnchor.Width <= 0 || shape.BigAnchor.Height <= 0)
        {
            Plain(form, change);
            return;
        }
        using Bitmap? shot = Capture(form, bigVisible);
        if (shot == null)
        {
            Plain(form, change);
            return;
        }
        var picture = new Picture(shot, bigVisible);
        var stage = new Stage(Screen.FromHandle(form.Handle).WorkingArea);
        GatherBelow(form, stage);
        IntPtr big = stage.Add(picture.Handle, bigVisible, 255);
        IntPtr small = stage.Add(form.Handle, smallBounds, 0);
        GatherAbove(stage);
        stage.Show();
        DwmFlush();
        var live = new Live { Shape = shape };
        Run(form, 0, Duration,
            t =>
            {
                float p = 1f - Progress(t);
                live.Shape.Draw(stage, big, small, p);
                DrawCompanions(stage, p);
            },
            () =>
            {
                Finish(form, stage, null);
                Keep(form, picture, bigVisible, shape.BigAnchor, Screen.FromHandle(form.Handle).WorkingArea);
            },
            next => !live.Ready && 1f - Progress(next) < SmallFade);
        Change(form, change);
        AfterIdle(() =>
        {
            if (!form.IsDisposed)
            {
                RedrawWindow(form.Handle, IntPtr.Zero, IntPtr.Zero, RDW_ALLCHILDREN | RDW_UPDATENOW);
                Rectangle bounds = form.Bounds, visible = VisibleBounds(form.Handle), anchor = form.MorphAnchor;
                if (!visible.IsEmpty && anchor.Width > 0 && anchor.Height > 0)
                    live.Shape = new Shape
                    {
                        BigBounds = shape.BigBounds,
                        BigAnchor = shape.BigAnchor,
                        SmallBounds = visible,
                        SmallAnchor = anchor,
                        SmallSource = new Rectangle(visible.X - bounds.X, visible.Y - bounds.Y, visible.Width, visible.Height),
                    };
            }
            live.Ready = true;
        });
    }

    private sealed class Live
    {
        public volatile Shape Shape = null!;
        public volatile bool Ready;
    }

    private static Rectangle Relative(Rectangle r, Point origin) => new(r.X - origin.X, r.Y - origin.Y, r.Width, r.Height);
    private static Rectangle Absolute(Rectangle r, Point origin) => new(r.X + origin.X, r.Y + origin.Y, r.Width, r.Height);

    private static Bitmap? Capture(Form form, Rectangle area)
    {
        if (area.Width <= 0 || area.Height <= 0) return null;
        if (Screen.FromRectangle(area).WorkingArea.Contains(area) && !Covered(form, area))
        {
            var bmp = new Bitmap(area.Width, area.Height, System.Drawing.Imaging.PixelFormat.Format24bppRgb);
            using (var g = Graphics.FromImage(bmp)) g.CopyFromScreen(area.Location, Point.Empty, area.Size);
            return bmp;
        }
        using Bitmap? full = Transition.Shot(form);
        if (full == null) return null;
        Rectangle part = Rectangle.Intersect(new Rectangle(area.X - form.Left, area.Y - form.Top, area.Width, area.Height), new Rectangle(Point.Empty, full.Size));
        if (part.Width <= 0 || part.Height <= 0) return null;
        return full.Clone(part, full.PixelFormat);
    }

    private static bool Covered(Form form, Rectangle area)
    {
        if (!form.Visible || IsCloaked(form.Handle)) return true;
        for (IntPtr h = GetWindow(form.Handle, 3); h != IntPtr.Zero; h = GetWindow(h, 3))
        {
            if (!IsWindowVisible(h) || IsCloaked(h)) continue;
            if (VisibleBounds(h).IntersectsWith(area)) return true;
        }
        return false;
    }

    private static void Change(Form form, Action change)
    {
        SetDwm(form.Handle, DWMWA_CLOAK, 1);
        SetDwm(form.Handle, DWMWA_TRANSITIONS_FORCEDISABLED, 1);
        var changeClock = System.Diagnostics.Stopwatch.StartNew();
        try { change(); }
        finally { SetDwm(form.Handle, DWMWA_TRANSITIONS_FORCEDISABLED, 0); }
        LastChangeMs = changeClock.Elapsed.TotalMilliseconds;
        RedrawWindow(form.Handle, IntPtr.Zero, IntPtr.Zero, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }

    private static void Plain(Form form, Action change)
    {
        Change(form, change);
        AfterIdle(() => Finish(form, null, null));
    }

    private static void AfterIdle(Action then)
    {
        bool started = false;
        var fallback = new System.Windows.Forms.Timer { Interval = 500 };
        void Go()
        {
            if (started) return;
            started = true;
            Application.Idle -= OnIdle;
            fallback.Stop();
            fallback.Dispose();
            then();
        }
        void OnIdle(object? sender, EventArgs e) => Go();
        Application.Idle += OnIdle;
        fallback.Tick += (_, _) => Go();
        fallback.Start();
    }

    public static void Animate(Form form, int duration, Action<double> frame, Action done) => Run(form, 0, duration, frame, done);

    private enum Role { Stay, Leave, Arrive }

    private sealed class Companion
    {
        public required Form Form;
        public IntPtr Thumb, Below;
        public Role Role;
        public Action? Then;
    }

    private static Stage? s_stage;
    private static readonly List<Companion> s_companions = new();

    private static void GatherBelow(Form owner, Stage stage)
    {
        s_stage = stage;
        lock (s_companions)
        {
            s_companions.Clear();
            foreach (Form f in owner.OwnedForms)
                if (f.Visible && f.IsHandleCreated && f.WindowState != FormWindowState.Minimized)
                    s_companions.Add(new Companion { Form = f, Below = stage.Add(f.Handle, f.Bounds, 0), Role = Role.Stay });
        }
    }

    private static void GatherAbove(Stage stage)
    {
        lock (s_companions)
            foreach (var c in s_companions) c.Thumb = stage.Add(c.Form.Handle, c.Form.Bounds, 255);
    }

    public static bool Join(Form f, bool leaving, Action then)
    {
        var stage = s_stage;
        if (stage == null || f.IsDisposed) return false;
        Companion? c;
        lock (s_companions) c = s_companions.FirstOrDefault(x => x.Form == f);
        if (leaving)
        {
            if (c == null || !f.IsHandleCreated) return false;
            c.Then = then;
            c.Role = Role.Leave;
            SetDwm(f.Handle, DWMWA_CLOAK, 1);
            return true;
        }
        if (f.IsHandleCreated) SetDwm(f.Handle, DWMWA_CLOAK, 1);
        then();
        if (!f.IsHandleCreated) return true;
        SetDwm(f.Handle, DWMWA_CLOAK, 1);
        RedrawWindow(f.Handle, IntPtr.Zero, IntPtr.Zero, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
        lock (s_companions)
        {
            if (c != null) { stage.Remove(c.Thumb); s_companions.Remove(c); }
            s_companions.Add(new Companion { Form = f, Thumb = stage.Add(f.Handle, f.Bounds, 0), Role = Role.Arrive });
        }
        return true;
    }

    private static void DrawCompanions(Stage stage, float p)
    {
        if (stage != s_stage) return;
        Companion[] list;
        lock (s_companions) list = s_companions.ToArray();
        float x = Math.Clamp(p / 0.6f, 0f, 1f);
        byte fading = (byte)Math.Round(255 * (1 - x * x * (3 - 2 * x)));
        foreach (var c in list)
        {
            if (c.Form.IsDisposed) continue;
            stage.Move(c.Thumb, c.Form.Bounds, c.Role switch { Role.Arrive => fading, Role.Leave => (byte)0, _ => (byte)255 });
            if (c.Below != IntPtr.Zero) stage.Move(c.Below, c.Form.Bounds, c.Role == Role.Leave ? (byte)255 : (byte)0);
        }
    }

    private static List<Companion> ReleaseCompanions(Stage? stage)
    {
        var leaving = new List<Companion>();
        if (stage == null || stage != s_stage) return leaving;
        s_stage = null;
        Companion[] list;
        lock (s_companions)
        {
            list = s_companions.ToArray();
            s_companions.Clear();
        }
        foreach (var c in list)
        {
            if (c.Role == Role.Leave && !c.Form.IsDisposed) leaving.Add(c);
            else if (c.Role == Role.Arrive && !c.Form.IsDisposed && c.Form.IsHandleCreated) SetDwm(c.Form.Handle, DWMWA_CLOAK, 0);
        }
        return leaving;
    }

    private const int LeaveMs = 200;

    private static void FadeAway(Form form, Stage stage, Picture? picture, List<Companion> leaving)
    {
        stage.KeepOnly(leaving.Select(c => c.Below));
        SetWindowPos(stage.Handle, form.Handle, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        Run(form, 0, LeaveMs, t =>
        {
            float x = (float)Math.Clamp(t / LeaveMs, 0, 1);
            byte alpha = (byte)Math.Round(255 * (1 - x * x * (3 - 2 * x)));
            foreach (var c in leaving)
                if (!c.Form.IsDisposed) stage.Move(c.Below, c.Form.Bounds, alpha);
        }, () =>
        {
            foreach (var c in leaving)
            {
                c.Then?.Invoke();
                if (!c.Form.IsDisposed && c.Form.IsHandleCreated) SetDwm(c.Form.Handle, DWMWA_CLOAK, 0);
            }
            stage.Dispose();
            picture?.Dispose();
        });
    }

    private static void Run(Form form, double offset, int duration, Action<double> frame, Action done, Func<double, bool>? hold = null)
    {
        var process = System.Diagnostics.Process.GetCurrentProcess();
        var cpuStart = process.TotalProcessorTime;
        new Thread(() =>
        {
            var run = System.Diagnostics.Stopwatch.StartNew();
            double worst = 0, last = 0, t = Math.Min(offset, duration), previous = 0;
            int frames = 0;
            while (true)
            {
                double elapsed = run.Elapsed.TotalMilliseconds;
                double next = Math.Min(t + elapsed - previous, duration);
                previous = elapsed;
                if (hold == null || !hold(next)) t = next;
                frame(t);
                DwmFlush();
                frames++;
                double now = run.Elapsed.TotalMilliseconds;
                worst = Math.Max(worst, now - last);
                last = now;
                if (t >= duration) break;
            }
            LastFrames = frames;
            LastWorstFrameMs = worst;
            LastTotalMs = run.Elapsed.TotalMilliseconds;
            process.Refresh();
            LastAnimationCpuMs = (process.TotalProcessorTime - cpuStart).TotalMilliseconds;
            try { form.BeginInvoke(done); } catch (InvalidOperationException) { }
        }) { IsBackground = true, Priority = ThreadPriority.AboveNormal }.Start();
    }

    private static void Finish(Form form, Stage? stage, Picture? picture)
    {
        var leaving = ReleaseCompanions(stage);
        if (!form.IsDisposed && form.IsHandleCreated)
        {
            SetDwm(form.Handle, DWMWA_CLOAK, 0);
            foreach (var c in leaving)
                if (!c.Form.IsDisposed && c.Form.IsHandleCreated) SetDwm(c.Form.Handle, DWMWA_CLOAK, 1);
            DwmFlush();
            SetWindowPos(form.Handle, IntPtr.Zero, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            form.Activate();
        }
        if (stage != null && leaving.Count > 0 && !form.IsDisposed)
        {
            FadeAway(form, stage, picture, leaving);
            return;
        }
        foreach (var c in leaving)
        {
            c.Then?.Invoke();
            if (!c.Form.IsDisposed && c.Form.IsHandleCreated) SetDwm(c.Form.Handle, DWMWA_CLOAK, 0);
        }
        stage?.Dispose();
        picture?.Dispose();
    }

    private static Point? s_taskbarPoint;

    private static Point TaskbarPoint(Screen screen, Point click)
    {
        if (screen.Bounds.Contains(click) && !screen.WorkingArea.Contains(click))
        {
            s_taskbarPoint = click;
            return click;
        }
        if (s_taskbarPoint is { } last && screen.Bounds.Contains(last)) return last;
        return new Point(screen.WorkingArea.Left + screen.WorkingArea.Width / 2, screen.WorkingArea.Bottom);
    }

    public static void Vanish(Form form, Action change)
    {
        var screen = Screen.FromHandle(form.Handle);
        Point origin = TaskbarPoint(screen, Cursor.Position);
        Rectangle from = form.Bounds;
        var size = new Size(Math.Max(1, from.Width / 8), Math.Max(1, from.Height / 8));
        Rectangle to = new(origin.X - size.Width / 2, origin.Y - size.Height / 2, size.Width, size.Height);
        Rectangle area = Rectangle.Intersect(Rectangle.Union(from, to), screen.Bounds);
        if (area == screen.Bounds) area.Height -= 1;
        var stage = new Stage(area);
        IntPtr thumb = stage.Add(form.Handle, from, 255);
        stage.Show();
        DwmFlush();
        SetDwm(form.Handle, DWMWA_CLOAK, 1);
        Run(form, 0, Duration, t =>
        {
            float e = Progress(t);
            stage.Move(thumb, Lerp(from, to, e), (byte)Math.Round(255 * Math.Min(1f, (1f - e) / 0.4f)));
        }, () =>
        {
            if (!form.IsDisposed)
            {
                SetDwm(form.Handle, DWMWA_TRANSITIONS_FORCEDISABLED, 1);
                try { change(); }
                finally { SetDwm(form.Handle, DWMWA_TRANSITIONS_FORCEDISABLED, 0); }
                SetDwm(form.Handle, DWMWA_CLOAK, 0);
            }
            stage.Dispose();
        });
    }

    public static void Appear(Form form, Action change)
    {
        Point click = Cursor.Position;
        SetDwm(form.Handle, DWMWA_CLOAK, 1);
        SetDwm(form.Handle, DWMWA_TRANSITIONS_FORCEDISABLED, 1);
        try { change(); }
        finally { SetDwm(form.Handle, DWMWA_TRANSITIONS_FORCEDISABLED, 0); }
        RedrawWindow(form.Handle, IntPtr.Zero, IntPtr.Zero, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
        {
            if (form.IsDisposed || form.WindowState == FormWindowState.Minimized) { Finish(form, null, null); return; }
            Rectangle to = form.Bounds;
            var screen = Screen.FromHandle(form.Handle);
            Point origin = TaskbarPoint(screen, click);
            var size = new Size(Math.Max(1, to.Width / 8), Math.Max(1, to.Height / 8));
            Rectangle from = new(origin.X - size.Width / 2, origin.Y - size.Height / 2, size.Width, size.Height);
            Rectangle area = Rectangle.Intersect(Rectangle.Union(to, from), screen.Bounds);
            if (area == screen.Bounds) area.Height -= 1;
            var stage = new Stage(area);
            IntPtr thumb = stage.Add(form.Handle, from, 0);
            stage.Show();
            Run(form, 0, AppearMs, t =>
            {
                float x = (float)(t / AppearMs);
                float e = 1 - (1 - x) * (1 - x) * (1 - x);
                stage.Move(thumb, Lerp(from, to, e), (byte)Math.Round(255 * Math.Min(1f, e / 0.5f)));
            }, () => Finish(form, stage, null));
        }
    }

    private const int AppearMs = 220;

    private static Rectangle Lerp(Rectangle a, Rectangle b, float e) => Rectangle.FromLTRB(
        (int)Math.Round(a.Left + (b.Left - a.Left) * e), (int)Math.Round(a.Top + (b.Top - a.Top) * e),
        (int)Math.Round(a.Right + (b.Right - a.Right) * e), (int)Math.Round(a.Bottom + (b.Bottom - a.Bottom) * e));
}
