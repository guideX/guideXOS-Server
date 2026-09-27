using System;

namespace HostLogProof;

public enum GuideXosGroupBoxResult
{
    Added = 1,
    Removed = 2,
    Cleared = 3,
    Duplicate = 4,
    CapacityReached = 5,
    InvalidMember = 6,
    MembershipConflict = 7,
    NotMember = 8,
}

/// <summary>
/// A bounded text-backed structural boundary. The box owns only its geometry,
/// optional caption, and visibility; it does not own, focus, or route any
/// child control.
/// </summary>
public sealed class GuideXosGroupBox
{
    public const int CharacterWidth = 8;
    public const int TextRowHeight = 18;
    public const int MinimumSupportedCoordinate = 0;
    public const int MaximumSupportedCoordinate = 4095;
    public const int MinimumSupportedRenderWidth = 8;
    public const int MaximumSupportedRenderWidth = 63;
    public const int MinimumSupportedWidth =
        MinimumSupportedRenderWidth * CharacterWidth;
    public const int MaximumSupportedWidth =
        MaximumSupportedRenderWidth * CharacterWidth;
    public const int MinimumSupportedHeight = TextRowHeight * 3;
    public const int MaximumSupportedHeight = TextRowHeight * 16;
    public const int DefaultMaximumCaptionLength = 48;
    public const int MaximumSupportedCaptionLength = 48;
    public const int DefaultMaximumMemberCount = 8;
    public const int MaximumSupportedMemberCount = 8;
    public const int DefaultContentPadding = 8;
    public const int MaximumSupportedContentPadding = 64;
    public const int FrameBorderWidth = CharacterWidth;
    public const int TitleBandHeight = TextRowHeight;
    private const int CaptionLeadingColumns = 3;
    private const int CaptionTrailingColumns = 2;

    private readonly char[] _captionStorage;
    private readonly object[] _members = new object[MaximumSupportedMemberCount];
    private readonly int _capacity = DefaultMaximumMemberCount;
    private int _captionLength;
    private int _memberCount;
    private int _contentPadding = DefaultContentPadding;
    private int _x;
    private int _y;
    private int _width;
    private int _height;
    private bool _visible = true;
    private bool _enabled = true;
    private GuideXosScrollView _scrollViewOwner;
    private uint _rejectedInputCount;

    public GuideXosGroupBox(
        int x,
        int y,
        int width,
        int height,
        string caption = "")
    {
        _captionStorage = new char[DefaultMaximumCaptionLength];
        _x = MinimumSupportedCoordinate;
        _y = MinimumSupportedCoordinate;
        _width = MinimumSupportedWidth;
        _height = MinimumSupportedHeight;
        TrySetBounds(x, y, width, height);
        TrySetCaption(caption ?? string.Empty);
    }

    public int X => _x;
    public int Y => _y;
    public int Width => _width;
    public int Height => _height;
    public int RenderWidth => _width / CharacterWidth;
    public int RenderRowCount => _height / TextRowHeight;
    public int MaximumCaptionLength => _captionStorage.Length;
    public int CaptionLength => _captionLength;
    public int RenderCaptionLength => Math.Min(
        _captionLength, GetCaptionRenderCapacity());
    public string Caption => new string(_captionStorage, 0, _captionLength);
    public string Text => Caption;
    public bool Visible => _visible;
    public bool Enabled => _enabled;
    public bool Focusable => false;
    public int MemberCapacity => _capacity;
    public int MemberCount => _memberCount;
    public int ContentPadding => _contentPadding;
    public int ContentLeft => _x + FrameBorderWidth + _contentPadding;
    public int ContentTop => _y + TitleBandHeight + _contentPadding;
    public int ContentWidth => Math.Max(0,
        _width - 2 * (FrameBorderWidth + _contentPadding));
    public int ContentHeight => Math.Max(0,
        _height - TitleBandHeight - TextRowHeight - 2 * _contentPadding);
    public GuideXosScrollView ParentScrollView => _scrollViewOwner;
    public uint RejectedInputCount => _rejectedInputCount;

    /// <summary>
    /// Replaces the bounded printable-ASCII caption atomically. Empty is valid;
    /// null, control characters, and over-capacity text are rejected.
    /// </summary>
    public bool TrySetCaption(string caption)
    {
        return caption != null && TrySetCaption(caption.AsSpan());
    }

