namespace HostLogProof;

/// <summary>Deterministic focused probes for the structural GroupBox boundary.</summary>
public static class GuideXosGroupBoxTests
{
    private static int s_caseCount;

    public static bool Run(GuideXosHost host, GuideXosSurface surface)
    {
        s_caseCount = 0;
        bool result = Construction() && BoundsAndPreservation() &&
            CaptionStorage() && VisibilityAndDynamicGeometry(surface) &&
            RenderingAndGeometry(surface) && RelativeCoordinates() &&
            ResetAndIndependence(host);
        if (host != null)
        {
            host.TryLog(result
                ? "C126-GROUP-BOX-TESTS cases=60 result=PASS"u8
                : "C126-GROUP-BOX-TESTS cases=60 result=FAIL"u8);
        }
        return result && s_caseCount == 60;
    }

    private static bool Check(bool condition)
    {
        ++s_caseCount;
        return condition;
    }

    private static bool Construction()
    {
        GuideXosGroupBox box = new(16, 24, 160, 54);
        bool coordinates = Check(box.X == 16 && box.Y == 24);
        bool width = Check(box.Width == 160 && box.RenderWidth == 20);
        bool height = Check(box.Height == 54 && box.RenderRowCount == 3);
        bool defaults = Check(box.Visible && box.CaptionLength == 0 &&
            box.Caption == string.Empty);
        bool policy = Check(box.Focusable == false &&
            box.RejectedInputCount == 0u);
        bool capacities = Check(box.MaximumCaptionLength ==
            GuideXosGroupBox.MaximumSupportedCaptionLength &&
            GuideXosGroupBox.MaximumSupportedWidth == 504);
        bool heightBounds = Check(GuideXosGroupBox.MinimumSupportedHeight == 54 &&
            GuideXosGroupBox.MaximumSupportedHeight == 288);
        return coordinates && width && height && defaults && policy &&
            capacities && heightBounds;
    }

    private static bool BoundsAndPreservation()
    {
        GuideXosGroupBox box = new(16, 24, 160, 54);
        bool minimumWidth = Check(box.TrySetBounds(16, 24,
            GuideXosGroupBox.MinimumSupportedWidth, 54) &&
            box.Width == GuideXosGroupBox.MinimumSupportedWidth);
        bool maximumWidth = Check(box.TrySetBounds(16, 24,
            GuideXosGroupBox.MaximumSupportedWidth, 54) &&
            box.Width == GuideXosGroupBox.MaximumSupportedWidth);
        bool minimumHeight = Check(box.TrySetBounds(16, 24, 160,
            GuideXosGroupBox.MinimumSupportedHeight) && box.Height == 54);
        bool maximumHeight = Check(box.TrySetBounds(16, 24, 160,
            GuideXosGroupBox.MaximumSupportedHeight) && box.Height == 288);
        bool invalidX = Check(!box.TrySetBounds(-1, 24, 160, 54));
        bool invalidY = Check(!box.TrySetBounds(16, -1, 160, 54));
        bool smallWidth = Check(!box.TrySetBounds(16, 24,
            GuideXosGroupBox.MinimumSupportedWidth - 8, 54));
        bool largeWidth = Check(!box.TrySetBounds(16, 24,
            GuideXosGroupBox.MaximumSupportedWidth + 8, 54));
        bool smallHeight = Check(!box.TrySetBounds(16, 24, 160, 36));
        bool largeHeight = Check(!box.TrySetBounds(16, 24, 160, 306));
        bool xOverflow = Check(!box.TrySetBounds(
            GuideXosGroupBox.MaximumSupportedCoordinate - 160 + 1,
            24, 160, 54));
        bool yOverflow = Check(!box.TrySetBounds(16,
            GuideXosGroupBox.MaximumSupportedCoordinate - 54 + 1,
            160, 54));
        bool unalignedWidth = Check(!box.TrySetBounds(16, 24, 161, 54));
        bool unalignedHeight = Check(!box.TrySetBounds(16, 24, 160, 55));
        int oldX = box.X;
        int oldY = box.Y;
        int oldWidth = box.Width;
        int oldHeight = box.Height;
        bool preserved = Check(box.X == oldX && box.Y == oldY &&
            box.Width == oldWidth && box.Height == oldHeight);
        return minimumWidth && maximumWidth && minimumHeight && maximumHeight &&
            invalidX && invalidY && smallWidth && largeWidth && smallHeight &&
            largeHeight && xOverflow && yOverflow && unalignedWidth &&
            unalignedHeight && preserved;
    }

