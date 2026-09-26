using System;

namespace HostLogProof;

public enum GuideXosScrollViewResult
{
    Ignored = 0,
    Added = 1,
    Removed = 2,
    Cleared = 3,
    Empty = 4,
    Duplicate = 5,
    CapacityReached = 6,
    InvalidMember = 7,
    MembershipConflict = 8,
    InvalidPosition = 9,
    OutOfBounds = 10,
    Moved = 11,
    NotMember = 12,
    Scrolled = 13,
    Focused = 14,
    Activated = 15,
    Toggled = 16,
    Selected = 17,
    Disabled = 18,
    Rejected = 19,
    Paged = 20,
}

/// <summary>
/// A bounded, single-level, non-owning vertical content viewport. Members are
/// ordinary managed controls with stable content-space bounds. The ScrollView
/// owns only membership metadata, the shared viewport, and presentation routing;
/// callers retain lifetime and application ownership of every member.
/// </summary>
public sealed class GuideXosScrollView
{
    public const int BorderPixels = 1;
    public const int CharacterWidth = 8;
    public const int TextRowHeight = 18;
    public const int MinimumSupportedCoordinate = 0;
    public const int MaximumSupportedCoordinate = 4095;
    public const int MinimumSupportedWidth = 24;
    public const int MaximumSupportedWidth = 504;
    public const int MinimumSupportedHeight = 20;
    public const int MaximumSupportedHeight = 288;
    public const int DefaultMaximumMemberCount = 8;
    public const int MaximumSupportedMemberCount = 8;

    private enum MemberKind
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

    private struct MemberEntry
    {
        public object Member;
        public MemberKind Kind;
        public int LogicalX;
        public int LogicalY;
    }

    private readonly MemberEntry[] _members =
        new MemberEntry[MaximumSupportedMemberCount];
    private readonly int _capacity;
    private readonly GuideXosVerticalViewport _viewport;
    private int _memberCount;
    private int _x;
    private int _y;
    private int _width;
    private int _height;
    private bool _visible = true;
    private bool _enabled = true;
    private int _focusedIndex = -1;
    private object _lastTargetMember;
    private object _lastActivatedMember;
    private uint _rejectedInputCount;
    private GuideXosScrollBar _boundScrollBar;

    public GuideXosScrollView(
        int x,
        int y,
        int width,
        int height,
        int maximumMemberCount = DefaultMaximumMemberCount)
    {
        _capacity = maximumMemberCount < 1 ||
            maximumMemberCount > MaximumSupportedMemberCount
            ? DefaultMaximumMemberCount : maximumMemberCount;
        _viewport = new GuideXosVerticalViewport();
        _x = MinimumSupportedCoordinate;
        _y = MinimumSupportedCoordinate;
        _width = MinimumSupportedWidth;
        _height = MinimumSupportedHeight;
        TrySetBounds(x, y, width, height);
        RecalculateContentExtent();
    }

    public int X => _x;
    public int Y => _y;
    public int Width => _width;
    public int Height => _height;
    public int InnerX => _x + BorderPixels;
    public int InnerY => _y + BorderPixels;
    public int InnerWidth => Math.Max(0, _width - BorderPixels * 2);
    public int InnerHeight => Math.Max(0, _height - BorderPixels * 2);
    public int MaximumMemberCount => _capacity;
    public int MemberCount => _memberCount;
    public bool Visible => _visible;
    public bool Enabled => _enabled;
    public bool EffectiveVisible => _visible;
    public bool HasFocus => _focusedIndex >= 0 && IsFocusableEligible(_focusedIndex);
    public int FocusedMemberIndex => HasFocus ? _focusedIndex : -1;
    public object FocusedMember => HasFocus ? _members[_focusedIndex].Member : null;
    public object LastTargetMember => _lastTargetMember;
    public object LastActivatedMember => _lastActivatedMember;
    public uint RejectedInputCount => _rejectedInputCount;
    public GuideXosVerticalViewport VerticalViewport => _viewport;
    public int ContentExtent => _viewport.ContentExtent;
    public int VisibleExtent => _viewport.VisibleExtent;
    public int Offset => _viewport.Offset;
    public int MaximumOffset => _viewport.MaximumOffset;
    public uint BackgroundColor { get; set; } = 0x003D465Au;
    public uint BorderColor { get; set; } = 0x0095A8C4u;

