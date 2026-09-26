namespace HostLogProof;

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
