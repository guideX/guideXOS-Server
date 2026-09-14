using System;

namespace HostLogProof.Applications;

public sealed class ManagedWorkspace : GuideXosApplication
{
    private static uint s_launchCount;
    private static uint s_actionCount;
    private static ulong s_window;

    public override GuideXosResult Launch(GuideXosHost host)
    {
        uint count = ++s_launchCount;
        GuideXosResult result = host.TryCreateSurface("Managed Workspace"u8, 520, 300,
            out GuideXosSurface surface);
        if (result != GuideXosResult.Success || surface == null) return result;
        s_window = surface.Handle;
        if (!Render(host, surface, count)) return GuideXosResult.InvalidArgument;
        return GuideXosText.LogApplication(host, "WORKSPACE"u8, count, host.LaunchContext)
            ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
    }

    public override GuideXosResult HandleAction(GuideXosHost host, uint actionId)
    {
        if (actionId != 1u) return GuideXosResult.InvalidAction;
        if (host.TryGetSurface(s_window, out GuideXosSurface surface) != GuideXosResult.Success ||
            surface == null) return GuideXosResult.SurfaceCreationFailed;
        uint count = ++s_actionCount;
        if (!GuideXosText.CountLine(surface, 164, "Actions handled: "u8, count))
        {
            return GuideXosResult.InvalidArgument;
        }
        return host.TryLog("C112-ACTION app=WORKSPACE id=1"u8) == GuideXosResult.Success
            ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
    }

    private static bool Render(GuideXosHost host, GuideXosSurface surface, uint count)
    {
        return surface.TryFillRect(10, 10, 500, 230, 0x002A4A70u) == GuideXosResult.Success &&
            GuideXosText.Line(surface, 24, "Managed Workspace | "u8, "C# NativeAOT"u8) &&
            GuideXosText.CountLine(surface, 52, "Resident launch count: "u8, count) &&
            GuideXosText.ContextLine(surface, 80, host.LaunchContext) &&
            GuideXosText.Line(surface, 108, "Managed allocation: "u8, "byte[] copied"u8) &&
            GuideXosText.Line(surface, 136, "State: "u8, "shared API ready"u8) &&
            GuideXosText.Line(surface, 164, "Use Activate to update this window"u8, ""u8) &&
            surface.TryAddButton(20, 200, 180, 28, "Activate action"u8, 1u, out _)
                == GuideXosResult.Success;
    }
}