    /// <summary>Changes only the outer surface. Existing logical member positions remain stable.</summary>
    public bool TrySetBounds(int x, int y, int width, int height)
    {
        if (!IsValidBounds(x, y, width, height))
        {
            ++_rejectedInputCount;
            return false;
        }
        for (int index = 0; index < _memberCount; index++)
        {
            if (!Fits(_members[index], x, y, width, height))
            {
                ++_rejectedInputCount;
                return false;
            }
        }
        for (int index = 0; index < _memberCount; index++)
        {
            MemberEntry entry = _members[index];
            if (!SetMemberBounds(entry.Kind, entry.Member, x + BorderPixels + entry.LogicalX,
                    y + BorderPixels + entry.LogicalY))
            {
                ++_rejectedInputCount;
                return false;
            }
        }
        _x = x;
        _y = y;
        _width = width;
        _height = height;
        _viewport.VisibleExtent = InnerHeight;
        return true;
    }

    public void SetVisible(bool visible)
    {
        _visible = visible;
        if (!visible) Blur();
    }

    public void SetEnabled(bool enabled)
    {
        _enabled = enabled;
        if (!enabled) Blur();
    }

    /// <summary>
    /// Adds a supported ordinary control at a logical content position. The
    /// control remains caller-owned; direct nested containers and scrollable
    /// controls are intentionally not supported in C140.
    /// </summary>
    public GuideXosScrollViewResult TryAddMember(
        object member, int logicalX, int logicalY)
    {
        if (!TryGetKind(member, out MemberKind kind))
            return Reject(GuideXosScrollViewResult.InvalidMember);
        if (FindMemberIndex(member) >= 0)
            return Reject(GuideXosScrollViewResult.Duplicate);
        if (GetParentPanel(kind, member) != null ||
            GetParentScrollView(kind, member) != null)
            return Reject(GuideXosScrollViewResult.MembershipConflict);
        if (_memberCount >= _capacity)
            return Reject(GuideXosScrollViewResult.CapacityReached);
        if (logicalX < 0 || logicalY < 0)
            return Reject(GuideXosScrollViewResult.InvalidPosition);

        MemberEntry entry = new()
        {
            Member = member,
            Kind = kind,
            LogicalX = logicalX,
            LogicalY = logicalY,
        };
        if (!Fits(entry, _x, _y, _width, _height))
            return Reject(GuideXosScrollViewResult.OutOfBounds);
        if (!AttachMember(kind, member) ||
            !SetMemberBounds(kind, member, InnerX + logicalX, InnerY + logicalY))
        {
            DetachMember(kind, member);
            return Reject(GuideXosScrollViewResult.InvalidMember);
        }

        _members[_memberCount++] = entry;
        RecalculateContentExtent();
        return GuideXosScrollViewResult.Added;
    }

    public GuideXosScrollViewResult TrySetMemberPosition(
        object member, int logicalX, int logicalY)
    {
        int index = FindMemberIndex(member);
        if (index < 0) return Reject(GuideXosScrollViewResult.NotMember);
        if (logicalX < 0 || logicalY < 0)
            return Reject(GuideXosScrollViewResult.InvalidPosition);
        MemberEntry entry = _members[index];
        entry.LogicalX = logicalX;
        entry.LogicalY = logicalY;
        if (!Fits(entry, _x, _y, _width, _height) ||
            !SetMemberBounds(entry.Kind, entry.Member,
                InnerX + logicalX, InnerY + logicalY))
            return Reject(GuideXosScrollViewResult.OutOfBounds);
        _members[index] = entry;
        RecalculateContentExtent();
        return GuideXosScrollViewResult.Moved;
    }

    public GuideXosScrollViewResult TryRemoveMember(object member)
    {
        int index = FindMemberIndex(member);
        if (index < 0) return Reject(GuideXosScrollViewResult.NotMember);
        if (_focusedIndex == index) Blur();
        else if (_focusedIndex > index) --_focusedIndex;
        DetachMember(_members[index].Kind, _members[index].Member);
        for (int move = index + 1; move < _memberCount; move++)
            _members[move - 1] = _members[move];
        _members[--_memberCount] = default;
        RecalculateContentExtent();
        return GuideXosScrollViewResult.Removed;
    }

    public GuideXosScrollViewResult Clear()
    {
        if (_memberCount == 0) return GuideXosScrollViewResult.Empty;
        Blur();
        for (int index = 0; index < _memberCount; index++)
        {
            DetachMember(_members[index].Kind, _members[index].Member);
            _members[index] = default;
        }
        _memberCount = 0;
        RecalculateContentExtent();
        return GuideXosScrollViewResult.Cleared;
    }

    public bool ContainsMember(object member) => FindMemberIndex(member) >= 0;

    public object GetMemberAt(int index)
    {
        return index >= 0 && index < _memberCount ? _members[index].Member : null;
    }

