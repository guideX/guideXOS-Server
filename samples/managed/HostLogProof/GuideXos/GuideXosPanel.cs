using System;

namespace HostLogProof;

public enum GuideXosPanelResult
{
    Added = 0,
    Removed = 1,
    Cleared = 2,
    Empty = 3,
    Duplicate = 4,
    CapacityReached = 5,
    InvalidChild = 6,
    MembershipConflict = 7,
    InvalidPosition = 8,
    OutOfBounds = 9,
    Moved = 10,
    NotMember = 11,
}

/// <summary>
/// A fixed-capacity, single-level managed composition boundary. A Panel stores
/// local child positions and temporarily contributes parent visibility; it does
/// not own child lifetime, focus, input routing, or ControlHost registration.
/// </summary>
public sealed class GuideXosPanel
{
    public const int CharacterWidth = 8;
    public const int TextRowHeight = 18;
    public const int MinimumSupportedCoordinate = 0;
    public const int MaximumSupportedCoordinate = 4095;
    public const int MinimumSupportedWidth = CharacterWidth;
    public const int MaximumSupportedWidth = 504;
    public const int MinimumSupportedHeight = TextRowHeight;
    public const int MaximumSupportedHeight = 288;
    public const int DefaultMaximumChildCount = 4;
    public const int MaximumSupportedChildCount = 4;

    private enum ChildKind
    {
        None = 0,
        Button = 1,
        CheckBox = 2,
        Label = 3,
        Separator = 4,
        RadioButton = 5,
        ProgressBar = 6,
        ComboBox = 7,
    }

    private struct ChildEntry
    {
        public object Child;
        public ChildKind Kind;
        public int LocalX;
        public int LocalY;
    }

    private readonly ChildEntry[] _children =
        new ChildEntry[MaximumSupportedChildCount];
    private readonly int _capacity;
    private int _childCount;
    private int _x;
    private int _y;
    private int _width;
    private int _height;
    private bool _visible = true;
    private uint _rejectedInputCount;

    public GuideXosPanel(
        int x,
        int y,
        int width,
        int height,
        int maximumChildCount = DefaultMaximumChildCount)
    {
        _capacity = maximumChildCount < 1 ||
            maximumChildCount > MaximumSupportedChildCount
            ? DefaultMaximumChildCount
            : maximumChildCount;
        _x = MinimumSupportedCoordinate;
        _y = MinimumSupportedCoordinate;
        _width = MinimumSupportedWidth;
        _height = MinimumSupportedHeight;
        TrySetBounds(x, y, width, height);
    }

    public int X => _x;
    public int Y => _y;
    public int Width => _width;
    public int Height => _height;
    public int MaximumChildCount => _capacity;
    public int ChildCount => _childCount;
    public bool Visible => _visible;
    public bool Focusable => false;
    public uint RejectedInputCount => _rejectedInputCount;

    /// <summary>
    /// Changes panel geometry only when every existing child still fits. Child
    /// bounds are recomputed from stored local positions, so movement never
    /// accumulates rounding or translation drift.
    /// </summary>
    public bool TrySetBounds(int x, int y, int width, int height)
    {
        if (!IsValidBounds(x, y, width, height))
        {
            ++_rejectedInputCount;
            return false;
        }
        for (int index = 0; index < _childCount; index++)
        {
            if (!Fits(_children[index], x, y, width, height))
            {
                ++_rejectedInputCount;
                return false;
            }
        }

        for (int index = 0; index < _childCount; index++)
        {
            ChildEntry entry = _children[index];
            if (!TrySetChildBounds(entry.Kind, entry.Child,
                    x + entry.LocalX, y + entry.LocalY))
            {
                ++_rejectedInputCount;
                return false;
            }
        }

        _x = x;
        _y = y;
        _width = width;
        _height = height;
        return true;
    }

    /// <summary>
    /// Parent visibility is combined with each child's local visibility. A
    /// child that is locally hidden remains hidden after the panel is shown.
    /// </summary>
    public void SetVisible(bool visible)
    {
        _visible = visible;
        for (int index = 0; index < _childCount; index++)
        {
            SetChildPanelVisible(_children[index].Kind,
                _children[index].Child, visible);
        }
    }

