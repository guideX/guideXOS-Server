using System.Diagnostics;
using System.Drawing.Drawing2D;
using Astronomy.Core;
using StellarNavigator.Scene;

namespace StellarNavigator.Gui;

internal sealed class SolarMapControl : Control
{
    private static readonly Dictionary<string, Color> Colors = new(StringComparer.Ordinal)
    {
        ["sun"] = Color.FromArgb(255, 204, 72),
        ["mercury"] = Color.FromArgb(176, 181, 190),
        ["venus"] = Color.FromArgb(238, 171, 104),
        ["earth"] = Color.FromArgb(76, 160, 255),
        ["moon"] = Color.FromArgb(203, 209, 221),
        ["mars"] = Color.FromArgb(237, 102, 75),
        ["jupiter"] = Color.FromArgb(224, 178, 126),
        ["saturn"] = Color.FromArgb(220, 207, 133),
        ["uranus"] = Color.FromArgb(102, 213, 215),
        ["neptune"] = Color.FromArgb(92, 124, 246),
        ["spacecraft"] = Color.FromArgb(255, 255, 255),
    };
    private static readonly Color[] SegmentColors =
    [
        Color.FromArgb(93, 205, 255), Color.FromArgb(248, 194, 88),
        Color.FromArgb(196, 150, 255), Color.FromArgb(120, 228, 175),
        Color.FromArgb(255, 143, 133), Color.FromArgb(119, 180, 255),
        Color.FromArgb(255, 185, 224), Color.FromArgb(225, 232, 135),
    ];

    private readonly Dictionary<string, Pen> _trailPens = new(StringComparer.Ordinal);
    private readonly Font _labelFont = new("Segoe UI", 9F, FontStyle.Regular, GraphicsUnit.Point);
    private readonly Font _smallFont = new("Segoe UI", 8F, FontStyle.Regular, GraphicsUnit.Point);
    private readonly Font _headingFont = new("Segoe UI Semibold", 10F, FontStyle.Bold, GraphicsUnit.Point);
    private NavigationScene? _scene;
    private bool _dragging;
    private bool _dragMoved;
    private Point _lastMouse;
    private Point _dragStart;
    private string? _selectedId;
    private string? _selectedManeuverId;

    public Camera2D Camera { get; } = new();
    public string? Diagnostic { get; private set; }
    public double LastPaintMilliseconds { get; private set; }
    public event Action<SceneObject?>? SelectionChanged;
    public event Action<double>? PaintMeasured;

