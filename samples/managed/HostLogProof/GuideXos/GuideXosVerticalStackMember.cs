namespace HostLogProof;

public enum GuideXosVerticalStackHorizontalAlignment
{
    Stretch = 0,
    Left = 1,
    Center = 2,
    Right = 3,
}

internal static class GuideXosVerticalStackMemberLayoutRules
{
    public const int MaximumSupportedMargin = 1024;

    public static bool IsValidMargin(int value)
    {
        return value >= 0 && value <= MaximumSupportedMargin;
    }

    public static int ClampMargin(int value)
    {
        if (value < 0) return 0;
        return value > MaximumSupportedMargin
            ? MaximumSupportedMargin : value;
    }

    public static bool IsValidAlignment(
        GuideXosVerticalStackHorizontalAlignment value)
    {
        return value >= GuideXosVerticalStackHorizontalAlignment.Stretch &&
            value <= GuideXosVerticalStackHorizontalAlignment.Right;
    }
}

/// <summary>
/// The small geometry contract shared by the C141 leaf controls. It exposes
/// their existing authoritative bounds; it does not add a second rectangle.
/// </summary>
internal interface IGuideXosVerticalStackMember
{
    int X { get; }
    int Y { get; }
    int Width { get; }
    int Height { get; }
    int MarginLeft { get; }
    int MarginTop { get; }
    int MarginRight { get; }
    int MarginBottom { get; }
    GuideXosVerticalStackHorizontalAlignment HorizontalAlignment { get; }
    int MinimumWidth { get; }
    int MaximumWidth { get; }
    bool WidthRequiresCharacterAlignment { get; }
    bool Visible { get; }
    GuideXosPanel ParentPanel { get; }
    GuideXosVerticalStack VerticalStackOwner { get; }
    bool TrySetVerticalStackBounds(int x, int y, int width);
    bool TrySetVerticalStackBounds(int x, int y, int width, int height);
    bool TryAttachToVerticalStack(GuideXosVerticalStack stack);
    void DetachFromVerticalStack(GuideXosVerticalStack stack);
}