    /// <summary>
    /// Adds one supported child at a local pixel position. The child retains
    /// its existing size; the complete child rectangle must fit in the panel.
    /// </summary>
    public GuideXosPanelResult TryAddChild(object child, int localX, int localY)
    {
        if (!TryGetKind(child, out ChildKind kind))
        {
            ++_rejectedInputCount;
            return GuideXosPanelResult.InvalidChild;
        }
        GuideXosPanel owner = GetParentPanel(kind, child);
        if (owner == this)
        {
            ++_rejectedInputCount;
            return GuideXosPanelResult.Duplicate;
        }
        if (owner != null)
        {
            ++_rejectedInputCount;
            return GuideXosPanelResult.MembershipConflict;
        }
        if (_childCount >= _capacity)
        {
            ++_rejectedInputCount;
            return GuideXosPanelResult.CapacityReached;
        }
        if (localX < 0 || localY < 0)
        {
            ++_rejectedInputCount;
            return GuideXosPanelResult.InvalidPosition;
        }

        ChildEntry entry = new()
        {
            Child = child,
            Kind = kind,
            LocalX = localX,
            LocalY = localY,
        };
        if (!Fits(entry, _x, _y, _width, _height))
        {
            ++_rejectedInputCount;
            return GuideXosPanelResult.OutOfBounds;
        }
        if (!TrySetChildBounds(kind, child, _x + localX, _y + localY) ||
            !AttachToPanel(kind, child, this))
        {
            ++_rejectedInputCount;
            return GuideXosPanelResult.InvalidChild;
        }
        _children[_childCount++] = entry;
        SetChildPanelVisible(kind, child, _visible);
        return GuideXosPanelResult.Added;
    }

    /// <summary>
    /// Changes one member's local position without changing its size.
    /// </summary>
    public GuideXosPanelResult TrySetChildPosition(
        object child, int localX, int localY)
    {
        int index = FindChildIndex(child);
        if (index < 0)
        {
            ++_rejectedInputCount;
            return GuideXosPanelResult.NotMember;
        }
        if (localX < 0 || localY < 0)
        {
            ++_rejectedInputCount;
            return GuideXosPanelResult.InvalidPosition;
        }
        ChildEntry entry = _children[index];
        entry.LocalX = localX;
        entry.LocalY = localY;
        if (!Fits(entry, _x, _y, _width, _height))
        {
            ++_rejectedInputCount;
            return GuideXosPanelResult.OutOfBounds;
        }
        if (!TrySetChildBounds(entry.Kind, entry.Child,
                _x + localX, _y + localY))
        {
            ++_rejectedInputCount;
            return GuideXosPanelResult.OutOfBounds;
        }
        _children[index] = entry;
        return GuideXosPanelResult.Moved;
    }

    /// <summary>Removes a member without disposing or otherwise destroying it.</summary>
    public GuideXosPanelResult TryRemoveChild(object child)
    {
        int index = FindChildIndex(child);
        if (index < 0)
        {
            ++_rejectedInputCount;
            return GuideXosPanelResult.NotMember;
        }

        ChildEntry entry = _children[index];
        DetachFromPanel(entry.Kind, entry.Child);
        for (int move = index + 1; move < _childCount; move++)
        {
            _children[move - 1] = _children[move];
        }
        _children[--_childCount] = default;
        return GuideXosPanelResult.Removed;
    }

    /// <summary>
    /// Clears membership in insertion order. Children remain live and reusable.
    /// </summary>
    public GuideXosPanelResult Clear()
    {
        if (_childCount == 0) return GuideXosPanelResult.Empty;
        for (int index = 0; index < _childCount; index++)
        {
            DetachFromPanel(_children[index].Kind, _children[index].Child);
            _children[index] = default;
        }
        _childCount = 0;
        return GuideXosPanelResult.Cleared;
    }

    public bool ContainsChild(object child)
    {
        return FindChildIndex(child) >= 0;
    }

    /// <summary>
    /// Returns the child in deterministic insertion order, or null when the
    /// index is outside the current membership range.
    /// </summary>
    public object GetChildAt(int index)
    {
        return index >= 0 && index < _childCount
            ? _children[index].Child : null;
    }

    /// <summary>Uses a half-open geometry-only rectangle.</summary>
    public bool ContainsPoint(int x, int y)
    {
        return x >= _x && x < _x + _width &&
            y >= _y && y < _y + _height;
    }

    /// <summary>Resolves a local point without claiming input ownership.</summary>
    public bool TryResolvePoint(
        int relativeX, int relativeY,
        out int absoluteX, out int absoluteY)
    {
        absoluteX = 0;
        absoluteY = 0;
        if (relativeX < 0 || relativeY < 0 ||
            relativeX >= _width || relativeY >= _height ||
            relativeX > MaximumSupportedCoordinate - _x ||
            relativeY > MaximumSupportedCoordinate - _y)
        {
            ++_rejectedInputCount;
            return false;
        }
        absoluteX = _x + relativeX;
        absoluteY = _y + relativeY;
        return true;
    }