    private static bool CaptionStorage()
    {
        GuideXosGroupBox box = new(16, 24, 160, 54);
        bool empty = Check(box.TrySetCaption(string.Empty) &&
            box.CaptionLength == 0);
        bool valid = Check(box.TrySetCaption("Path Display") &&
            box.Caption == "Path Display");
        string maximum = new('A', GuideXosGroupBox.MaximumSupportedCaptionLength);
        bool maximumAccepted = Check(box.TrySetCaption(maximum) &&
            box.CaptionLength == GuideXosGroupBox.MaximumSupportedCaptionLength);
        string prior = box.Caption;
        bool overlong = Check(!box.TrySetCaption(prior + "A") &&
            box.Caption == prior);
        bool rejectionPreserved = Check(!box.TrySetCaption("bad\ncaption") &&
            box.Caption == prior);
        bool nullRejected = Check(!box.TrySetCaption((string)null) &&
            box.Caption == prior);
        bool clear = Check(box.ClearCaption() && box.CaptionLength == 0);
        bool afterClear = Check(box.TrySetCaption("Document") &&
            box.Caption == "Document");
        return empty && valid && maximumAccepted && overlong &&
            rejectionPreserved && nullRejected && clear && afterClear;
    }

    private static bool VisibilityAndDynamicGeometry(GuideXosSurface surface)
    {
        GuideXosGroupBox box = new(16, 24, 160, 72, "Document");
        bool visibleRender = Check(surface != null);
        box.SetVisible(false);
        bool hiddenRender = Check(!box.Visible && box.Render(surface) ==
            GuideXosResult.Success);
        bool hiddenGeometry = Check(box.X == 16 && box.Y == 24 &&
            box.Width == 160 && box.Height == 72);
        bool hiddenCaption = Check(box.Caption == "Document");
        box.SetVisible(true);
        bool shown = Check(box.Visible);
        bool grownWidth = Check(box.TrySetBounds(16, 24, 320, 72) &&
            box.Width == 320);
        bool shrunkWidth = Check(box.TrySetBounds(16, 24, 96, 72) &&
            box.Width == 96);
        bool grownHeight = Check(box.TrySetBounds(16, 24, 96, 144) &&
            box.Height == 144);
        bool shrunkHeight = Check(box.TrySetBounds(16, 24, 96, 54) &&
            box.Height == 54);
        bool moved = Check(box.TrySetBounds(48, 60, 96, 54) &&
            box.X == 48 && box.Y == 60);
        bool captionAfterResize = Check(box.RenderCaptionLength == 6 &&
            box.Caption == "Document");
        bool noStaleBorder = Check(box.Render(surface) == GuideXosResult.Success &&
            box.RenderWidth == 12 && box.RenderRowCount == 3 &&
            box.Caption == "Document");
        return visibleRender && hiddenRender && hiddenGeometry && hiddenCaption &&
            shown && grownWidth && shrunkWidth && grownHeight && shrunkHeight &&
            moved && captionAfterResize && noStaleBorder;
    }