    public bool TryGetLogicalBounds(object member, out int x, out int y,
        out int width, out int height)
    {
        int index = FindMemberIndex(member);
        if (index < 0)
        {
            x = y = width = height = 0;
            return false;
        }
        MemberEntry entry = _members[index];
        x = entry.LogicalX;
        y = entry.LogicalY;
        GetMemberSize(entry.Kind, entry.Member, out width, out height);
        return true;
    }

    /// <summary>Returns the visible screen intersection after the current offset.</summary>
    public bool TryGetVisibleMemberBounds(object member, out int x, out int y,
        out int width, out int height)
    {
        x = y = width = height = 0;
        if (!TryGetLogicalBounds(member, out int logicalX, out int logicalY,
                out int memberWidth, out int memberHeight) ||
            !IsMemberVisible(member)) return false;
        x = InnerX + logicalX;
        y = InnerY + logicalY - _viewport.Offset;
        width = memberWidth;
        height = memberHeight;
        int top = Math.Max(y, InnerY);
        int bottom = Math.Min(y + height, InnerY + InnerHeight);
        int left = Math.Max(x, InnerX);
        int right = Math.Min(x + width, InnerX + InnerWidth);
        x = left;
        y = top;
        width = right - left;
        height = bottom - top;
        return width > 0 && height > 0;
    }

    public bool TryHitTest(int x, int y, out object member)
    {
        member = null;
        if (!_visible || x < InnerX || x >= InnerX + InnerWidth ||
            y < InnerY || y >= InnerY + InnerHeight) return false;
        int contentX = x - InnerX;
        int contentY = y - InnerY + _viewport.Offset;
        for (int index = _memberCount - 1; index >= 0; index--)
        {
            MemberEntry entry = _members[index];
            if (!IsMemberVisible(index)) continue;
            GetMemberSize(entry.Kind, entry.Member, out int width, out int height);
            if (contentX >= entry.LogicalX && contentX < entry.LogicalX + width &&
                contentY >= entry.LogicalY && contentY < entry.LogicalY + height)
            {
                member = entry.Member;
                return true;
            }
        }
        return false;
    }

    public bool SetOffset(int offset) => _viewport.SetOffset(offset);
    public bool ScrollSmall(int delta) => _viewport.ScrollSmall(delta);
    public bool ScrollPage(int delta) => _viewport.ScrollLarge(delta);
    public bool EnsureMemberVisible(object member)
    {
        int index = FindMemberIndex(member);
        return index >= 0 && EnsureMemberVisible(index);
    }

    public bool EnsureRectangleVisible(int logicalY, int height)
    {
        if (height < 1) height = 1;
        if (logicalY < 0) logicalY = 0;
        int next = _viewport.Offset;
        if (logicalY < next) next = logicalY;
        else if ((long)logicalY + height > (long)next + _viewport.VisibleExtent)
            next = logicalY + height - _viewport.VisibleExtent;
        return _viewport.SetOffset(next);
    }

    public void BindScrollBar(GuideXosScrollBar scrollBar)
    {
        if (ReferenceEquals(_boundScrollBar, scrollBar))
        {
            _boundScrollBar?.SynchronizeViewport();
            return;
        }
        _boundScrollBar?.UnbindViewport();
        _boundScrollBar = scrollBar;
        _boundScrollBar?.BindViewport(_viewport);
    }

    public void UnbindScrollBar()
    {
        _boundScrollBar?.UnbindViewport();
        _boundScrollBar = null;
    }

    public bool HasScrollBarBinding => _boundScrollBar != null;
    public GuideXosScrollBar BoundScrollBar => _boundScrollBar;

    public GuideXosScrollViewResult HandleWheel(int x, int y, int wheelDelta)
    {
        if (!_visible || !_enabled || wheelDelta == 0 ||
            x < InnerX || x >= InnerX + InnerWidth ||
            y < InnerY || y >= InnerY + InnerHeight)
            return GuideXosScrollViewResult.Ignored;
        return _viewport.ScrollSmall(-wheelDelta * 3)
            ? GuideXosScrollViewResult.Scrolled : GuideXosScrollViewResult.Ignored;
    }

    public GuideXosScrollViewResult HandlePointerDown(int x, int y)
    {
        _lastTargetMember = null;
        _lastActivatedMember = null;
        if (!_visible) return GuideXosScrollViewResult.Ignored;
        if (!_enabled) return GuideXosScrollViewResult.Disabled;
        if (!TryHitTest(x, y, out object member))
            return GuideXosScrollViewResult.Ignored;
        _lastTargetMember = member;
        int index = FindMemberIndex(member);
        if (index < 0 || !IsMemberEnabled(index))
            return GuideXosScrollViewResult.Disabled;
        FocusMemberIndex(index);
        GuideXosScrollViewResult result = RoutePointer(index, x, y + _viewport.Offset);
        if (result == GuideXosScrollViewResult.Activated ||
            result == GuideXosScrollViewResult.Toggled ||
            result == GuideXosScrollViewResult.Selected)
            _lastActivatedMember = member;
        return result;
    }