    /// <summary>Renders members in deterministic insertion order; the Panel has no frame.</summary>
    public GuideXosResult Render(GuideXosSurface surface)
    {
        if (surface == null) return GuideXosResult.InvalidArgument;
        if (!_visible) return GuideXosResult.Success;
        for (int index = 0; index < _childCount; index++)
        {
            GuideXosResult result = RenderChild(
                _children[index].Kind, _children[index].Child, surface);
            if (result != GuideXosResult.Success) return result;
        }
        return GuideXosResult.Success;
    }

    /// <summary>Restores visibility and diagnostics while retaining membership and geometry.</summary>
    public void Reset()
    {
        _visible = true;
        _rejectedInputCount = 0u;
        for (int index = 0; index < _childCount; index++)
        {
            SetChildPanelVisible(_children[index].Kind,
                _children[index].Child, true);
        }
    }

    private static bool IsValidBounds(
        int x, int y, int width, int height)
    {
        return x >= MinimumSupportedCoordinate &&
            y >= MinimumSupportedCoordinate &&
            width >= MinimumSupportedWidth &&
            width <= MaximumSupportedWidth &&
            width % CharacterWidth == 0 &&
            height >= MinimumSupportedHeight &&
            height <= MaximumSupportedHeight &&
            height % TextRowHeight == 0 &&
            x <= MaximumSupportedCoordinate - width &&
            y <= MaximumSupportedCoordinate - height;
    }

    private static bool Fits(
        ChildEntry entry, int x, int y, int width, int height)
    {
        GetChildSize(entry.Kind, entry.Child, out int childWidth,
            out int childHeight);
        return entry.LocalX <= width - childWidth &&
            entry.LocalY <= height - childHeight &&
            x <= MaximumSupportedCoordinate - entry.LocalX - childWidth &&
            y <= MaximumSupportedCoordinate - entry.LocalY - childHeight;
    }

    private int FindChildIndex(object child)
    {
        for (int index = 0; index < _childCount; index++)
        {
            if (ReferenceEquals(_children[index].Child, child)) return index;
        }
        return -1;
    }

    private static bool TryGetKind(object child, out ChildKind kind)
    {
        kind = child switch
        {
            GuideXosButton => ChildKind.Button,
            GuideXosCheckBox => ChildKind.CheckBox,
            GuideXosLabel => ChildKind.Label,
            GuideXosSeparator => ChildKind.Separator,
            GuideXosRadioButton => ChildKind.RadioButton,
            GuideXosProgressBar => ChildKind.ProgressBar,
            GuideXosComboBox => ChildKind.ComboBox,
            _ => ChildKind.None,
        };
        return kind != ChildKind.None;
    }

    private static GuideXosPanel GetParentPanel(ChildKind kind, object child)
    {
        return kind switch
        {
            ChildKind.Button => ((GuideXosButton)child).ParentPanel,
            ChildKind.CheckBox => ((GuideXosCheckBox)child).ParentPanel,
            ChildKind.Label => ((GuideXosLabel)child).ParentPanel,
            ChildKind.Separator => ((GuideXosSeparator)child).ParentPanel,
            ChildKind.RadioButton => ((GuideXosRadioButton)child).ParentPanel,
            ChildKind.ProgressBar => ((GuideXosProgressBar)child).ParentPanel,
            ChildKind.ComboBox => ((GuideXosComboBox)child).ParentPanel,
            _ => null,
        };
    }

    private static void GetChildSize(
        ChildKind kind, object child, out int width, out int height)
    {
        switch (kind)
        {
            case ChildKind.Button:
                width = ((GuideXosButton)child).Width;
                height = ((GuideXosButton)child).Height;
                break;
            case ChildKind.CheckBox:
                width = ((GuideXosCheckBox)child).Width;
                height = ((GuideXosCheckBox)child).Height;
                break;
            case ChildKind.Label:
                width = ((GuideXosLabel)child).Width;
                height = ((GuideXosLabel)child).Height;
                break;
            case ChildKind.Separator:
                width = ((GuideXosSeparator)child).Width;
                height = ((GuideXosSeparator)child).Height;
                break;
            case ChildKind.RadioButton:
                width = ((GuideXosRadioButton)child).Width;
                height = ((GuideXosRadioButton)child).Height;
                break;
            case ChildKind.ProgressBar:
                width = ((GuideXosProgressBar)child).Width;
                height = ((GuideXosProgressBar)child).Height;
                break;
            case ChildKind.ComboBox:
                width = ((GuideXosComboBox)child).Width;
                height = ((GuideXosComboBox)child).Height;
                break;
            default:
                width = 0;
                height = 0;
                break;
        }
    }

