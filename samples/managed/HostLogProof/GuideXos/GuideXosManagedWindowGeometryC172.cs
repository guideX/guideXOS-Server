namespace HostLogProof;

/// <summary>
/// Packs a synchronous native window geometry notification into the existing
/// application action channel. The C172 range is reserved for geometry events.
/// </summary>
internal static class GuideXosManagedWindowGeometryC172
{
    // Keep this action range out of bit 28, which marks the existing input
    // channel in GuideXosLaunchContext.Flags.
    internal const uint ActionTag = 0x08000000u;
    internal const uint ActionTagMask = 0x1F000000u;
    internal const int ComponentMask = 0x0FFF;

    internal static bool TryEncodeAction(
        int outerWidth, int outerHeight, out uint actionId)
    {
        actionId = 0u;
        if (outerWidth <= 0 || outerHeight <= 0 ||
            outerWidth > ComponentMask || outerHeight > ComponentMask)
            return false;

        actionId = ActionTag | ((uint)outerWidth << 12) | (uint)outerHeight;
        return true;
    }

    internal static bool TryDecodeAction(
        uint actionId, out int outerWidth, out int outerHeight)
    {
        outerWidth = 0;
        outerHeight = 0;
        if ((actionId & ActionTagMask) != ActionTag) return false;

        outerWidth = (int)((actionId >> 12) & ComponentMask);
        outerHeight = (int)(actionId & ComponentMask);
        return outerWidth > 0 && outerHeight > 0;
    }
}
