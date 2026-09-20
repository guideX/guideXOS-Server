namespace HostLogProof;

/// <summary>Deterministic focused probes for the bounded horizontal separator.</summary>
public static class GuideXosSeparatorTests
{
    private static int s_caseCount;

    public static bool Run(GuideXosHost host, GuideXosSurface surface)
    {
        s_caseCount = 0;
        bool construction = ConstructionAndDefaults();
        bool bounds = BoundsAndPreservation();
        bool visibility = VisibilityAndReset(surface);
        bool dynamic = DynamicGeometry(surface);
        bool rendering = Rendering(surface);
        bool independent = IndependentInstances(surface);
        bool result = construction && bounds && visibility && dynamic &&
            rendering && independent;
        if (host != null)
        {
            host.TryLog(result
                ? "C123-SEPARATOR-TESTS cases=48 result=PASS"u8
                : "C123-SEPARATOR-TESTS cases=48 result=FAIL"u8);
        }
        return result && s_caseCount == 48;
    }

    private static bool Check(bool condition)
    {
        ++s_caseCount;
        return condition;
    }

    private static bool ConstructionAndDefaults()
    {
        GuideXosSeparator separator = new(16, 24, 160);
        return Check(separator.X == 16 && separator.Y == 24) &&
            Check(separator.Width == 160 &&
                separator.Height == GuideXosSeparator.TextRowHeight) &&
            Check(separator.RenderWidth == 20 && separator.Visible) &&
            Check(GuideXosSeparator.SeparatorGlyph == (byte)'-' &&
                separator.RejectedInputCount == 0u);
    }

    private static bool BoundsAndPreservation()
    {
        GuideXosSeparator separator = new(16, 24, 160);
        bool minimum = Check(separator.TrySetBounds(16, 24,
            GuideXosSeparator.MinimumSupportedWidth) && separator.Width == 8);
        bool maximum = Check(separator.TrySetBounds(16, 24,
            GuideXosSeparator.MaximumSupportedWidth) && separator.Width == 504);
        int oldX = separator.X;
        int oldY = separator.Y;
        int oldWidth = separator.Width;
        bool belowMinimum = Check(!separator.SetWidth(0) && separator.Width == oldWidth);
        bool aboveMaximum = Check(!separator.SetWidth(512) && separator.Width == oldWidth);
        bool invalidX = Check(!separator.TrySetBounds(-1, oldY, oldWidth) &&
            separator.X == oldX);
        bool invalidY = Check(!separator.TrySetBounds(oldX, -1, oldWidth) &&
            separator.Y == oldY);
        bool unaligned = Check(!separator.TrySetBounds(oldX, oldY, 15) &&
            separator.Width == oldWidth);
        bool xOverflow = Check(!separator.TrySetBounds(
            GuideXosSeparator.MaximumSupportedCoordinate - oldWidth + 1,
            oldY, oldWidth) && separator.X == oldX);
        bool yOverflow = Check(!separator.TrySetBounds(oldX,
            GuideXosSeparator.MaximumSupportedCoordinate -
                GuideXosSeparator.TextRowHeight + 1, oldWidth) &&
            separator.Y == oldY);
        bool preserved = Check(separator.X == oldX && separator.Y == oldY &&
            separator.Width == oldWidth);
        bool widthUpdate = Check(separator.SetWidth(320) && separator.Width == 320);
        bool invalidWidthPreserved = Check(!separator.SetWidth(321) &&
            separator.Width == 320);
        return minimum && maximum && belowMinimum && aboveMaximum && invalidX &&
            invalidY && unaligned && xOverflow && yOverflow && preserved &&
            widthUpdate && invalidWidthPreserved;
    }

    private static bool VisibilityAndReset(GuideXosSurface surface)
    {
        GuideXosSeparator separator = new(16, 24, 160);
        bool defaultVisible = Check(separator.Visible);
        separator.SetVisible(false);
        bool hidden = Check(!separator.Visible);
        bool hiddenGeometry = Check(separator.X == 16 && separator.Y == 24 &&
            separator.Width == 160);
        bool hiddenRender = Check(surface != null &&
            separator.Render(surface) == GuideXosResult.Success);
        separator.SetVisible(true);
        bool shown = Check(separator.Visible &&
            separator.Render(surface) == GuideXosResult.Success);
        separator.SetWidth(240);
        separator.SetVisible(false);
        separator.Reset();
        bool reset = Check(separator.Visible && separator.Width == 240 &&
            separator.RejectedInputCount == 0u);
        return defaultVisible && hidden && hiddenGeometry && hiddenRender &&
            shown && reset;
    }

