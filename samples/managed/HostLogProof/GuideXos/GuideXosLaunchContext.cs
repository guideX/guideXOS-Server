using System;

namespace HostLogProof;

/// <summary>
/// Managed-owned launch data. The native pointer is copied synchronously and is
/// never exposed to an application or retained after the dispatch returns.
/// </summary>
public sealed unsafe class GuideXosLaunchContext
{
    private readonly byte[] _utf8;

    internal GuideXosLaunchContext(uint selector, uint flags, byte[] utf8)
    {
        Selector = selector;
        Flags = flags;
        _utf8 = utf8;
    }

    public uint Selector { get; }
    public uint Flags { get; }
    public bool HasText => _utf8.Length != 0;
    public int Length => _utf8.Length;
    public ReadOnlySpan<byte> Utf8 => _utf8;
    public bool IsAction => (Flags & GxAbi.LaunchFlagAction) != 0;
    public uint ActionId => Flags & GxAbi.LaunchFlagPayloadMask;
    public bool IsInput => (Flags & GxAbi.LaunchFlagInput) != 0;
    public GuideXosInputKind InputKind =>
        (Flags & GxAbi.LaunchFlagInputKindMask) switch
        {
            GxAbi.LaunchFlagInputPointerDown => GuideXosInputKind.PointerDown,
            GxAbi.LaunchFlagInputKeyDown => GuideXosInputKind.KeyDown,
            GxAbi.LaunchFlagInputKeyChar => GuideXosInputKind.KeyChar,
            _ => GuideXosInputKind.None,
        };
    public uint InputPayload => Flags & GxAbi.LaunchFlagInputPayloadMask;
    public int InputX => (int)(InputPayload & GxAbi.LaunchFlagInputCoordinateMask);
    public int InputY => (int)((InputPayload >> 12) & GxAbi.LaunchFlagInputCoordinateMask);
    public uint InputKeyCode => InputPayload & GxAbi.LaunchFlagInputValueMask;
    public char InputCharacter => (char)(InputPayload & 0xFFu);
    public bool InputShift => InputKind != GuideXosInputKind.PointerDown &&
        (InputPayload & GxAbi.LaunchFlagInputShift) != 0u;

    internal static bool TryCopy(
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

        byte[] bytes = new byte[(int)context->launchContextLength];
        for (int index = 0; index < bytes.Length; index++)
        {
            byte value = context->launchContext[index];
            if (value == 0u) return false;
            bytes[index] = value;
        }

        result = new GuideXosLaunchContext(selector, context->launchFlags, bytes);
        return true;
    }
}
