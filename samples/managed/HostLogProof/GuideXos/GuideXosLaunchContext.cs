using System;

namespace HostLogProof;

/// <summary>
/// Managed-owned launch data. The native pointer is copied synchronously and is
/// never exposed to an application or retained after the dispatch returns.
/// </summary>
public sealed unsafe class GuideXosLaunchContext
{
    private readonly byte[] _utf8 =
        new byte[(int)GxAbi.MaxLaunchContextBytes];
    private int _length;
    private uint _selector;
    private uint _flags;

    internal GuideXosLaunchContext()
    {
    }

    public uint Selector => _selector;
    public uint Flags => _flags;
    public bool HasText => _length != 0;
    public int Length => _length;
    public ReadOnlySpan<byte> Utf8 => _utf8.AsSpan(0, _length);
    public bool IsAction => (Flags & GxAbi.LaunchFlagAction) != 0;
    public uint ActionId => Flags & GxAbi.LaunchFlagPayloadMask;
    public bool IsInput => (Flags & GxAbi.LaunchFlagInput) != 0;
    public GuideXosInputKind InputKind
    {
        get
        {
            uint kind = Flags & GxAbi.LaunchFlagInputKindMask;
            if (kind >= GxAbi.LaunchFlagInputWheel &&
                kind <= GxAbi.LaunchFlagInputWheelLast)
            {
                return GuideXosInputKind.Wheel;
            }
            return kind switch
            {
                GxAbi.LaunchFlagInputPointerDown => GuideXosInputKind.PointerDown,
                GxAbi.LaunchFlagInputPointerUp => GuideXosInputKind.PointerUp,
                GxAbi.LaunchFlagInputSecondaryPointerDown => GuideXosInputKind.PointerDown,
                GxAbi.LaunchFlagInputSecondaryPointerUp => GuideXosInputKind.PointerUp,
                GxAbi.LaunchFlagInputKeyDown => GuideXosInputKind.KeyDown,
                GxAbi.LaunchFlagInputKeyChar => GuideXosInputKind.KeyChar,
                _ => GuideXosInputKind.None,
            };
        }
    }
    public uint InputPayload => Flags & GxAbi.LaunchFlagInputPayloadMask;
    public int InputX => (int)(InputPayload & GxAbi.LaunchFlagInputCoordinateMask);
    public int InputY => (int)((InputPayload >> 12) & GxAbi.LaunchFlagInputCoordinateMask);
    public uint InputKeyCode => InputPayload & GxAbi.LaunchFlagInputValueMask;
    public char InputCharacter => (char)(InputPayload & 0xFFu);
    public int InputWheelDelta
    {
        get
        {
            uint kind = Flags & GxAbi.LaunchFlagInputKindMask;
            return kind >= GxAbi.LaunchFlagInputWheel &&
                kind <= GxAbi.LaunchFlagInputWheelLast
                ? (int)((kind - GxAbi.LaunchFlagInputWheel) >> 24) - 4
                : 0;
        }
    }
    public GuideXosPointerButton InputButton =>
        (Flags & GxAbi.LaunchFlagInputKindMask) switch
        {
            GxAbi.LaunchFlagInputPointerDown => GuideXosPointerButton.Primary,
            GxAbi.LaunchFlagInputPointerUp => GuideXosPointerButton.Primary,
            GxAbi.LaunchFlagInputSecondaryPointerDown => GuideXosPointerButton.Secondary,
            GxAbi.LaunchFlagInputSecondaryPointerUp => GuideXosPointerButton.Secondary,
            _ => GuideXosPointerButton.None,
        };
    public bool InputShift => InputKind != GuideXosInputKind.PointerDown &&
        InputKind != GuideXosInputKind.PointerUp &&
        InputKind != GuideXosInputKind.Wheel &&
        (InputPayload & GxAbi.LaunchFlagInputShift) != 0u;

    internal bool TryCopy(
        NativeGxAppContext* context,
        uint selector,
        out GuideXosLaunchContext result)
    {
        result = null;
        if (context == null || context->size < GxAbi.LegacyContextSize ||
            context->launchContextLength > GxAbi.MaxLaunchContextBytes ||
            (context->launchContextLength != 0u && context->launchContext == null))
        {
            return false;
        }

        _selector = selector;
        _flags = context->launchFlags;
        _length = (int)context->launchContextLength;
        for (int index = 0; index < context->launchContextLength; index++)
        {
            byte value = context->launchContext[index];
            if (value == 0u) return false;
            _utf8[index] = value;
        }

        result = this;
        return true;
    }
}
