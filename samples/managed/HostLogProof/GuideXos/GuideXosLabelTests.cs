namespace HostLogProof;

/// <summary>Deterministic focused probes for the presentation-only label.</summary>
public static class GuideXosLabelTests
{
    private static int s_caseCount;

    public static bool Run(GuideXosHost host, GuideXosSurface surface)
    {
        s_caseCount = 0;
        bool construction = ConstructionAndDefaults();
        bool text = TextStorage();
        bool visibility = Visibility();
        bool geometry = Geometry();
        bool rendering = Rendering(surface);
        bool independent = IndependentInstances();
        bool result = construction && text && visibility && geometry && rendering && independent;
        if (host != null)
        {
            host.TryLog(result
                ? "C122-LABEL-TESTS cases=44 result=PASS"u8
                : "C122-LABEL-TESTS cases=44 result=FAIL"u8);
        }
        return result && s_caseCount == 44;
    }

    private static bool Check(bool condition)
    {
        ++s_caseCount;
        return condition;
    }

    private static bool ConstructionAndDefaults()
    {
        GuideXosLabel label = new(16, 24, 160);
        return Check(label.X == 16 && label.Y == 24) &&
            Check(label.Width == 160 && label.Height == GuideXosLabel.TextRowHeight) &&
            Check(label.RenderWidth == 20 && label.MaximumTextLength ==
                GuideXosLabel.DefaultMaximumTextLength) &&
            Check(label.Text == string.Empty && label.Length == 0 && label.Visible);
    }

    private static bool TextStorage()
    {
        GuideXosLabel label = new(16, 24, 160);
        bool valid = Check(label.SetText("Ready") && label.Text == "Ready" &&
            label.Length == 5);
        bool empty = Check(label.SetText(string.Empty) && label.Text == string.Empty &&
            label.Length == 0);
        string maximum = new('A', label.MaximumTextLength);
        bool maximumAccepted = Check(label.SetText(maximum) && label.Text == maximum &&
            label.Length == label.MaximumTextLength);
        string beforeRejected = label.Text;
        string overCapacity = new('B', label.MaximumTextLength + 1);
        bool overCapacityRejected = Check(!label.SetText(overCapacity) &&
            label.Text == beforeRejected && label.Length == beforeRejected.Length);
        bool clear = Check(label.Clear() && label.Text == string.Empty && label.Length == 0);
        bool setAfterClear = Check(label.SetText("After clear") &&
            label.Text == "After clear");
        bool repeated = Check(label.SetText("Ready") && label.Text == "Ready") &&
            Check(label.SetText("/system/apps/NOTES.TXT") &&
                label.Text == "/system/apps/NOTES.TXT");
        bool shorter = Check(label.SetText("Longer value") &&
            label.SetText("Short") && label.Text == "Short" && label.Length == 5);
        bool noStaleTail = Check(!label.Text.Contains("value"));
        bool longer = Check(label.SetText("A") && label.SetText("Longer again") &&
            label.Text == "Longer again");
        return valid && empty && maximumAccepted && overCapacityRejected && clear &&
            setAfterClear && repeated && shorter && noStaleTail && longer;
    }

    private static bool Visibility()
    {
        GuideXosLabel label = new(16, 24, 160, "Persist me");
        bool defaultVisible = Check(label.Visible);
        label.SetVisible(false);
        bool hidden = Check(!label.Visible);
        bool hiddenPreservesText = Check(label.Text == "Persist me" && label.Length == 10);
        bool hiddenRender = Check(label.Render(null) == GuideXosResult.InvalidArgument);
        label.SetVisible(true);
        bool shown = Check(label.Visible && label.Text == "Persist me");
        bool repeatedVisibility = Check(label.Visible && label.Text == "Persist me");
        return defaultVisible && hidden && hiddenPreservesText && hiddenRender &&
            shown && repeatedVisibility;
    }

    private static bool Geometry()
    {
        GuideXosLabel label = new(16, 24, 160, "Geometry");
        bool valid = Check(label.TrySetBounds(32, 40, 240) && label.X == 32 &&
            label.Y == 40 && label.Width == 240 && label.RenderWidth == 30);
        int oldX = label.X;
        int oldY = label.Y;
        int oldWidth = label.Width;
        bool invalidX = Check(!label.TrySetBounds(-1, oldY, oldWidth) && label.X == oldX);
        bool invalidY = Check(!label.TrySetBounds(oldX, -1, oldWidth) && label.Y == oldY);
        bool invalidWidth = Check(!label.TrySetBounds(oldX, oldY, 7) &&
            label.Width == oldWidth);
        bool tooWide = Check(!label.TrySetBounds(oldX, oldY, 512 + 8) &&
            label.Width == oldWidth);
        bool unaligned = Check(!label.TrySetBounds(oldX, oldY, 81) &&
            label.Width == oldWidth);
        bool preserved = Check(label.Text == "Geometry" && label.X == oldX &&
            label.Y == oldY && label.Width == oldWidth);
        return valid && invalidX && invalidY && invalidWidth && tooWide &&
            unaligned && preserved;
    }

    private static bool Rendering(GuideXosSurface surface)
    {
        GuideXosLabel normal = new(16, 24, 160, "Visible text");
        GuideXosLabel empty = new(16, 42, 160);
        GuideXosLabel hidden = new(16, 60, 160, "Hidden text");
        GuideXosLabel clipped = new(16, 78, 80, "ABCDEFGHIJKLMN");
        hidden.SetVisible(false);
        bool surfaceKnown = Check(surface != null);
        bool normalRender = Check(surfaceKnown && normal.Render(surface) == GuideXosResult.Success);
        bool emptyRender = Check(surfaceKnown && empty.Render(surface) == GuideXosResult.Success);
        bool hiddenRender = Check(surfaceKnown && hidden.Render(surface) == GuideXosResult.Success);
        bool clippedRender = Check(surfaceKnown && clipped.Render(surface) == GuideXosResult.Success);
        bool clippedStored = Check(clipped.Text == "ABCDEFGHIJKLMN" && clipped.Length == 14);
        bool widthBound = Check(clipped.RenderWidth == 10 &&
            clipped.MaximumTextLength == GuideXosLabel.DefaultMaximumTextLength);
        bool updatedRender = Check(clipped.SetText("UPDATED") &&
            clipped.Render(surface) == GuideXosResult.Success && clipped.Text == "UPDATED");
        return surfaceKnown && normalRender && emptyRender && hiddenRender &&
            clippedRender && clippedStored && widthBound && updatedRender;
    }

    private static bool IndependentInstances()
    {
        GuideXosLabel first = new(16, 24, 80, "First");
        GuideXosLabel second = new(160, 24, 160, "Second");
        bool initial = Check(first.Text == "First" && second.Text == "Second");
        first.SetVisible(false);
        bool visibility = Check(!first.Visible && second.Visible);
        bool geometry = Check(first.X == 16 && second.X == 160 &&
            first.Width == 80 && second.Width == 160);
        bool update = Check(first.SetText("A") && first.Text == "A" &&
            second.Text == "Second");
        bool secondUpdate = Check(second.SetText("Long second") &&
            second.Text == "Long second" && first.Text == "A");
        first.SetVisible(true);
        bool revealIsolation = Check(first.Visible && second.Visible);
        bool firstGeometry = Check(first.TrySetBounds(24, 40, 96) &&
            first.X == 24 && second.X == 160);
        bool secondGeometry = Check(second.TrySetBounds(176, 40, 176) &&
            second.Width == 176 && first.Width == 96);
        return initial && visibility && geometry && update && secondUpdate &&
            revealIsolation && firstGeometry && secondGeometry;
    }
}
