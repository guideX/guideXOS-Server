using Astronomy.Core;

namespace StellarNavigator.Scene;

public readonly record struct ScreenPoint(double X, double Y);
public readonly record struct Viewport(double X, double Y, double Width, double Height)
{
    public bool IsUsable => double.IsFinite(X) && double.IsFinite(Y) &&
        double.IsFinite(Width) && double.IsFinite(Height) && Width > 0 && Height > 0;
}

/// <summary>
/// Portable double-precision camera for the physical J2000 X/Y coordinate map.
/// Camera state is entirely independent of ephemeris or spacecraft state.
/// </summary>
public sealed class Camera2D
{
    public const double MinimumPixelsPerMeter = 1e-20;
    public const double MaximumPixelsPerMeter = 1e-2;
    public const double DefaultPixelsPerMeter = 1e-10;

    public double CenterX { get; private set; }
    public double CenterY { get; private set; }
    public double PixelsPerMeter { get; private set; } = DefaultPixelsPerMeter;

    public void Reset()
    {
        CenterX = 0.0;
        CenterY = 0.0;
        PixelsPerMeter = DefaultPixelsPerMeter;
    }

    public void SetCenter(double x, double y)
    {
        if (!double.IsFinite(x) || !double.IsFinite(y))
            throw new ArgumentOutOfRangeException(nameof(x), "Camera center must be finite.");
        CenterX = x;
        CenterY = y;
    }

    public void SetScale(double pixelsPerMeter)
    {
        if (!double.IsFinite(pixelsPerMeter) || pixelsPerMeter <= 0)
            throw new ArgumentOutOfRangeException(nameof(pixelsPerMeter), "Camera scale must be finite and positive.");
        PixelsPerMeter = Math.Clamp(pixelsPerMeter, MinimumPixelsPerMeter, MaximumPixelsPerMeter);
    }

    public bool TryWorldToScreen(double xMeters, double yMeters, Viewport viewport, out ScreenPoint point)
    {
        point = default;
        if (!viewport.IsUsable || !double.IsFinite(xMeters) || !double.IsFinite(yMeters) ||
            !double.IsFinite(CenterX) || !double.IsFinite(CenterY) ||
            !double.IsFinite(PixelsPerMeter) || PixelsPerMeter <= 0)
            return false;

        // Subtract in binary64 before scaling. Absolute astronomical coordinates
        // are never cast to float or mapped to pixels directly.
        double x = viewport.X + viewport.Width * 0.5 + (xMeters - CenterX) * PixelsPerMeter;
        double y = viewport.Y + viewport.Height * 0.5 - (yMeters - CenterY) * PixelsPerMeter;
        if (!double.IsFinite(x) || !double.IsFinite(y))
            return false;
        point = new ScreenPoint(x, y);
        return true;
    }

    public bool TryScreenToWorld(double screenX, double screenY, Viewport viewport,
        out double xMeters, out double yMeters)
    {
        xMeters = yMeters = 0;
        if (!viewport.IsUsable || !double.IsFinite(screenX) || !double.IsFinite(screenY) ||
            !double.IsFinite(PixelsPerMeter) || PixelsPerMeter <= 0)
            return false;
        double x = CenterX + (screenX - viewport.X - viewport.Width * 0.5) / PixelsPerMeter;
        double y = CenterY - (screenY - viewport.Y - viewport.Height * 0.5) / PixelsPerMeter;
        if (!double.IsFinite(x) || !double.IsFinite(y))
            return false;
        xMeters = x;
        yMeters = y;
        return true;
    }

    public bool ZoomAt(double screenX, double screenY, double factor, Viewport viewport)
    {
        if (!double.IsFinite(factor) || factor <= 0 || !viewport.IsUsable ||
            !TryScreenToWorld(screenX, screenY, viewport, out double beforeX, out double beforeY))
            return false;
        double nextScale = Math.Clamp(PixelsPerMeter * factor,
            MinimumPixelsPerMeter, MaximumPixelsPerMeter);
        double dx = screenX - viewport.X - viewport.Width * 0.5;
        double dy = screenY - viewport.Y - viewport.Height * 0.5;
        double nextCenterX = beforeX - dx / nextScale;
        double nextCenterY = beforeY + dy / nextScale;
        if (!double.IsFinite(nextCenterX) || !double.IsFinite(nextCenterY))
            return false;
        CenterX = nextCenterX;
        CenterY = nextCenterY;
        PixelsPerMeter = nextScale;
        return true;
    }

