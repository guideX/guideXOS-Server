using System;

namespace HostLogProof.Applications;

public sealed class ManagedStatus : GuideXosApplication
{
    private static uint s_launchCount;
    private static uint s_refreshCount;
    private static ulong s_window;

    public override GuideXosResult Launch(GuideXosHost host)
    {
        uint count = ++s_launchCount;
        GuideXosResult result = host.TryCreateSurface("Managed Status"u8, 520, 300,
            out GuideXosSurface surface);
        if (result != GuideXosResult.Success || surface == null) return result;
        s_window = surface.Handle;
        if (surface.TryFillRect(10, 10, 500, 230, 0x003A5A42u) != GuideXosResult.Success ||
            !GuideXosText.Line(surface, 24, "Managed Status | "u8, "resident composite"u8) ||
            !GuideXosText.CountLine(surface, 52, "Status launches: "u8, count) ||
            !GuideXosText.ContextLine(surface, 80, host.LaunchContext) ||
            !GuideXosText.Line(surface, 108, "Allocation: "u8, "byte[] copied"u8) ||
            !GuideXosText.Line(surface, 136, "TLS: "u8, "continuing managed thread"u8) ||
            !GuideXosText.Line(surface, 164, "Distinct logical application"u8, ""u8) ||
            surface.TryAddButton(20, 200, 180, 28, "Refresh status"u8, 1u, out _)
                != GuideXosResult.Success)
        {
            return GuideXosResult.InvalidArgument;
        }
        return GuideXosText.LogApplication(host, "STATUS"u8, count, host.LaunchContext)
            ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
    }

    public override GuideXosResult HandleAction(GuideXosHost host, uint actionId)
    {
        if (actionId != 1u) return GuideXosResult.InvalidAction;
        if (host.TryGetSurface(s_window, out GuideXosSurface surface) != GuideXosResult.Success ||
            surface == null) return GuideXosResult.SurfaceCreationFailed;
        uint count = ++s_refreshCount;
        return GuideXosText.CountLine(surface, 164, "Refreshes handled: "u8, count)
            ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
    }
}