    private static bool RenderingAndGeometry(GuideXosSurface surface)
    {
        GuideXosGroupBox box = new(20, 200, 64, 54, "Long Caption");
        bool shortCaption = Check(box.TrySetCaption("A") &&
            box.RenderCaptionLength == 1);
        string exact = new('E', box.RenderWidth - 6);
        bool exactFit = Check(box.TrySetCaption(exact) &&
            box.RenderCaptionLength == exact.Length);
        string stored = new('C', GuideXosGroupBox.MaximumSupportedCaptionLength);
        bool clipped = Check(box.TrySetCaption(stored) &&
            box.RenderCaptionLength < box.CaptionLength);
        bool storageUnchanged = Check(box.Caption == stored &&
            box.CaptionLength == stored.Length && box.RenderCaptionLength == 2);
        bool nullSurface = Check(box.Render(null) == GuideXosResult.InvalidArgument);
        bool uncaptained = Check(box.ClearCaption() &&
            box.RenderCaptionLength == 0 && box.Render(surface) ==
            GuideXosResult.Success);
        return shortCaption && exactFit && clipped && storageUnchanged &&
            nullSurface && uncaptained;
    }

    private static bool RelativeCoordinates()
    {
        GuideXosGroupBox box = new(20, 80, 240, 72, "Path Display");
        bool valid = Check(box.TryResolvePoint(12, 24,
            out int absoluteX, out int absoluteY) &&
            absoluteX == 32 && absoluteY == 104);
        bool origin = Check(box.TryResolvePoint(0, 0,
            out absoluteX, out absoluteY) && absoluteX == 20 &&
            absoluteY == 80);
        bool bottomRight = Check(box.TryResolvePoint(box.Width - 1,
            box.Height - 1, out absoluteX, out absoluteY) &&
            absoluteX == 259 && absoluteY == 151);
        bool invalidX = Check(!box.TryResolvePoint(box.Width, 0,
            out _, out _));
        bool invalidY = Check(!box.TryResolvePoint(0, box.Height,
            out _, out _));
        GuideXosGroupBox edge = new(4095 - 64, 4095 - 54, 64, 54);
        bool overflow = Check(!edge.TryResolvePoint(64, 0,
            out _, out _) && !edge.TryResolvePoint(0, 54,
            out _, out _));
        int oldX = box.X;
        int oldY = box.Y;
        int oldRejected = (int)box.RejectedInputCount;
        box.TryResolvePoint(-1, 0, out _, out _);
        bool noMutation = Check(box.X == oldX && box.Y == oldY &&
            box.RejectedInputCount == (uint)(oldRejected + 1));
        bool containment = Check(box.ContainsPoint(20, 80) &&
            box.ContainsPoint(259, 151) && !box.ContainsPoint(260, 151));
        return valid && origin && bottomRight && invalidX && invalidY &&
            overflow && noMutation && containment;
    }

    private static bool ResetAndIndependence(GuideXosHost host)
    {
        GuideXosGroupBox first = new(16, 24, 160, 54, "First");
        GuideXosGroupBox second = new(200, 42, 240, 72, "Second");
        first.SetVisible(false);
        first.TrySetBounds(32, 60, 96, 54);
        bool independent = Check(!first.Visible && second.Visible &&
            first.Caption == "First" && second.Caption == "Second" &&
            first.Width == 96 && second.Width == 240 &&
            first.ContainsPoint(32, 60) && !second.ContainsPoint(32, 60) &&
            second.TryResolvePoint(4, 5, out int x, out int y) &&
            x == 204 && y == 47);
        first.Reset();
        bool reset = Check(first.Visible && first.Caption == "First" &&
            first.X == 32 && first.Y == 60 && first.RejectedInputCount == 0u);
        GuideXosControlHost focusHost = new(1);
        GuideXosRadioButton registered = new(0, 0, 64, 28, "R");
        focusHost.TryRegisterRadioButton(1, registered);
        int registrationCount = focusHost.RegistrationCount;
        bool noRegistration = Check(registrationCount == 1 &&
            focusHost.RegistrationCount == registrationCount && !first.Focusable);
        bool hostProvided = Check(host != null);
        return independent && reset && noRegistration && hostProvided;
    }
}
