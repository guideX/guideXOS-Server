namespace HostLogProof;

/// <summary>Deterministic focused probes for the bounded progress bar.</summary>
public static class GuideXosProgressBarTests
{
    private static int s_caseCount;

    public static bool Run(GuideXosHost host, GuideXosSurface surface)
    {
        s_caseCount = 0;
        bool result = ConstructionAndRange() && ValuesAndArithmetic() &&
            GeometryAndVisibility(surface) && DynamicAndIndependent(surface);
        if (host != null)
        {
            host.TryLog(result
                ? "C125-PROGRESS-TESTS cases=50 result=PASS"u8
                : "C125-PROGRESS-TESTS cases=50 result=FAIL"u8);
        }
        return result && s_caseCount == 50;
    }

    private static bool Check(bool condition)
    {
        ++s_caseCount;
        return condition;
    }

    private static bool ConstructionAndRange()
    {
        GuideXosProgressBar bar = new(16, 24, 240, 0, 100, 0);
        bool construction = Check(bar.X == 16 && bar.Y == 24 &&
            bar.Width == 240 && bar.Height == 18 && bar.Minimum == 0 &&
            bar.Maximum == 100 && bar.Value == 0 && bar.Visible);
        bool configured = Check(bar.TrySetRange(0, 110) &&
            bar.Minimum == 0 && bar.Maximum == 110);
        bool min = Check(bar.TrySetValue(0) && bar.Value == 0);
        bool max = Check(bar.TrySetValue(110) && bar.Value == 110);
        bool equalRejected = Check(!bar.TrySetRange(110, 110) &&
            bar.Minimum == 0 && bar.Maximum == 110 && bar.Value == 110);
        bool reversedRejected = Check(!bar.TrySetRange(111, 10) &&
            bar.Minimum == 0 && bar.Maximum == 110);
        bool negativeRejected = Check(!bar.TrySetRange(-1, 110) &&
            bar.Minimum == 0 && bar.Maximum == 110);
        bool ceiling = Check(bar.TrySetRange(0, GuideXosProgressBar.MaximumSupportedValue) &&
            bar.Minimum == 0 && bar.Maximum == GuideXosProgressBar.MaximumSupportedValue);
        bool aboveCeiling = Check(!bar.TrySetRange(0,
            GuideXosProgressBar.MaximumSupportedValue + 1) &&
            bar.Maximum == GuideXosProgressBar.MaximumSupportedValue);
        bool rangePreserved = Check(bar.TrySetValue(7) &&
            !bar.TrySetRange(8, 9) && bar.Minimum == 0 &&
            bar.Maximum == GuideXosProgressBar.MaximumSupportedValue && bar.Value == 7);
        bool defaultRange = Check(new GuideXosProgressBar(0, 0,
            GuideXosProgressBar.MinimumSupportedWidth).Maximum == 100);
        return construction && configured && min && max && equalRejected &&
            reversedRejected && negativeRejected && ceiling && aboveCeiling &&
            rangePreserved && defaultRange;
    }

    private static bool ValuesAndArithmetic()
    {
        GuideXosProgressBar bar = new(16, 24, 240, 0, 100, 0);
        bool initial = Check(bar.Value == 0 && bar.Percentage == 0 &&
            bar.FilledCells == 0);
        bool minimum = Check(bar.TrySetValue(0) && bar.Percentage == 0 &&
            bar.FilledCells == 0);
        bool quarter = Check(bar.TrySetValue(25) && bar.Percentage == 25);
        bool half = Check(bar.TrySetValue(50) && bar.Percentage == 50);
        bool threeQuarter = Check(bar.TrySetValue(75) && bar.Percentage == 75);
        bool maximum = Check(bar.TrySetValue(100) && bar.Percentage == 100 &&
            bar.FilledCells == bar.FillCellCount);
        bool belowRejected = Check(!bar.TrySetValue(-1) && bar.Value == 100);
        bool aboveRejected = Check(!bar.TrySetValue(101) && bar.Value == 100);
        bool valuePreserved = Check(bar.TrySetValue(50) &&
            bar.TrySetValue(51) && bar.Value == 51);
        // Re-establish a valid value before testing the exact rejection case.
        bar.TrySetValue(50);
        bool rejectedPreserved = Check(!bar.TrySetValue(101) && bar.Value == 50);
        bool rounding = Check(bar.TrySetValue(0) && bar.TrySetRange(0, 3) &&
            bar.TrySetValue(1) &&
            bar.Percentage == 33 && bar.FilledCells * 3 <= bar.Value * bar.FillCellCount);
        bool wideArithmetic = Check(bar.TrySetRange(0, GuideXosProgressBar.MaximumSupportedValue) &&
            bar.TrySetValue(GuideXosProgressBar.MaximumSupportedValue - 1) &&
            bar.Percentage == 99 && bar.FilledCells == bar.FillCellCount - 1);
        bool boundaries = Check(bar.TrySetValue(0) && bar.Percentage == 0 &&
            bar.TrySetValue(GuideXosProgressBar.MaximumSupportedValue) &&
            bar.Percentage == 100 && bar.FilledCells == bar.FillCellCount);
        return initial && minimum && quarter && half && threeQuarter && maximum &&
            belowRejected && aboveRejected && valuePreserved && rejectedPreserved &&
            rounding && wideArithmetic && boundaries;
    }