    /// <summary>Bounded span overload for callers that already own the value.</summary>
    public bool TrySetCaption(ReadOnlySpan<char> caption)
    {
        if (!IsValidCaption(caption))
        {
            ++_rejectedInputCount;
            return false;
        }

        for (int index = 0; index < caption.Length; index++)
        {
            _captionStorage[index] = caption[index];
        }
        _captionLength = caption.Length;
        return true;
    }

    public bool ClearCaption()
    {
        return TrySetCaption(ReadOnlySpan<char>.Empty);
    }

    public bool TrySetText(string text) => TrySetCaption(text);

    public bool TrySetText(ReadOnlySpan<char> text) => TrySetCaption(text);

    public bool TrySetContentPadding(int padding)
    {
        if (padding < 0 || padding > MaximumSupportedContentPadding)
        {
            ++_rejectedInputCount;
            return false;
        }
        _contentPadding = padding;
        return true;
    }

    /// <summary>Returns the current content rectangle without caching member geometry.</summary>
    public bool TryGetContentRectangle(
        out int x, out int y, out int width, out int height)
    {
        x = ContentLeft;
        y = ContentTop;
        width = ContentWidth;
        height = ContentHeight;
        return width > 0 && height > 0;
    }

    /// <summary>
    /// Associates one supported ordinary leaf control. Membership does not set
    /// bounds, register the control, or change its lifetime.
    /// </summary>
    public GuideXosGroupBoxResult TryAddMember(object member)
    {
        if (!IsSupportedMember(member))
            return RejectMember(GuideXosGroupBoxResult.InvalidMember);
        if (FindMemberIndex(member) >= 0)
            return RejectMember(GuideXosGroupBoxResult.Duplicate);
        if (GetMemberGroupBox(member) != null || GetMemberPanel(member) != null)
            return RejectMember(GuideXosGroupBoxResult.MembershipConflict);
        if (_memberCount >= _capacity)
            return RejectMember(GuideXosGroupBoxResult.CapacityReached);
        if (!AttachMember(member))
            return RejectMember(GuideXosGroupBoxResult.MembershipConflict);
        _members[_memberCount++] = member;
        return GuideXosGroupBoxResult.Added;
    }

    /// <summary>Removes only this association; the member remains registered and alive.</summary>
    public GuideXosGroupBoxResult TryRemoveMember(object member)
    {
        int index = FindMemberIndex(member);
        if (index < 0) return RejectMember(GuideXosGroupBoxResult.NotMember);
        DetachMember(member);
        for (int move = index + 1; move < _memberCount; move++)
            _members[move - 1] = _members[move];
        _members[--_memberCount] = null;
        return GuideXosGroupBoxResult.Removed;
    }

    public GuideXosGroupBoxResult ClearMembers()
    {
        if (_memberCount == 0) return GuideXosGroupBoxResult.Cleared;
        for (int index = 0; index < _memberCount; index++)
        {
            DetachMember(_members[index]);
            _members[index] = null;
        }
        _memberCount = 0;
        return GuideXosGroupBoxResult.Cleared;
    }

    public object GetMember(int index) =>
        index >= 0 && index < _memberCount ? _members[index] : null;

    /// <summary>Checks live member bounds, so later Stack layout is never stale.</summary>
    public bool ValidateMembershipGeometry(out int outsideMemberCount)
    {
        outsideMemberCount = 0;
        int right = ContentLeft + ContentWidth;
        int bottom = ContentTop + ContentHeight;
        for (int index = 0; index < _memberCount; index++)
        {
            GetMemberBounds(_members[index], out int x, out int y,
                out int width, out int height);
            if (x < ContentLeft || y < ContentTop ||
                x + width > right || y + height > bottom)
                ++outsideMemberCount;
        }
        return outsideMemberCount == 0;
    }

    public void SetEnabled(bool enabled) => _enabled = enabled;

    public bool TrySetBounds(int x, int y, int width, int height)
    {
        if (_scrollViewOwner != null)
        {
            ++_rejectedInputCount;
            return false;
        }
        return TrySetBoundsCore(x, y, width, height);
    }

