using System;

namespace HostLogProof.Applications;

public sealed class ManagedCounter : GuideXosApplication
{
    private static uint s_launchCount;
    private static uint s_value;
    private static ulong s_window;

    public override GuideXosResult Launch(GuideXosHost host)
    {
        GuideXosResult result = host.TryCreateSurface("Managed Counter"u8, 520, 300,
            out GuideXosSurface surface);
        if (result != GuideXosResult.Success || surface == null) return result;
        s_window = surface.Handle;
        uint launchCount = s_launchCount + 1u;

        if (surface.TryFillRect(10, 10, 500, 230, 0x006A4A2Au) != GuideXosResult.Success ||
            !GuideXosText.Line(surface, 24, "Managed Counter | "u8, "shared application API"u8) ||
            !GuideXosText.CountLine(surface, 52, "Counter launches: "u8, launchCount) ||
            !GuideXosText.CountLine(surface, 80, "Value: "u8, s_value) ||
            !GuideXosText.ContextLine(surface, 108, host.LaunchContext))
        {
            return GuideXosResult.InvalidArgument;
        }

        GuideXosResult actionResult = surface.TryAddButton(
            20, 200, 180, 28, "Increment"u8, 1u, out _);
        if (host.IsCapabilityProbe)
        {
            bool safeDowngrade = actionResult == GuideXosResult.CapabilityUnavailable;
            host.TryLog(safeDowngrade
                ? "C112-CAPABILITY-DOWNGRADE result=PASS"u8
                : "C112-CAPABILITY-DOWNGRADE result=FAIL"u8);
            return safeDowngrade ? GuideXosResult.Success : GuideXosResult.InvalidAction;
        }
        if (actionResult != GuideXosResult.Success) return actionResult;
        s_launchCount = launchCount;

        return GuideXosText.LogApplication(host, "COUNTER"u8, s_launchCount, host.LaunchContext)
            ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
    }

    public override GuideXosResult HandleAction(GuideXosHost host, uint actionId)
    {
        if (actionId != 1u) return GuideXosResult.InvalidAction;
        if (host.TryGetSurface(s_window, out GuideXosSurface surface) != GuideXosResult.Success ||
            surface == null) return GuideXosResult.SurfaceCreationFailed;
        ++s_value;
        if (!GuideXosText.CountLine(surface, 80, "Value: "u8, s_value))
        {
            return GuideXosResult.InvalidArgument;
        }
        return host.TryLog("C112-ACTION app=COUNTER id=1"u8) == GuideXosResult.Success
            ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
    }
}