    private static bool DynamicGeometry(GuideXosSurface surface)
    {
        GuideXosSeparator separator = new(16, 24, 160);
        bool increased = Check(separator.SetWidth(320) && separator.RenderWidth == 40);
        bool increasedRender = Check(surface != null &&
            separator.Render(surface) == GuideXosResult.Success);
        bool decreased = Check(separator.SetWidth(96) && separator.RenderWidth == 12);
        bool decreasedRender = Check(surface != null &&
            separator.Render(surface) == GuideXosResult.Success);
        bool noStaleTail = Check(separator.Width == 96 && separator.RenderWidth == 12);
        bool repositionX = Check(separator.TrySetBounds(48, 24, 96) &&
            separator.X == 48);
        bool repositionY = Check(separator.TrySetBounds(48, 42, 96) &&
            separator.Y == 42);
        separator.SetVisible(false);
        bool hiddenUpdate = Check(separator.TrySetBounds(80, 60, 240) &&
            separator.X == 80 && separator.Y == 60 && separator.Width == 240);
        bool hiddenWidth = Check(separator.RenderWidth == 30 && !separator.Visible);
        separator.SetVisible(true);
        bool shownUpdated = Check(separator.Visible &&
            separator.Render(surface) == GuideXosResult.Success &&
            separator.X == 80 && separator.Y == 60 && separator.Width == 240);
        return increased && increasedRender && decreased && decreasedRender &&
            noStaleTail && repositionX && repositionY && hiddenUpdate &&
            hiddenWidth && shownUpdated;
    }

    private static bool Rendering(GuideXosSurface surface)
    {
        GuideXosSeparator separator = new(20, 200, 480);
        bool surfaceKnown = Check(surface != null);
        bool visibleRender = Check(surfaceKnown &&
            separator.Render(surface) == GuideXosResult.Success);
        bool widthBound = Check(separator.RenderWidth == 60 &&
            separator.Width == 480);
        separator.SetVisible(false);
        bool hiddenRender = Check(surfaceKnown &&
            separator.Render(surface) == GuideXosResult.Success);
        bool nullSurface = Check(separator.Render(null) ==
            GuideXosResult.InvalidArgument);
        return surfaceKnown && visibleRender && widthBound && hiddenRender &&
            nullSurface;
    }

    private static bool IndependentInstances(GuideXosSurface surface)
    {
        GuideXosSeparator first = new(16, 24, 80);
        GuideXosSeparator second = new(160, 42, 160);
        bool initial = Check(first.X == 16 && first.Width == 80 &&
            second.X == 160 && second.Width == 160);
        first.SetVisible(false);
        bool visibility = Check(!first.Visible && second.Visible);
        bool geometry = Check(first.Y == 24 && second.Y == 42);
        bool firstUpdate = Check(first.TrySetBounds(24, 60, 96) &&
            first.X == 24 && second.X == 160 && second.Width == 160);
        bool secondUpdate = Check(second.SetWidth(240) &&
            second.Width == 240 && first.Width == 96);
        second.SetVisible(false);
        bool visibilityIsolation = Check(!first.Visible && !second.Visible);
        first.SetVisible(true);
        bool revealIsolation = Check(first.Visible && !second.Visible);
        second.SetVisible(true);
        bool secondShown = Check(second.Visible && second.RenderWidth == 30);
        bool independentRender = Check(surface != null &&
            first.Render(surface) == GuideXosResult.Success &&
            second.Render(surface) == GuideXosResult.Success);
        bool hiddenUpdateIsolation = Check(first.SetWidth(32) &&
            first.Width == 32 && second.Width == 240);
        bool rejectionIsolation = Check(!second.SetWidth(7) &&
            second.Width == 240 && first.Width == 32);
        return initial && visibility && geometry && firstUpdate && secondUpdate &&
            visibilityIsolation && revealIsolation && secondShown &&
            independentRender && hiddenUpdateIsolation && rejectionIsolation;
    }
}