    private bool TrySetBoundsCore(int x, int y, int width, int height)
    {
        if (x < MinimumSupportedCoordinate ||
            y < MinimumSupportedCoordinate ||
            width < MinimumSupportedWidth ||
            width > MaximumSupportedWidth ||
            width % CharacterWidth != 0 ||
            height < MinimumSupportedHeight ||
            height > MaximumSupportedHeight ||
            height % TextRowHeight != 0 ||
            x > MaximumSupportedCoordinate - width ||
            y > MaximumSupportedCoordinate - height)
        {
            ++_rejectedInputCount;
            return false;
        }

        _x = x;
        _y = y;
        _width = width;
        _height = height;
        return true;
    }

    public void SetVisible(bool visible)
    {
        _visible = visible;
    }

    /// <summary>
    /// Uses a half-open rectangle: X &lt;= px &lt; X+Width and
    /// Y &lt;= py &lt; Y+Height. It is geometry only and does not route input.
    /// </summary>
    public bool ContainsPoint(int x, int y)
    {
        return x >= _x && x < _x + _width &&
            y >= _y && y < _y + _height;
    }

    /// <summary>
    /// Resolves a pixel coordinate relative to this box without changing state.
    /// Relative coordinates use the same half-open bounds as ContainsPoint.
    /// </summary>
    public bool TryResolvePoint(
        int relativeX,
        int relativeY,
        out int absoluteX,
        out int absoluteY)
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

    /// <summary>
    /// Restores visibility and diagnostics while retaining geometry and caption.
    /// </summary>
    public void Reset()
    {
        _visible = true;
        _enabled = true;
        _rejectedInputCount = 0u;
    }

    internal bool TrySetScrollViewBounds(int x, int y, int width, int height)
    {
        return TrySetBoundsCore(x, y, width, height);
    }

    internal bool TryAttachToScrollView(GuideXosScrollView scrollView)
    {
        if (scrollView == null || _scrollViewOwner != null) return false;
        _scrollViewOwner = scrollView;
        return true;
    }

    internal void DetachFromScrollView() => _scrollViewOwner = null;

    /// <summary>
    /// Renders a complete bounded text frame. The first and last text rows are
    /// horizontal borders; interior rows contain vertical sides and spaces.
    /// Caption clipping is render-only and never changes stored caption text.
    /// </summary>
    public GuideXosResult Render(GuideXosSurface surface)
    {
        if (surface == null) return GuideXosResult.InvalidArgument;
        if (!_visible) return GuideXosResult.Success;

        Span<byte> line = stackalloc byte[MaximumSupportedRenderWidth + 1];
        int rowCount = RenderRowCount;
        for (int row = 0; row < rowCount; row++)
        {
            int renderLength = RenderWidth;
            if (row == 0)
            {
                for (int index = 0; index < renderLength; index++)
                {
                    line[index] = (byte)'-';
                }
                line[0] = (byte)'+';
                line[renderLength - 1] = (byte)'+';
                RenderCaption(line, renderLength);
            }
            else if (row == rowCount - 1)
            {
                for (int index = 0; index < renderLength; index++)
                {
                    line[index] = (byte)'-';
                }
                line[0] = (byte)'+';
                line[renderLength - 1] = (byte)'+';
            }
            else
            {
                for (int index = 0; index < renderLength; index++)
                {
                    line[index] = (byte)' ';
                }
                line[0] = (byte)'|';
                line[renderLength - 1] = (byte)'|';
            }

            GuideXosResult result = surface.TrySetText(
                _x, _y + row * TextRowHeight, line[..renderLength]);
            if (result != GuideXosResult.Success) return result;
        }
        return GuideXosResult.Success;
    }

    private int GetCaptionRenderCapacity()
    {
        return Math.Max(0, RenderWidth -
            CaptionLeadingColumns - CaptionTrailingColumns - 1);
    }

    private void RenderCaption(Span<byte> line, int renderLength)
    {
        if (_captionLength == 0) return;

        int renderCaptionLength = RenderCaptionLength;
        int captionStart = CaptionLeadingColumns;
        for (int index = 0; index < renderCaptionLength; index++)
        {
            line[captionStart + index] = (byte)_captionStorage[index];
        }
        int captionEnd = captionStart + renderCaptionLength;
        if (captionEnd < renderLength - 1)
        {
            line[captionEnd] = (byte)' ';
        }
    }