    public GuideXosScrollViewResult HandleKey(GuideXosTextInputKey key)
    {
        if (!_visible || !_enabled) return GuideXosScrollViewResult.Ignored;
        if (key == GuideXosTextInputKey.Tab)
            return TryMoveFocus(false) ? GuideXosScrollViewResult.Focused :
                GuideXosScrollViewResult.Ignored;
        if (!HasFocus) return GuideXosScrollViewResult.Ignored;
        return RouteKey(_focusedIndex, key);
    }

    public GuideXosScrollViewResult HandleKey(
        GuideXosTextInputKey key, bool shift)
    {
        if (key == GuideXosTextInputKey.Tab)
            return TryMoveFocus(shift) ? GuideXosScrollViewResult.Focused :
                GuideXosScrollViewResult.Ignored;
        return HandleKey(key);
    }

    public GuideXosScrollViewResult HandleCharacter(char character)
    {
        if (!_visible || !_enabled || !HasFocus) return GuideXosScrollViewResult.Ignored;
        return RouteCharacter(_focusedIndex, character);
    }

    public bool TryFocusMember(object member)
    {
        int index = FindMemberIndex(member);
        return index >= 0 && FocusMemberIndex(index);
    }

    public bool TryMoveFocus(bool reverse)
    {
        if (_memberCount == 0) return false;
        int start = _focusedIndex < 0 ? (reverse ? _memberCount : -1) : _focusedIndex;
        for (int count = 0; count < _memberCount; count++)
        {
            start += reverse ? -1 : 1;
            if (start < 0 || start >= _memberCount) return false;
            if (IsFocusableEligible(start)) return FocusMemberIndex(start);
        }
        return false;
    }

    public void Focus()
    {
        if (!_visible || !_enabled) return;
        if (HasFocus) return;
        int start = _focusedIndex < 0 ? 0 : _focusedIndex;
        for (int index = start; index < _memberCount; index++)
        {
            if (IsFocusableEligible(index))
            {
                FocusMemberIndex(index);
                return;
            }
        }
    }

    public void Blur()
    {
        if (_focusedIndex >= 0 && _focusedIndex < _memberCount)
            BlurMember(_members[_focusedIndex].Kind, _members[_focusedIndex].Member);
        _focusedIndex = -1;
    }

    public void RecalculateContentExtent()
    {
        SynchronizeMemberLogicalBounds();
        RecalculateContentExtentCore(0);
    }

    /// <summary>
    /// Refreshes the existing derived extent after a non-owning C141 stack
    /// moved its members. The ScrollView stores no layout or duplicate bounds.
    /// </summary>
    public bool TryRecalculateContentExtent(GuideXosVerticalStack layout)
    {
        if (layout == null || !layout.ContainsAllMembersIn(this)) return false;
        SynchronizeMemberLogicalBounds();
        long stackBottom = (long)layout.ContentBottom - InnerY;
        if (stackBottom < 0 || stackBottom > int.MaxValue) return false;
        RecalculateContentExtentCore((int)stackBottom);
        return true;
    }

    private void RecalculateContentExtentCore(int additionalExtent)
    {
        int extent = 0;
        for (int index = 0; index < _memberCount; index++)
        {
            if (!IsMemberVisible(index)) continue;
            MemberEntry entry = _members[index];
            GetMemberSize(entry.Kind, entry.Member, out _, out int height);
            int bottom = entry.LogicalY + height;
            if (bottom > extent) extent = bottom;
        }
        if (additionalExtent > extent) extent = additionalExtent;
        _viewport.VisibleExtent = InnerHeight;
        _viewport.ContentExtent = extent;
    }

    private void SynchronizeMemberLogicalBounds()
    {
        for (int index = 0; index < _memberCount; index++)
        {
            MemberEntry entry = _members[index];
            GetMemberBounds(entry.Kind, entry.Member, out int x, out int y,
                out _, out _);
            int logicalX = x - InnerX;
            int logicalY = y - InnerY;
            if (logicalX >= 0 && logicalY >= 0 &&
                logicalX <= MaximumSupportedCoordinate &&
                logicalY <= MaximumSupportedCoordinate)
            {
                entry.LogicalX = logicalX;
                entry.LogicalY = logicalY;
                _members[index] = entry;
            }
        }
    }