    private static bool GeometryAndVisibility(GuideXosSurface surface)
    {
        GuideXosProgressBar bar = new(16, 24, 240, 0, 100, 50);
        bool validGeometry = Check(bar.TrySetBounds(32, 42, 320) &&
            bar.X == 32 && bar.Y == 42 && bar.Width == 320);
        int oldX = bar.X;
        int oldY = bar.Y;
        int oldWidth = bar.Width;
        bool invalidX = Check(!bar.TrySetBounds(-1, oldY, oldWidth) && bar.X == oldX);
        bool invalidY = Check(!bar.TrySetBounds(oldX, -1, oldWidth) && bar.Y == oldY);
        bool tooSmall = Check(!bar.TrySetBounds(oldX, oldY,
            GuideXosProgressBar.MinimumSupportedWidth - 8) && bar.Width == oldWidth);
        bool tooLarge = Check(!bar.TrySetBounds(oldX, oldY,
            GuideXosProgressBar.MaximumSupportedWidth + 8) && bar.Width == oldWidth);
        bool unaligned = Check(!bar.TrySetBounds(oldX, oldY, oldWidth + 4) &&
            bar.Width == oldWidth);
        bool geometryPreserved = Check(bar.X == oldX && bar.Y == oldY &&
            bar.Width == oldWidth && bar.Value == 50 && bar.Minimum == 0 &&
            bar.Maximum == 100);
        bool rendered = Check(surface != null && bar.Render(surface) == GuideXosResult.Success);
        bar.SetVisible(false);
        bool hidden = Check(!bar.Visible && bar.Render(surface) == GuideXosResult.Success);
        bool hiddenValue = Check(bar.TrySetValue(75) && bar.Value == 75 && !bar.Visible);
        bar.SetVisible(true);
        bool reshown = Check(bar.Visible && bar.Render(surface) == GuideXosResult.Success &&
            bar.Percentage == 75);
        bool reset = Check(bar.SetWidth(240) && bar.Value == 75 &&
            bar.Minimum == 0 && bar.Maximum == 100);
        return validGeometry && invalidX && invalidY && tooSmall && tooLarge &&
            unaligned && geometryPreserved && rendered && hidden && hiddenValue &&
            reshown && reset;
    }

    private static bool DynamicAndIndependent(GuideXosSurface surface)
    {
        GuideXosProgressBar first = new(16, 24, 160, 0, 100, 25);
        GuideXosProgressBar second = new(200, 42, 320, 0, 200, 100);
        bool initial = Check(first.Value == 25 && first.Width == 160 &&
            second.Value == 100 && second.Width == 320);
        bool increase = Check(first.SetWidth(320) && first.FilledCells == 8);
        bool decrease = Check(first.SetWidth(160) && first.FilledCells == 3 &&
            first.Value == 25);
        bool noTail = Check(first.RenderWidth == 20 && first.FillCellCount == 13 &&
            first.FilledCells <= first.FillCellCount);
        bool reposition = Check(first.TrySetBounds(32, 60, 160) &&
            first.X == 32 && first.Y == 60);
        bool rangeIsolation = Check(second.TrySetRange(10, 210) &&
            second.Minimum == 10 && second.Maximum == 210 && first.Minimum == 0 &&
            first.Maximum == 100);
        bool valueIsolation = Check(second.TrySetValue(200) &&
            second.Value == 200 && first.Value == 25);
        first.SetVisible(false);
        bool visibilityIsolation = Check(!first.Visible && second.Visible);
        second.SetVisible(false);
        bool secondHidden = Check(!second.Visible && !first.Visible);
        first.SetVisible(true);
        bool firstShown = Check(first.Visible && !second.Visible);
        bool renders = Check(surface != null && first.Render(surface) == GuideXosResult.Success &&
            second.Render(surface) == GuideXosResult.Success);
        bool nullSurface = Check(first.Render(null) == GuideXosResult.InvalidArgument);
        bool zeroAndFull = Check(first.TrySetValue(0) && first.FilledCells == 0 &&
            first.TrySetValue(100) && first.FilledCells == first.FillCellCount);
        bool independentGeometry = Check(first.Width == 160 && second.Width == 320 &&
            first.X == 32 && second.X == 200);
        return initial && increase && decrease && noTail && reposition && rangeIsolation &&
            valueIsolation && visibilityIsolation && secondHidden && firstShown && renders &&
            nullSurface &&
            zeroAndFull && independentGeometry;
    }
}