    private bool IsValidCaption(ReadOnlySpan<char> caption)
    {
        if (caption.Length > _captionStorage.Length) return false;
        for (int index = 0; index < caption.Length; index++)
        {
            if (caption[index] < 0x20 || caption[index] > 0x7E) return false;
        }
        return true;
    }

    private GuideXosGroupBoxResult RejectMember(GuideXosGroupBoxResult result)
    {
        ++_rejectedInputCount;
        return result;
    }

    private int FindMemberIndex(object member)
    {
        for (int index = 0; index < _memberCount; index++)
            if (ReferenceEquals(_members[index], member)) return index;
        return -1;
    }

    private static bool IsSupportedMember(object member) => member is
        GuideXosButton or GuideXosCheckBox or GuideXosLabel or
        GuideXosSeparator or GuideXosRadioButton or GuideXosProgressBar or
        GuideXosComboBox;

    private static GuideXosGroupBox GetMemberGroupBox(object member) => member switch
    {
        GuideXosButton value => value.ParentGroupBox,
        GuideXosCheckBox value => value.ParentGroupBox,
        GuideXosLabel value => value.ParentGroupBox,
        GuideXosSeparator value => value.ParentGroupBox,
        GuideXosRadioButton value => value.ParentGroupBox,
        GuideXosProgressBar value => value.ParentGroupBox,
        GuideXosComboBox value => value.ParentGroupBox,
        _ => null,
    };

    private static GuideXosPanel GetMemberPanel(object member) => member switch
    {
        GuideXosButton value => value.ParentPanel,
        GuideXosCheckBox value => value.ParentPanel,
        GuideXosLabel value => value.ParentPanel,
        GuideXosSeparator value => value.ParentPanel,
        GuideXosRadioButton value => value.ParentPanel,
        GuideXosProgressBar value => value.ParentPanel,
        GuideXosComboBox value => value.ParentPanel,
        _ => null,
    };

    private bool AttachMember(object member) => member switch
    {
        GuideXosButton value => value.TryAttachToGroupBox(this),
        GuideXosCheckBox value => value.TryAttachToGroupBox(this),
        GuideXosLabel value => value.TryAttachToGroupBox(this),
        GuideXosSeparator value => value.TryAttachToGroupBox(this),
        GuideXosRadioButton value => value.TryAttachToGroupBox(this),
        GuideXosProgressBar value => value.TryAttachToGroupBox(this),
        GuideXosComboBox value => value.TryAttachToGroupBox(this),
        _ => false,
    };

    private static void DetachMember(object member)
    {
        switch (member)
        {
            case GuideXosButton value: value.DetachFromGroupBox(); break;
            case GuideXosCheckBox value: value.DetachFromGroupBox(); break;
            case GuideXosLabel value: value.DetachFromGroupBox(); break;
            case GuideXosSeparator value: value.DetachFromGroupBox(); break;
            case GuideXosRadioButton value: value.DetachFromGroupBox(); break;
            case GuideXosProgressBar value: value.DetachFromGroupBox(); break;
            case GuideXosComboBox value: value.DetachFromGroupBox(); break;
        }
    }

    private static void GetMemberBounds(object member,
        out int x, out int y, out int width, out int height)
    {
        switch (member)
        {
            case GuideXosButton value: x = value.X; y = value.Y; width = value.Width; height = value.Height; break;
            case GuideXosCheckBox value: x = value.X; y = value.Y; width = value.Width; height = value.Height; break;
            case GuideXosLabel value: x = value.X; y = value.Y; width = value.Width; height = GuideXosLabel.TextRowHeight; break;
            case GuideXosSeparator value: x = value.X; y = value.Y; width = value.Width; height = GuideXosSeparator.TextRowHeight; break;
            case GuideXosRadioButton value: x = value.X; y = value.Y; width = value.Width; height = value.Height; break;
            case GuideXosProgressBar value: x = value.X; y = value.Y; width = value.Width; height = GuideXosProgressBar.TextRowHeight; break;
            case GuideXosComboBox value: x = value.X; y = value.Y; width = value.Width; height = value.Height; break;
            default: x = y = width = height = 0; break;
        }
    }
}