    /// <summary>
    /// Draws the frame first, then renders members through the reusable surface
    /// translation/clip state. The underlying native text primitive is clipped
    /// at whole 18-pixel row granularity; rectangles are clipped exactly.
    /// </summary>
    public GuideXosResult Render(GuideXosSurface surface)
    {
        if (surface == null) return GuideXosResult.InvalidArgument;
        if (!_visible) return GuideXosResult.Success;
        if (surface.TryFillRect(_x, _y, _width, _height, BackgroundColor) !=
                GuideXosResult.Success) return GuideXosResult.InvalidArgument;
        if (surface.TryFillRect(_x, _y, _width, BorderPixels, BorderColor) != GuideXosResult.Success ||
            surface.TryFillRect(_x, _y + _height - BorderPixels, _width, BorderPixels, BorderColor) != GuideXosResult.Success ||
            surface.TryFillRect(_x, _y, BorderPixels, _height, BorderColor) != GuideXosResult.Success ||
            surface.TryFillRect(_x + _width - BorderPixels, _y, BorderPixels, _height, BorderColor) != GuideXosResult.Success)
            return GuideXosResult.InvalidArgument;

        GuideXosSurface.RenderState state = surface.SaveRenderState();
        surface.SetRenderClip(InnerX, InnerY, InnerWidth, InnerHeight);
        surface.SetRenderTranslation(0, -_viewport.Offset);
        try
        {
            for (int index = 0; index < _memberCount; index++)
            {
                if (!IsMemberVisible(index)) continue;
                if (RenderMember(_members[index].Kind, _members[index].Member, surface) !=
                        GuideXosResult.Success) return GuideXosResult.InvalidArgument;
            }
        }
        finally
        {
            surface.RestoreRenderState(state);
        }
        return GuideXosResult.Success;
    }

    public void Reset()
    {
        _visible = true;
        _enabled = true;
        _lastTargetMember = null;
        _lastActivatedMember = null;
        _rejectedInputCount = 0;
        Blur();
        _viewport.SetOffset(0);
    }

    private GuideXosScrollViewResult Reject(GuideXosScrollViewResult result)
    {
        ++_rejectedInputCount;
        return result;
    }

    private bool EnsureMemberVisible(int index)
    {
        MemberEntry entry = _members[index];
        GetMemberSize(entry.Kind, entry.Member, out _, out int height);
        return EnsureRectangleVisible(entry.LogicalY, height);
    }

    private bool FocusMemberIndex(int index)
    {
        if (!IsFocusableEligible(index)) return false;
        if (_focusedIndex != index)
        {
            Blur();
            _focusedIndex = index;
            FocusMember(_members[index].Kind, _members[index].Member);
        }
        EnsureMemberVisible(index);
        return true;
    }

    private int FindMemberIndex(object member)
    {
        for (int index = 0; index < _memberCount; index++)
            if (ReferenceEquals(_members[index].Member, member)) return index;
        return -1;
    }

    private bool IsFocusableEligible(int index)
    {
        return index >= 0 && index < _memberCount && IsMemberVisible(index) &&
            IsMemberEnabled(index) && IsFocusable(_members[index].Kind);
    }

    private bool IsMemberVisible(object member)
    {
        int index = FindMemberIndex(member);
        return index >= 0 && IsMemberVisible(index);
    }

    private bool IsMemberVisible(int index)
    {
        MemberEntry entry = _members[index];
        return entry.Kind switch
        {
            MemberKind.Button => ((GuideXosButton)entry.Member).EffectiveVisible,
            MemberKind.CheckBox => ((GuideXosCheckBox)entry.Member).EffectiveVisible,
            MemberKind.Label => ((GuideXosLabel)entry.Member).EffectiveVisible,
            MemberKind.Separator => ((GuideXosSeparator)entry.Member).EffectiveVisible,
            MemberKind.RadioButton => ((GuideXosRadioButton)entry.Member).EffectiveVisible,
            MemberKind.ProgressBar => ((GuideXosProgressBar)entry.Member).EffectiveVisible,
            MemberKind.ComboBox => ((GuideXosComboBox)entry.Member).EffectiveVisible,
            _ => false,
        };
    }

    private bool IsMemberEnabled(int index)
    {
        MemberEntry entry = _members[index];
        return entry.Kind switch
        {
            MemberKind.Button => ((GuideXosButton)entry.Member).Enabled,
            MemberKind.CheckBox => ((GuideXosCheckBox)entry.Member).Enabled,
            MemberKind.RadioButton => ((GuideXosRadioButton)entry.Member).Enabled,
            MemberKind.ComboBox => ((GuideXosComboBox)entry.Member).Enabled,
            _ => true,
        };
    }

    private static bool IsFocusable(MemberKind kind)
    {
        return kind == MemberKind.Button || kind == MemberKind.CheckBox ||
            kind == MemberKind.RadioButton || kind == MemberKind.ComboBox;
    }