    public bool PanPixels(double deltaX, double deltaY)
    {
        if (!double.IsFinite(deltaX) || !double.IsFinite(deltaY) ||
            !double.IsFinite(PixelsPerMeter) || PixelsPerMeter <= 0)
            return false;
        double x = CenterX - deltaX / PixelsPerMeter;
        double y = CenterY + deltaY / PixelsPerMeter;
        if (!double.IsFinite(x) || !double.IsFinite(y))
            return false;
        CenterX = x;
        CenterY = y;
        return true;
    }

    public void Fit(IEnumerable<Vector3D> positions, Viewport viewport, double margin = 0.88)
    {
        ArgumentNullException.ThrowIfNull(positions);
        if (!viewport.IsUsable || !double.IsFinite(margin) || margin <= 0 || margin >= 1)
            throw new ArgumentOutOfRangeException(nameof(viewport), "A usable viewport and margin in (0,1) are required.");
        var values = positions.Where(IsFinite).ToArray();
        if (values.Length == 0)
        {
            Reset();
            return;
        }
        double minX = values.Min(v => v.X), maxX = values.Max(v => v.X);
        double minY = values.Min(v => v.Y), maxY = values.Max(v => v.Y);
        CenterX = minX * 0.5 + maxX * 0.5;
        CenterY = minY * 0.5 + maxY * 0.5;
        double spanX = maxX * 0.5 - minX * 0.5;
        double spanY = maxY * 0.5 - minY * 0.5;
        double span = Math.Max(spanX, spanY);
        if (!double.IsFinite(CenterX) || !double.IsFinite(CenterY) || !double.IsFinite(span) || span <= 0)
        {
            PixelsPerMeter = DefaultPixelsPerMeter;
            return;
        }
        double next = margin * Math.Min(viewport.Width * 0.5 / Math.Max(spanX, span * 1e-12),
            viewport.Height * 0.5 / Math.Max(spanY, span * 1e-12));
        if (!double.IsFinite(next) || next <= 0)
            next = MinimumPixelsPerMeter;
        PixelsPerMeter = Math.Clamp(next, MinimumPixelsPerMeter, MaximumPixelsPerMeter);
    }

    public static SceneObject? HitTest(NavigationScene? scene, Camera2D camera,
        double screenX, double screenY, Viewport viewport, double minimumRadiusPixels = 10)
    {
        if (scene is null || camera is null || !double.IsFinite(minimumRadiusPixels) || minimumRadiusPixels <= 0)
            return null;
        SceneObject? hit = null;
        double bestDistanceSquared = minimumRadiusPixels * minimumRadiusPixels;
        IEnumerable<SceneObject> candidates = scene.Objects;
        if (scene.Trajectory is { } trajectory)
        {
            candidates = candidates
                .Concat(trajectory.Samples.Select(sample => scene.FindObject(sample.Id)!).Where(x => x is not null))
                .Concat(trajectory.ManeuverMarkers.Select(marker => scene.FindObject($"maneuver:{marker.Id}")!).Where(x => x is not null))
                .Append(trajectory.VehicleMarker);
        }
        foreach (var item in candidates)
        {
            if (!camera.TryWorldToScreen(item.State.Position.X, item.State.Position.Y, viewport, out var p))
                continue;
            double dx = p.X - screenX, dy = p.Y - screenY;
            double d2 = dx * dx + dy * dy;
            if (double.IsFinite(d2) && d2 <= bestDistanceSquared)
            {
                hit = item;
                bestDistanceSquared = d2;
            }
        }
        return hit;
    }

    private static bool IsFinite(Vector3D v) =>
        double.IsFinite(v.X) && double.IsFinite(v.Y) && double.IsFinite(v.Z);
}