    public SolarMapControl()
    {
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint |
            ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw | ControlStyles.Selectable, true);
        BackColor = Color.FromArgb(8, 13, 23);
        ForeColor = Color.WhiteSmoke;
        TabStop = true;
        AccessibleName = "Stellar Navigator solar system map";
        AccessibleDescription = "Physical-scale top-down reference-frame X/Y projection. Click a marker to select it; drag to pan and use the mouse wheel to zoom at the pointer.";
        AccessibleRole = AccessibleRole.Pane;
        MinimumSize = new Size(400, 300);
        foreach (var (id, color) in Colors)
            _trailPens[id] = new Pen(Color.FromArgb(112, color), 1F);
    }

    public NavigationScene? Scene => _scene;
    public string? SelectedId => _selectedId;
    internal void SetSelectedManeuver(string? id)
    {
        _selectedManeuverId = id;
        Invalidate();
    }
    internal bool ShowTrajectory = true;
    internal bool ShowManeuvers = true;

    public Viewport MapViewport => new(58, 46, Math.Max(1, ClientSize.Width - 82), Math.Max(1, ClientSize.Height - 90));

    public void SetScene(NavigationScene scene)
    {
        _scene = scene;
        Diagnostic = null;
        if (_selectedId is null || scene.FindObject(_selectedId) is null)
            _selectedId = "earth";
        Invalidate();
        SelectionChanged?.Invoke(scene.FindObject(_selectedId));
    }

    public void SetDiagnostic(string? message)
    {
        Diagnostic = message;
        Invalidate();
    }

    public void Select(string id)
    {
        _selectedId = id;
        Invalidate();
        SelectionChanged?.Invoke(_scene?.FindObject(id));
    }

    public void FitSolarSystem()
    {
        if (_scene is null)
            return;
        IEnumerable<Vector3D> positions = _scene.Objects.Select(o => o.State.Position);
        if (_scene.Trajectory is { } trajectory)
            positions = positions.Concat(trajectory.Samples.Select(p => p.State.Position))
                .Append(trajectory.VehicleMarker.State.Position)
                .Concat(trajectory.ManeuverMarkers.Select(m => m.PostState.Position));
        Camera.Fit(positions, MapViewport);
        Invalidate();
    }

    public void CenterOnSelected()
    {
        var selected = _scene?.FindObject(_selectedId ?? "");
        if (selected is null)
            return;
        Camera.SetCenter(selected.State.Position.X, selected.State.Position.Y);
        Invalidate();
    }

    public void ResetView()
    {
        Camera.Reset();
        Invalidate();
    }

    public void ZoomAtCenter(double factor)
    {
        var v = MapViewport;
        Camera.ZoomAt(v.X + v.Width / 2, v.Y + v.Height / 2, factor, v);
        Invalidate();
    }

    public void PanByPixels(double x, double y)
    {
        Camera.PanPixels(x, y);
        Invalidate();
    }

    public void CancelInteraction()
    {
        _dragging = false;
        _dragMoved = false;
        Capture = false;
        Cursor = Cursors.Default;
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var timer = Stopwatch.StartNew();
        base.OnPaint(e);
        Graphics g = e.Graphics;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.ClearTypeGridFit;
        g.Clear(BackColor);

        var viewport = MapViewport;
        using var clip = new Region(new RectangleF((float)viewport.X, (float)viewport.Y,
            (float)viewport.Width, (float)viewport.Height));
        g.SetClip(clip, CombineMode.Intersect);
        DrawAxes(g, viewport);
        if (_scene is not null)
        {
            DrawTrails(g, viewport, _scene);
            DrawTrajectory(g, viewport, _scene);
            DrawObjects(g, viewport, _scene);
        }
        g.ResetClip();
        DrawHeader(g);
        DrawScale(g, viewport);
        if (_scene is not null)
            DrawProjectionNote(g);
        if (Diagnostic is not null)
            DrawDiagnostic(g, Diagnostic);

        timer.Stop();
        LastPaintMilliseconds = timer.Elapsed.TotalMilliseconds;
        PaintMeasured?.Invoke(LastPaintMilliseconds);
    }

    private void DrawHeader(Graphics g)
    {
        string title = _scene is null
            ? "STELLAR NAVIGATOR  /  SN8"
            : $"STELLAR NAVIGATOR  /  SN8 · {_scene.Orientation} X/Y PLANE  /  ORIGIN {_scene.ScientificOriginId.ToUpperInvariant()}";
        TextRenderer.DrawText(g, title, _headingFont, new Point(16, 13), Color.FromArgb(192, 213, 241));
        TextRenderer.DrawText(g, _scene is null
                ? "Camera is presentation-only; it does not represent spacecraft position"
                : $"{_scene.Orientation} X/Y projection · Z retained in details · camera is presentation-only",
            _smallFont,
            new Point(17, 30), Color.FromArgb(130, 151, 180));
    }

    private void DrawAxes(Graphics g, Viewport viewport)
    {
        using var axisPen = new Pen(Color.FromArgb(57, 75, 101), 1F);
        using var gridPen = new Pen(Color.FromArgb(25, 41, 63), 1F);
        // A light local grid at one astronomical unit per division is a
        // physical coordinate reference, not an orbit or an invented state.
        const double au = Distance.MetersPerAstronomicalUnit;
        double left = Camera.CenterX - viewport.Width * 0.5 / Camera.PixelsPerMeter;
        double right = Camera.CenterX + viewport.Width * 0.5 / Camera.PixelsPerMeter;
        double bottom = Camera.CenterY - viewport.Height * 0.5 / Camera.PixelsPerMeter;
        double top = Camera.CenterY + viewport.Height * 0.5 / Camera.PixelsPerMeter;
        if (Camera.PixelsPerMeter * au >= 46 && Camera.PixelsPerMeter * au <= 200)
        {
            DrawGridAxis(g, gridPen, viewport, true, left, right, au);
            DrawGridAxis(g, gridPen, viewport, false, bottom, top, au);
        }
        if (Camera.TryWorldToScreen(0, Camera.CenterY, viewport, out var yAxis) &&
            yAxis.X >= viewport.X && yAxis.X <= viewport.X + viewport.Width)
            g.DrawLine(axisPen, (float)yAxis.X, (float)viewport.Y, (float)yAxis.X, (float)(viewport.Y + viewport.Height));
        if (Camera.TryWorldToScreen(Camera.CenterX, 0, viewport, out var xAxis) &&
            xAxis.Y >= viewport.Y && xAxis.Y <= viewport.Y + viewport.Height)
            g.DrawLine(axisPen, (float)viewport.X, (float)xAxis.Y, (float)(viewport.X + viewport.Width), (float)xAxis.Y);
    }

    private void DrawGridAxis(Graphics g, Pen pen, Viewport viewport, bool vertical, double min, double max, double step)
    {
        double first = Math.Ceiling(min / step);
        double last = Math.Floor(max / step);
        if (!double.IsFinite(first) || !double.IsFinite(last) || last - first > 48 || Math.Abs(first) > 1e15)
            return;
        for (double tick = first; tick <= last; tick += 1)
        {
            double value = tick * step;
            if (vertical && Camera.TryWorldToScreen(value, Camera.CenterY, viewport, out var p))
                g.DrawLine(pen, (float)p.X, (float)viewport.Y, (float)p.X, (float)(viewport.Y + viewport.Height));
            else if (!vertical && Camera.TryWorldToScreen(Camera.CenterX, value, viewport, out p))
                g.DrawLine(pen, (float)viewport.X, (float)p.Y, (float)(viewport.X + viewport.Width), (float)p.Y);
        }
    }

    private void DrawTrails(Graphics g, Viewport viewport, NavigationScene scene)
    {
        foreach (var item in scene.Objects)
        {
            if (item.Trail.Count < 2 || !_trailPens.TryGetValue(item.Id, out var pen))
                continue;
            for (int i = 1; i < item.Trail.Count; i++)
            {
                var a = item.Trail[i - 1];
                var b = item.Trail[i];
                if (!Camera.TryWorldToScreen(a.PositionMeters.X, a.PositionMeters.Y, viewport, out var p0) ||
                    !Camera.TryWorldToScreen(b.PositionMeters.X, b.PositionMeters.Y, viewport, out var p1) ||
                    !TryClipLine(p0, p1, viewport, out var clipped0, out var clipped1))
                    continue;
                g.DrawLine(pen, clipped0, clipped1);
            }
        }
    }

    private void DrawTrajectory(Graphics g, Viewport viewport, NavigationScene scene)
    {
        if (scene.Trajectory is not { } trajectory)
            return;
        if (ShowTrajectory)
        {
            for (int i = 1; i < trajectory.Samples.Count; i++)
            {
                var before = trajectory.Samples[i - 1];
                var after = trajectory.Samples[i];
                if (before.State.EtSeconds == after.State.EtSeconds &&
                    before.State.Position.Equals(after.State.Position))
                    continue;
                if (!Camera.TryWorldToScreen(before.State.Position.X, before.State.Position.Y, viewport, out var p0) ||
                    !Camera.TryWorldToScreen(after.State.Position.X, after.State.Position.Y, viewport, out var p1) ||
                    !TryClipLine(p0, p1, viewport, out var clipped0, out var clipped1))
                    continue;
                Color color = SegmentColors[Math.Clamp(after.SegmentIndex, 0, SegmentColors.Length - 1)];
                using var pen = new Pen(Color.FromArgb(222, color), 2.1F);
                g.DrawLine(pen, clipped0, clipped1);
            }
            foreach (var sample in trajectory.Samples)
            {
                if (!Camera.TryWorldToScreen(sample.State.Position.X, sample.State.Position.Y, viewport, out var point) ||
                    point.X < viewport.X - 4 || point.X > viewport.X + viewport.Width + 4 ||
                    point.Y < viewport.Y - 4 || point.Y > viewport.Y + viewport.Height + 4)
                    continue;
                using var brush = new SolidBrush(Color.FromArgb(198, 139, 222, 248));
                g.FillEllipse(brush, (float)point.X - 1.6F, (float)point.Y - 1.6F, 3.2F, 3.2F);
            }
        }

        if (ShowManeuvers)
        {
            foreach (var marker in trajectory.ManeuverMarkers)
            {
                if (!Camera.TryWorldToScreen(marker.PostState.Position.X, marker.PostState.Position.Y,
                        viewport, out var point) ||
                    point.X < viewport.X - 12 || point.X > viewport.X + viewport.Width + 12 ||
                    point.Y < viewport.Y - 12 || point.Y > viewport.Y + viewport.Height + 12)
                    continue;
                float radius = marker.Id == _selectedManeuverId ? 8F : 5.5F;
                var diamond = new PointF[]
                {
                    new((float)point.X, (float)point.Y - radius),
                    new((float)point.X + radius, (float)point.Y),
                    new((float)point.X, (float)point.Y + radius),
                    new((float)point.X - radius, (float)point.Y),
                };
                using var brush = new SolidBrush(marker.Enabled ? Color.FromArgb(255, 188, 92) : Color.Gray);
                using var pen = new Pen(marker.Id == _selectedManeuverId ? Color.White : Color.FromArgb(255, 225, 171),
                    marker.Id == _selectedManeuverId ? 2.2F : 1.2F);
                g.FillPolygon(brush, diamond);
                g.DrawPolygon(pen, diamond);
            }
        }

        if (ShowTrajectory && Camera.TryWorldToScreen(trajectory.VehicleMarker.State.Position.X,
                trajectory.VehicleMarker.State.Position.Y, viewport, out var current) &&
            current.X >= viewport.X - 12 && current.X <= viewport.X + viewport.Width + 12 &&
            current.Y >= viewport.Y - 12 && current.Y <= viewport.Y + viewport.Height + 12)
        {
            using var brush = new SolidBrush(Color.FromArgb(106, 238, 255));
            using var pen = new Pen(Color.White, 1.5F);
            var triangle = new PointF[]
            {
                new((float)current.X, (float)current.Y - 8),
                new((float)current.X + 7, (float)current.Y + 6),
                new((float)current.X - 7, (float)current.Y + 6),
            };
            g.FillPolygon(brush, triangle);
            g.DrawPolygon(pen, triangle);
        }
    }

    private void DrawObjects(Graphics g, Viewport viewport, NavigationScene scene)
    {
        // Markers are always visible. Crowded inner-planet labels are omitted
        // until there is room; the selected object always gets a readable callout.
        foreach (var item in scene.Objects)
        {
            if (!Camera.TryWorldToScreen(item.State.Position.X, item.State.Position.Y, viewport, out var p) ||
                p.X < viewport.X - 30 || p.X > viewport.X + viewport.Width + 30 ||
                p.Y < viewport.Y - 30 || p.Y > viewport.Y + viewport.Height + 30 ||
                Math.Abs(p.X) > float.MaxValue || Math.Abs(p.Y) > float.MaxValue)
                continue;
            Color color = Colors.TryGetValue(item.Id, out var objectColor) ? objectColor : Color.White;
            float radius = item.Id == "sun" ? 8F : 6F; // screen-space navigation symbols, not physical diameters
            var marker = new RectangleF((float)p.X - radius, (float)p.Y - radius, radius * 2, radius * 2);
            using var brush = new SolidBrush(color);
            g.FillEllipse(brush, marker);
            if (item.Id == _selectedId)
            {
                using var selectedPen = new Pen(Color.WhiteSmoke, 1.4F);
                g.DrawEllipse(selectedPen, RectangleF.Inflate(marker, 4, 4));
            }
        }

        foreach (var item in scene.Objects)
        {
            if (!Camera.TryWorldToScreen(item.State.Position.X, item.State.Position.Y, viewport, out var p) ||
                p.X < viewport.X - 30 || p.X > viewport.X + viewport.Width + 30 ||
                p.Y < viewport.Y - 30 || p.Y > viewport.Y + viewport.Height + 30 ||
                Math.Abs(p.X) > float.MaxValue || Math.Abs(p.Y) > float.MaxValue)
                continue;
            using var labelBrush = new SolidBrush(item.IsSimulated
                ? Color.FromArgb(238, 215, 250)
                : Color.FromArgb(220, 230, 244));
            if (item.Id == _selectedId)
            {
                DrawSelectedLabel(g, viewport, item.Name, p, labelBrush);
            }
            else if (!IsCrowded(item, scene, viewport, p, 34))
            {
                g.DrawString(item.Name, _labelFont, labelBrush, (float)p.X + 10F, (float)p.Y - 9F);
            }
        }
    }

    private void DrawSelectedLabel(Graphics g, Viewport viewport, string name, ScreenPoint marker, Brush textBrush)
    {
        SizeF textSize = g.MeasureString(name, _labelFont);
        float width = textSize.Width + 12F;
        float height = textSize.Height + 6F;
        float x = (float)marker.X + 13F;
        float y = (float)marker.Y - height * 0.5F;
        if (x + width > viewport.X + viewport.Width)
            x = (float)marker.X - width - 13F;
        if (x < viewport.X)
            x = (float)viewport.X + 2F;
        if (y < viewport.Y)
            y = (float)viewport.Y + 2F;
        if (y + height > viewport.Y + viewport.Height)
            y = (float)(viewport.Y + viewport.Height - height - 2F);
        var box = new RectangleF(x, y, width, height);
        using var back = new SolidBrush(Color.FromArgb(230, 18, 29, 44));
        using var edge = new Pen(Color.FromArgb(211, 235, 246), 1F);
        g.FillRectangle(back, box);
        g.DrawRectangle(edge, box.X, box.Y, box.Width, box.Height);
        g.DrawString(name, _labelFont, textBrush, box.X + 6F, box.Y + 3F);
    }

    private bool IsCrowded(SceneObject item, NavigationScene scene, Viewport viewport,
        ScreenPoint point, double thresholdPixels)
    {
        double thresholdSquared = thresholdPixels * thresholdPixels;
        foreach (var other in scene.Objects)
        {
            if (other.Id == item.Id ||
                !Camera.TryWorldToScreen(other.State.Position.X, other.State.Position.Y, viewport, out var p))
                continue;
            double dx = point.X - p.X, dy = point.Y - p.Y;
            if (dx * dx + dy * dy < thresholdSquared)
                return true;
        }
        return false;
    }

    private void DrawScale(Graphics g, Viewport viewport)
    {
        double targetMeters = 110.0 / Camera.PixelsPerMeter;
        double niceMeters = NiceValue(targetMeters);
        double px = niceMeters * Camera.PixelsPerMeter;
        if (!double.IsFinite(px) || px < 24 || px > viewport.Width * 0.75)
            return;
        float x = (float)(viewport.X + 18);
        float y = (float)(viewport.Y + viewport.Height - 20);
        using var pen = new Pen(Color.FromArgb(190, 210, 229), 2F);
        g.DrawLine(pen, x, y, x + (float)px, y);
        g.DrawLine(pen, x, y - 5, x, y + 3);
        g.DrawLine(pen, x + (float)px, y - 5, x + (float)px, y + 3);
        string label = FormatDistance(niceMeters);
        TextRenderer.DrawText(g, label, _smallFont, new Point((int)x, (int)y - 19), Color.FromArgb(202, 216, 235));
    }

    private void DrawProjectionNote(Graphics g) =>
        TextRenderer.DrawText(g, "Planet symbols are not drawn to physical scale", _smallFont,
            new Point(15, Math.Max(0, Height - 25)), Color.FromArgb(153, 168, 189));

    private void DrawDiagnostic(Graphics g, string message)
    {
        using var shade = new SolidBrush(Color.FromArgb(224, 16, 24, 37));
        var box = new Rectangle(36, 68, Math.Max(300, Width - 72), 96);
        g.FillRectangle(shade, box);
        TextRenderer.DrawText(g, "Scene not updated", _headingFont, new Point(box.X + 16, box.Y + 12), Color.FromArgb(255, 192, 120));
        TextRenderer.DrawText(g, message, Font, new Rectangle(box.X + 16, box.Y + 36, box.Width - 32, box.Height - 44),
            Color.FromArgb(239, 225, 208), TextFormatFlags.WordBreak | TextFormatFlags.EndEllipsis);
    }

    private static bool TryClipLine(ScreenPoint a, ScreenPoint b, Viewport v, out PointF clippedA, out PointF clippedB)
    {
        clippedA = clippedB = default;
        if (!double.IsFinite(a.X) || !double.IsFinite(a.Y) || !double.IsFinite(b.X) || !double.IsFinite(b.Y))
            return false;
        double dx = b.X - a.X, dy = b.Y - a.Y;
        if (!double.IsFinite(dx) || !double.IsFinite(dy))
            return false;
        double t0 = 0, t1 = 1;
        if (!ClipEdge(-dx, a.X - v.X, ref t0, ref t1) ||
            !ClipEdge(dx, v.X + v.Width - a.X, ref t0, ref t1) ||
            !ClipEdge(-dy, a.Y - v.Y, ref t0, ref t1) ||
            !ClipEdge(dy, v.Y + v.Height - a.Y, ref t0, ref t1))
            return false;
        double x0 = a.X + t0 * dx, y0 = a.Y + t0 * dy;
        double x1 = a.X + t1 * dx, y1 = a.Y + t1 * dy;
        if (!double.IsFinite(x0) || !double.IsFinite(y0) || !double.IsFinite(x1) || !double.IsFinite(y1))
            return false;
        clippedA = new PointF((float)x0, (float)y0);
        clippedB = new PointF((float)x1, (float)y1);
        return true;
    }

    private static bool ClipEdge(double p, double q, ref double t0, ref double t1)
    {
        if (!double.IsFinite(p) || !double.IsFinite(q))
            return false;
        if (Math.Abs(p) < double.Epsilon)
            return q >= 0;
        double r = q / p;
        if (!double.IsFinite(r))
            return false;
        if (p < 0)
        {
            if (r > t1) return false;
            if (r > t0) t0 = r;
        }
        else
        {
            if (r < t0) return false;
            if (r < t1) t1 = r;
        }
        return true;
    }

    private static double NiceValue(double value)
    {
        if (!double.IsFinite(value) || value <= 0)
            return 0;
        double power = Math.Pow(10, Math.Floor(Math.Log10(value)));
        double normalized = value / power;
        double factor = normalized >= 5 ? 5 : normalized >= 2 ? 2 : 1;
        return factor * power;
    }

    internal static string FormatDistance(double meters)
    {
        if (meters >= Distance.MetersPerAstronomicalUnit * 0.01)
            return $"{meters / Distance.MetersPerAstronomicalUnit:F3} AU";
        if (meters >= 1_000_000)
            return $"{meters / 1_000_000:F2} million km";
        if (meters >= 1_000)
            return $"{meters / 1_000:F0} km";
        return $"{meters:F0} m";
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        base.OnMouseDown(e);
        if (e.Button is MouseButtons.Left or MouseButtons.Middle)
        {
            Focus();
            _dragging = true;
            _dragMoved = false;
            _lastMouse = _dragStart = e.Location;
            Capture = true;
            Cursor = Cursors.SizeAll;
        }
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        base.OnMouseMove(e);
        if (!_dragging)
            return;
        int dx = e.X - _lastMouse.X, dy = e.Y - _lastMouse.Y;
        if (Math.Abs(e.X - _dragStart.X) + Math.Abs(e.Y - _dragStart.Y) > 4)
            _dragMoved = true;
        if (dx != 0 || dy != 0)
        {
            Camera.PanPixels(dx, dy);
            _lastMouse = e.Location;
            Invalidate();
        }
    }

    protected override void OnMouseUp(MouseEventArgs e)
    {
        base.OnMouseUp(e);
        if (!_dragging)
            return;
        bool wasMoved = _dragMoved;
        _dragging = false;
        Capture = false;
        Cursor = Cursors.Default;
        if (e.Button == MouseButtons.Left && !wasMoved)
        {
            var hit = Camera2D.HitTest(_scene, Camera, e.X, e.Y, MapViewport, 11);
            if (hit is not null)
                Select(hit.Id);
        }
    }

    protected override void OnMouseWheel(MouseEventArgs e)
    {
        base.OnMouseWheel(e);
        Camera.ZoomAt(e.X, e.Y, e.Delta > 0 ? 1.22 : 1.0 / 1.22, MapViewport);
        Invalidate();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            foreach (var pen in _trailPens.Values)
                pen.Dispose();
            _labelFont.Dispose();
            _smallFont.Dispose();
            _headingFont.Dispose();
        }
        base.Dispose(disposing);
    }
}