    private static bool IsValidBounds(int x, int y, int width, int height)
    {
        return x >= 0 && y >= 0 && width >= MinimumSupportedWidth &&
            width <= MaximumSupportedWidth && height >= MinimumSupportedHeight &&
            height <= MaximumSupportedHeight &&
            x <= MaximumSupportedCoordinate - width &&
            y <= MaximumSupportedCoordinate - height;
    }

    private static bool Fits(MemberEntry entry, int x, int y, int width, int height)
    {
        GetMemberSize(entry.Kind, entry.Member, out int memberWidth, out int memberHeight);
        return entry.LogicalX <= width - BorderPixels * 2 - memberWidth &&
            entry.LogicalY <= MaximumSupportedCoordinate - y - BorderPixels - memberHeight &&
            entry.LogicalY <= MaximumSupportedCoordinate - memberHeight;
    }

    private static bool TryGetKind(object member, out MemberKind kind)
    {
        kind = member switch
        {
            GuideXosButton => MemberKind.Button,
            GuideXosCheckBox => MemberKind.CheckBox,
            GuideXosLabel => MemberKind.Label,
            GuideXosSeparator => MemberKind.Separator,
            GuideXosRadioButton => MemberKind.RadioButton,
            GuideXosProgressBar => MemberKind.ProgressBar,
            GuideXosComboBox => MemberKind.ComboBox,
            _ => MemberKind.None,
        };
        return kind != MemberKind.None;
    }

    private static GuideXosPanel GetParentPanel(MemberKind kind, object member)
    {
        return kind switch
        {
            MemberKind.Button => ((GuideXosButton)member).ParentPanel,
            MemberKind.CheckBox => ((GuideXosCheckBox)member).ParentPanel,
            MemberKind.Label => ((GuideXosLabel)member).ParentPanel,
            MemberKind.Separator => ((GuideXosSeparator)member).ParentPanel,
            MemberKind.RadioButton => ((GuideXosRadioButton)member).ParentPanel,
            MemberKind.ProgressBar => ((GuideXosProgressBar)member).ParentPanel,
            MemberKind.ComboBox => ((GuideXosComboBox)member).ParentPanel,
            _ => null,
        };
    }

    private static GuideXosScrollView GetParentScrollView(MemberKind kind, object member)
    {
        return kind switch
        {
            MemberKind.Button => ((GuideXosButton)member).ParentScrollView,
            MemberKind.CheckBox => ((GuideXosCheckBox)member).ParentScrollView,
            MemberKind.Label => ((GuideXosLabel)member).ParentScrollView,
            MemberKind.Separator => ((GuideXosSeparator)member).ParentScrollView,
            MemberKind.RadioButton => ((GuideXosRadioButton)member).ParentScrollView,
            MemberKind.ProgressBar => ((GuideXosProgressBar)member).ParentScrollView,
            MemberKind.ComboBox => ((GuideXosComboBox)member).ParentScrollView,
            _ => null,
        };
    }

    private static void GetMemberSize(MemberKind kind, object member,
        out int width, out int height)
    {
        switch (kind)
        {
            case MemberKind.Button:
                width = ((GuideXosButton)member).Width;
                height = ((GuideXosButton)member).Height;
                break;
            case MemberKind.CheckBox:
                width = ((GuideXosCheckBox)member).Width;
                height = ((GuideXosCheckBox)member).Height;
                break;
            case MemberKind.Label:
                width = ((GuideXosLabel)member).Width;
                height = ((GuideXosLabel)member).Height;
                break;
            case MemberKind.Separator:
                width = ((GuideXosSeparator)member).Width;
                height = ((GuideXosSeparator)member).Height;
                break;
            case MemberKind.RadioButton:
                width = ((GuideXosRadioButton)member).Width;
                height = ((GuideXosRadioButton)member).Height;
                break;
            case MemberKind.ProgressBar:
                width = ((GuideXosProgressBar)member).Width;
                height = ((GuideXosProgressBar)member).Height;
                break;
            case MemberKind.ComboBox:
                width = ((GuideXosComboBox)member).Width;
                height = ((GuideXosComboBox)member).Height;
                break;
            default:
                width = height = 0;
                break;
        }
    }