    private static bool TrySetChildBounds(
        ChildKind kind, object child, int x, int y)
    {
        return kind switch
        {
            ChildKind.Button => ((GuideXosButton)child).TrySetPanelBounds(
                x, y, ((GuideXosButton)child).Width,
                ((GuideXosButton)child).Height),
            ChildKind.CheckBox => ((GuideXosCheckBox)child).TrySetPanelBounds(
                x, y, ((GuideXosCheckBox)child).Width,
                ((GuideXosCheckBox)child).Height),
            ChildKind.Label => ((GuideXosLabel)child).TrySetPanelBounds(
                x, y, ((GuideXosLabel)child).Width),
            ChildKind.Separator => ((GuideXosSeparator)child).TrySetPanelBounds(
                x, y, ((GuideXosSeparator)child).Width),
            ChildKind.RadioButton => ((GuideXosRadioButton)child).TrySetPanelBounds(
                x, y, ((GuideXosRadioButton)child).Width,
                ((GuideXosRadioButton)child).Height),
            ChildKind.ProgressBar => ((GuideXosProgressBar)child).TrySetPanelBounds(
                x, y, ((GuideXosProgressBar)child).Width),
            ChildKind.ComboBox => ((GuideXosComboBox)child).TrySetPanelBounds(
                x, y, ((GuideXosComboBox)child).Width,
                ((GuideXosComboBox)child).Height),
            _ => false,
        };
    }

    private static bool AttachToPanel(
        ChildKind kind, object child, GuideXosPanel panel)
    {
        return kind switch
        {
            ChildKind.Button => ((GuideXosButton)child).TryAttachToPanel(panel),
            ChildKind.CheckBox => ((GuideXosCheckBox)child).TryAttachToPanel(panel),
            ChildKind.Label => ((GuideXosLabel)child).TryAttachToPanel(panel),
            ChildKind.Separator => ((GuideXosSeparator)child).TryAttachToPanel(panel),
            ChildKind.RadioButton => ((GuideXosRadioButton)child).TryAttachToPanel(panel),
            ChildKind.ProgressBar => ((GuideXosProgressBar)child).TryAttachToPanel(panel),
            ChildKind.ComboBox => ((GuideXosComboBox)child).TryAttachToPanel(panel),
            _ => false,
        };
    }

    private static void SetChildPanelVisible(
        ChildKind kind, object child, bool visible)
    {
        switch (kind)
        {
            case ChildKind.Button:
                ((GuideXosButton)child).SetPanelVisible(visible);
                break;
            case ChildKind.CheckBox:
                ((GuideXosCheckBox)child).SetPanelVisible(visible);
                break;
            case ChildKind.Label:
                ((GuideXosLabel)child).SetPanelVisible(visible);
                break;
            case ChildKind.Separator:
                ((GuideXosSeparator)child).SetPanelVisible(visible);
                break;
            case ChildKind.RadioButton:
                ((GuideXosRadioButton)child).SetPanelVisible(visible);
                break;
            case ChildKind.ProgressBar:
                ((GuideXosProgressBar)child).SetPanelVisible(visible);
                break;
            case ChildKind.ComboBox:
                ((GuideXosComboBox)child).SetPanelVisible(visible);
                break;
        }
    }

    private static void DetachFromPanel(ChildKind kind, object child)
    {
        switch (kind)
        {
            case ChildKind.Button:
                ((GuideXosButton)child).DetachFromPanel();
                break;
            case ChildKind.CheckBox:
                ((GuideXosCheckBox)child).DetachFromPanel();
                break;
            case ChildKind.Label:
                ((GuideXosLabel)child).DetachFromPanel();
                break;
            case ChildKind.Separator:
                ((GuideXosSeparator)child).DetachFromPanel();
                break;
            case ChildKind.RadioButton:
                ((GuideXosRadioButton)child).DetachFromPanel();
                break;
            case ChildKind.ProgressBar:
                ((GuideXosProgressBar)child).DetachFromPanel();
                break;
            case ChildKind.ComboBox:
                ((GuideXosComboBox)child).DetachFromPanel();
                break;
        }
    }

    private static GuideXosResult RenderChild(
        ChildKind kind, object child, GuideXosSurface surface)
    {
        return kind switch
        {
            ChildKind.Button => ((GuideXosButton)child).Render(surface),
            ChildKind.CheckBox => ((GuideXosCheckBox)child).Render(surface),
            ChildKind.Label => ((GuideXosLabel)child).Render(surface),
            ChildKind.Separator => ((GuideXosSeparator)child).Render(surface),
            ChildKind.RadioButton => ((GuideXosRadioButton)child).Render(surface),
            ChildKind.ProgressBar => ((GuideXosProgressBar)child).Render(surface),
            ChildKind.ComboBox => ((GuideXosComboBox)child).Render(surface),
            _ => GuideXosResult.InvalidArgument,
        };
    }
}