    private static void GetMemberBounds(MemberKind kind, object member,
        out int x, out int y, out int width, out int height)
    {
        switch (kind)
        {
            case MemberKind.Button:
                GuideXosButton button = (GuideXosButton)member;
                x = button.X; y = button.Y; width = button.Width; height = button.Height;
                break;
            case MemberKind.CheckBox:
                GuideXosCheckBox checkBox = (GuideXosCheckBox)member;
                x = checkBox.X; y = checkBox.Y; width = checkBox.Width; height = checkBox.Height;
                break;
            case MemberKind.Label:
                GuideXosLabel label = (GuideXosLabel)member;
                x = label.X; y = label.Y; width = label.Width; height = label.Height;
                break;
            case MemberKind.Separator:
                GuideXosSeparator separator = (GuideXosSeparator)member;
                x = separator.X; y = separator.Y; width = separator.Width; height = separator.Height;
                break;
            case MemberKind.RadioButton:
                GuideXosRadioButton radio = (GuideXosRadioButton)member;
                x = radio.X; y = radio.Y; width = radio.Width; height = radio.Height;
                break;
            case MemberKind.ProgressBar:
                GuideXosProgressBar progress = (GuideXosProgressBar)member;
                x = progress.X; y = progress.Y; width = progress.Width; height = progress.Height;
                break;
            case MemberKind.ComboBox:
                GuideXosComboBox combo = (GuideXosComboBox)member;
                x = combo.X; y = combo.Y; width = combo.Width; height = combo.Height;
                break;
            default:
                x = y = width = height = 0;
                break;
        }
    }

    private static bool SetMemberBounds(MemberKind kind, object member, int x, int y)
    {
        GetMemberSize(kind, member, out int width, out int height);
        return kind switch
        {
            MemberKind.Button => ((GuideXosButton)member).TrySetScrollViewBounds(x, y, width, height),
            MemberKind.CheckBox => ((GuideXosCheckBox)member).TrySetScrollViewBounds(x, y, width, height),
            MemberKind.Label => ((GuideXosLabel)member).TrySetScrollViewBounds(x, y, width),
            MemberKind.Separator => ((GuideXosSeparator)member).TrySetScrollViewBounds(x, y, width),
            MemberKind.RadioButton => ((GuideXosRadioButton)member).TrySetScrollViewBounds(x, y, width, height),
            MemberKind.ProgressBar => ((GuideXosProgressBar)member).TrySetScrollViewBounds(x, y, width),
            MemberKind.ComboBox => ((GuideXosComboBox)member).TrySetScrollViewBounds(x, y, width, height),
            _ => false,
        };
    }

    private bool AttachMember(MemberKind kind, object member)
    {
        return kind switch
        {
            MemberKind.Button => ((GuideXosButton)member).TryAttachToScrollView(this),
            MemberKind.CheckBox => ((GuideXosCheckBox)member).TryAttachToScrollView(this),
            MemberKind.Label => ((GuideXosLabel)member).TryAttachToScrollView(this),
            MemberKind.Separator => ((GuideXosSeparator)member).TryAttachToScrollView(this),
            MemberKind.RadioButton => ((GuideXosRadioButton)member).TryAttachToScrollView(this),
            MemberKind.ProgressBar => ((GuideXosProgressBar)member).TryAttachToScrollView(this),
            MemberKind.ComboBox => ((GuideXosComboBox)member).TryAttachToScrollView(this),
            _ => false,
        };
    }

    private void DetachMember(MemberKind kind, object member)
    {
        switch (kind)
        {
            case MemberKind.Button: ((GuideXosButton)member).DetachFromScrollView(); break;
            case MemberKind.CheckBox: ((GuideXosCheckBox)member).DetachFromScrollView(); break;
            case MemberKind.Label: ((GuideXosLabel)member).DetachFromScrollView(); break;
            case MemberKind.Separator: ((GuideXosSeparator)member).DetachFromScrollView(); break;
            case MemberKind.RadioButton: ((GuideXosRadioButton)member).DetachFromScrollView(); break;
            case MemberKind.ProgressBar: ((GuideXosProgressBar)member).DetachFromScrollView(); break;
            case MemberKind.ComboBox: ((GuideXosComboBox)member).DetachFromScrollView(); break;
        }
    }

    private static GuideXosResult RenderMember(MemberKind kind, object member,
        GuideXosSurface surface)
    {
        return kind switch
        {
            MemberKind.Button => ((GuideXosButton)member).Render(surface),
            MemberKind.CheckBox => ((GuideXosCheckBox)member).Render(surface),
            MemberKind.Label => ((GuideXosLabel)member).Render(surface),
            MemberKind.Separator => ((GuideXosSeparator)member).Render(surface),
            MemberKind.RadioButton => ((GuideXosRadioButton)member).Render(surface),
            MemberKind.ProgressBar => ((GuideXosProgressBar)member).Render(surface),
            MemberKind.ComboBox => ((GuideXosComboBox)member).Render(surface),
            _ => GuideXosResult.InvalidArgument,
        };
    }

    private static void FocusMember(MemberKind kind, object member)
    {
        switch (kind)
        {
            case MemberKind.Button: ((GuideXosButton)member).Focus(); break;
            case MemberKind.CheckBox: ((GuideXosCheckBox)member).Focus(); break;
            case MemberKind.RadioButton: ((GuideXosRadioButton)member).Focus(); break;
            case MemberKind.ComboBox: ((GuideXosComboBox)member).Focus(); break;
        }
    }

    private static void BlurMember(MemberKind kind, object member)
    {
        switch (kind)
        {
            case MemberKind.Button: ((GuideXosButton)member).Blur(); break;
            case MemberKind.CheckBox: ((GuideXosCheckBox)member).Blur(); break;
            case MemberKind.RadioButton: ((GuideXosRadioButton)member).Blur(); break;
            case MemberKind.ComboBox: ((GuideXosComboBox)member).Blur(); break;
        }
    }

    private GuideXosScrollViewResult RoutePointer(int index, int x, int y)
    {
        MemberEntry entry = _members[index];
        return entry.Kind switch
        {
            MemberKind.Button => Map(((GuideXosButton)entry.Member).HandlePointerDown(x, y)),
            MemberKind.CheckBox => Map(((GuideXosCheckBox)entry.Member).HandlePointerDown(x, y)),
            MemberKind.RadioButton => Map(((GuideXosRadioButton)entry.Member).HandlePointerDown(x, y)),
            MemberKind.ComboBox => Map(((GuideXosComboBox)entry.Member).HandlePointerDown(x, y)),
            _ => GuideXosScrollViewResult.Ignored,
        };
    }

    private GuideXosScrollViewResult RouteKey(int index, GuideXosTextInputKey key)
    {
        MemberEntry entry = _members[index];
        return entry.Kind switch
        {
            MemberKind.Button => Map(((GuideXosButton)entry.Member).HandleKey(key)),
            MemberKind.CheckBox => Map(((GuideXosCheckBox)entry.Member).HandleKey(key)),
            MemberKind.RadioButton => Map(((GuideXosRadioButton)entry.Member).HandleKey(key)),
            MemberKind.ComboBox => Map(((GuideXosComboBox)entry.Member).HandleKey(key)),
            _ => GuideXosScrollViewResult.Ignored,
        };
    }

    private GuideXosScrollViewResult RouteCharacter(int index, char character)
    {
        MemberEntry entry = _members[index];
        return entry.Kind switch
        {
            MemberKind.Button => Map(((GuideXosButton)entry.Member).HandleCharacter(character)),
            MemberKind.CheckBox => Map(((GuideXosCheckBox)entry.Member).HandleCharacter(character)),
            MemberKind.RadioButton => Map(((GuideXosRadioButton)entry.Member).HandleCharacter(character)),
            MemberKind.ComboBox => Map(((GuideXosComboBox)entry.Member).HandleCharacter(character)),
            _ => GuideXosScrollViewResult.Ignored,
        };
    }

    private static GuideXosScrollViewResult Map(GuideXosButtonResult result) =>
        result switch
        {
            GuideXosButtonResult.Activated => GuideXosScrollViewResult.Activated,
            GuideXosButtonResult.Disabled => GuideXosScrollViewResult.Disabled,
            _ => GuideXosScrollViewResult.Ignored,
        };

    private static GuideXosScrollViewResult Map(GuideXosCheckBoxResult result) =>
        result switch
        {
            GuideXosCheckBoxResult.Toggled => GuideXosScrollViewResult.Toggled,
            GuideXosCheckBoxResult.Disabled => GuideXosScrollViewResult.Disabled,
            GuideXosCheckBoxResult.Focused => GuideXosScrollViewResult.Focused,
            _ => GuideXosScrollViewResult.Ignored,
        };

    private static GuideXosScrollViewResult Map(GuideXosRadioButtonResult result) =>
        result switch
        {
            GuideXosRadioButtonResult.Selected => GuideXosScrollViewResult.Selected,
            GuideXosRadioButtonResult.Disabled => GuideXosScrollViewResult.Disabled,
            GuideXosRadioButtonResult.Focused => GuideXosScrollViewResult.Focused,
            _ => GuideXosScrollViewResult.Ignored,
        };

    private static GuideXosScrollViewResult Map(GuideXosComboBoxResult result) =>
        result switch
        {
            GuideXosComboBoxResult.Opened => GuideXosScrollViewResult.Activated,
            GuideXosComboBoxResult.SelectionChanged => GuideXosScrollViewResult.Selected,
            GuideXosComboBoxResult.Disabled => GuideXosScrollViewResult.Disabled,
            GuideXosComboBoxResult.Focused => GuideXosScrollViewResult.Focused,
            _ => GuideXosScrollViewResult.Ignored,
        };
}
