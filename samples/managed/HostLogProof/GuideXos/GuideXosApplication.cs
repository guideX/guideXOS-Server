using System;

namespace HostLogProof;

public abstract unsafe class GuideXosApplication
{
    public abstract GuideXosResult Launch(GuideXosHost host);
    public virtual GuideXosResult HandleAction(GuideXosHost host, uint actionId)
    {
        return GuideXosResult.InvalidAction;
    }
    public virtual GuideXosResult HandleInput(
        GuideXosHost host, GuideXosInputEvent input)
    {
        return GuideXosResult.Success;
    }
}

public readonly struct GuideXosApplicationDescriptor
{
    public GuideXosApplicationDescriptor(
        uint selector,
        ReadOnlySpan<byte> name,
        GuideXosApplication application)
    {
        Selector = selector;
        Name = name.ToArray();
        Application = application;
#if HOSTLOGPROOF_MANAGED_APP_RETURN
        Factory = null;
#endif
    }

#if HOSTLOGPROOF_MANAGED_APP_RETURN
    public GuideXosApplicationDescriptor(
        uint selector,
        ReadOnlySpan<byte> name,
        Func<GuideXosApplication> factory)
    {
        Selector = selector;
        Name = name.ToArray();
        Application = null;
        Factory = factory;
    }
#endif

    public uint Selector { get; }
    public byte[] Name { get; }
    public GuideXosApplication Application { get; }
#if HOSTLOGPROOF_MANAGED_APP_RETURN
    public Func<GuideXosApplication> Factory { get; }
#endif
}

/// <summary>
/// Compile-time, bounded registry. There is no reflection or assembly scan.
/// </summary>
public static unsafe class GuideXosApplicationRegistry
{
#if HOSTLOGPROOF_MANAGED_APP_RETURN
    private static readonly GuideXosApplicationDescriptor[] s_entries =
    {
        new(4u, "Managed Notes"u8, static () => new Applications.ManagedNotes()),
        new(5u, "Managed Settings Center"u8,
            static () => new Applications.ManagedSettingsCenter()),
    };
    private static readonly GuideXosManagedApplicationLifetime s_lifetime = new();
#if HOSTLOGPROOF_C150_MANAGED_APP_RETURN
    private static bool s_lifecycleTestsRun;
#endif
#elif HOSTLOGPROOF_C147_RUNTIME_SETTINGS
    private static readonly GuideXosApplicationDescriptor[] s_entries =
    {
        new(4u, "Managed Notes"u8, new Applications.ManagedNotes()),
        new(5u, "Managed Settings Center"u8, new Applications.ManagedSettingsCenter()),
    };
#elif HOSTLOGPROOF_C146_PERSISTENT_SETTINGS
    private static readonly GuideXosApplicationDescriptor[] s_entries =
    {
        new(5u, "Managed Settings Center"u8, new Applications.ManagedSettingsCenter()),
    };
#else
    private static readonly GuideXosApplicationDescriptor[] s_entries =
    {
        new(1u, "Managed Workspace"u8, new Applications.ManagedWorkspace()),
        new(2u, "Managed Status"u8, new Applications.ManagedStatus()),
        new(3u, "Managed Counter"u8, new Applications.ManagedCounter()),
        new(4u, "Managed Notes"u8, new Applications.ManagedNotes()),
#if HOSTLOGPROOF_C145_MANAGED_MODAL_DIALOG
        new(5u, "Managed Settings Center"u8, new Applications.ManagedSettingsCenter()),
#elif HOSTLOGPROOF_C144_MANAGED_SETTINGS_CENTER
        new(5u, "Managed Settings Center"u8, new Applications.ManagedSettingsCenter()),
#elif HOSTLOGPROOF_C143_MANAGED_GROUP_BOX
        new(5u, "Managed GroupBox"u8, new Applications.ManagedGroupBoxDemo()),
#elif HOSTLOGPROOF_C142_MANAGED_VERTICAL_STACK
        new(5u, "Managed Vertical Stack"u8, new Applications.ManagedVerticalStackDemo()),
#elif HOSTLOGPROOF_C141_MANAGED_VERTICAL_STACK
        new(5u, "Managed Vertical Stack"u8, new Applications.ManagedVerticalStackDemo()),
#elif HOSTLOGPROOF_C140_MANAGED_SCROLL_VIEW
        new(5u, "Managed ScrollView"u8, new Applications.ManagedScrollViewDemo()),
#endif
    };
#endif

    public static bool TryFind(
        uint selector,
        out GuideXosApplicationDescriptor descriptor)
    {
        for (int index = 0; index < s_entries.Length; index++)
        {
            if (s_entries[index].Selector == selector)
            {
                descriptor = s_entries[index];
                return true;
            }
        }

        descriptor = default;
        return false;
    }

    public static int Dispatch(NativeGxAppContext* context)
    {
        if (context == null || context->size < GxAbi.LegacyContextSize)
        {
            return GxAbi.ErrorInvalidArgument;
        }

        uint selector = unchecked((uint)(nuint)context->userData);
        if (!GuideXosHost.TryCreate(context, selector, out GuideXosHost host,
                out GuideXosResult hostResult) || host == null)
        {
            return hostResult == GuideXosResult.AbiIncompatible
                ? GxAbi.ErrorUnsupported
                : (int)hostResult;
        }

        if (!TryFind(selector, out GuideXosApplicationDescriptor descriptor))
        {
            host.TryLog("C112-UNKNOWN-SELECTOR"u8);
            return GxAbi.ErrorInvalidApplicationId;
        }

#if HOSTLOGPROOF_C155_MANAGED_NOTES_SESSION
        // C150 may need to discard a pending semantic session after Notes was
        // destroyed. This host-owned one-shot operation intentionally runs
        // before the resident application lifetime lookup.
        if (selector == 4u && host.IsAction &&
            host.LaunchContext.ActionId ==
                GuideXosNotesReturnSessionC155.NativeClearActionId)
        {
            GuideXosNotesReturnSessionC155.Shared.Clear();
            host.TryLog("C155-SESSION-CLEAR pending=none result=PASS"u8);
            return (int)GuideXosResult.Success;
        }
        if (selector == 4u && host.IsAction &&
            host.LaunchContext.ActionId ==
                GuideXosNotesReturnSessionC155.NativeCheckActionId)
        {
            return GuideXosNotesReturnSessionC155.Shared.IsPending
                ? (int)GuideXosResult.Success : (int)GuideXosResult.InvalidAction;
        }
#endif

#if HOSTLOGPROOF_C147_RUNTIME_SETTINGS
        // The shared loader runs before the first managed application launch
        // or input callback; Settings Center is only one consumer of state.
        if (!host.IsCapabilityProbe &&
            !Applications.GuideXosRuntimeSettings.EnsureInitialized(host))
        {
            return GxAbi.ErrorInvalidArgument;
        }
#endif

        GuideXosInputEvent input = default;
        if (host.LaunchContext.IsInput)
        {
            input = GuideXosInputEvent.From(host.LaunchContext);
            if (input.Button == GuideXosPointerButton.Secondary &&
                (input.Kind == GuideXosInputKind.PointerDown ||
                    input.Kind == GuideXosInputKind.PointerUp))
            {
                host.TryLog("C136-BRIDGE secondary=recognized result=PASS"u8);
            }
        }
#if HOSTLOGPROOF_C150_MANAGED_APP_RETURN
        if (!s_lifecycleTestsRun)
        {
            s_lifecycleTestsRun = true;
            bool lifecycleTests = GuideXosManagedApplicationC150Tests.Run();
            host.TryLog(lifecycleTests
                ? "C150-MANAGED-LIFECYCLE-TESTS cases=8 fresh=PASS active=one result=PASS"u8
                : "C150-MANAGED-LIFECYCLE-TESTS result=FAIL"u8);
            if (!lifecycleTests) return GxAbi.ErrorInvalidArgument;
        }
#endif

#if HOSTLOGPROOF_MANAGED_APP_RETURN
        GuideXosApplication application;
        bool isNewLaunch = !host.LaunchContext.IsInput && !host.IsAction;
        if (isNewLaunch)
        {
            if (!s_lifetime.TryStart(descriptor, out application,
                    out uint generation))
                return GxAbi.ErrorInvalidApplicationId;
#if HOSTLOGPROOF_C150_MANAGED_APP_RETURN
            Span<byte> instanceMarker = stackalloc byte[96];
            int markerLength = 0;
            GuideXosText.Append(instanceMarker, ref markerLength,
                "C150-APP-INSTANCE id="u8);
            GuideXosText.AppendUnsigned(instanceMarker, ref markerLength, selector);
            GuideXosText.Append(instanceMarker, ref markerLength,
                " generation="u8);
            GuideXosText.AppendUnsigned(instanceMarker, ref markerLength, generation);
            host.TryLog(instanceMarker[..markerLength]);
#endif
        }
        else if (s_lifetime.TryGet(selector, out application))
        {
        }
        else
        {
#if HOSTLOGPROOF_C150_MANAGED_APP_RETURN
            host.TryLog("C150-DISPATCH rejected=stale-application result=FAIL"u8);
#endif
            return GxAbi.ErrorInvalidApplicationId;
        }

        GuideXosResult result = host.LaunchContext.IsInput
            ? application.HandleInput(host, input)
            : host.IsAction
                ? application.HandleAction(host, host.LaunchContext.ActionId)
                : application.Launch(host);
        if (isNewLaunch && result != GuideXosResult.Success)
        {
            s_lifetime.Clear(selector);
        }
#else
        GuideXosResult result = host.LaunchContext.IsInput
            ? descriptor.Application.HandleInput(host, input)
            : host.IsAction
                ? descriptor.Application.HandleAction(host, host.LaunchContext.ActionId)
                : descriptor.Application.Launch(host);
#endif
        return (int)result;
    }
}

internal static class GuideXosText
{
    public static bool Line(
        GuideXosSurface surface,
        int y,
        ReadOnlySpan<byte> prefix,
        ReadOnlySpan<byte> suffix)
    {
        Span<byte> line = stackalloc byte[64];
        int position = 0;
        if (!Append(line, ref position, prefix) || !Append(line, ref position, suffix))
        {
            return false;
        }
        return surface.TrySetText(20, y, line[..position]) == GuideXosResult.Success;
    }

    public static bool CountLine(
        GuideXosSurface surface,
        int y,
        ReadOnlySpan<byte> prefix,
        uint value)
    {
        Span<byte> line = stackalloc byte[64];
        int position = 0;
        if (!Append(line, ref position, prefix) || !AppendUnsigned(line, ref position, value))
        {
            return false;
        }
        return surface.TrySetText(20, y, line[..position]) == GuideXosResult.Success;
    }

    public static bool ContextLine(
        GuideXosSurface surface,
        int y,
        GuideXosLaunchContext context)
    {
        Span<byte> line = stackalloc byte[64];
        int position = 0;
        if (!Append(line, ref position, "Context: "u8)) return false;
        if (context.Length == 0)
        {
            if (!Append(line, ref position, "<empty>"u8)) return false;
        }
        else if (!Append(line, ref position, context.Utf8))
        {
            return false;
        }
        return surface.TrySetText(20, y, line[..position]) == GuideXosResult.Success;
    }

    public static bool Append(Span<byte> buffer, ref int position, ReadOnlySpan<byte> value)
    {
        if (value.Length > buffer.Length - position) return false;
        value.CopyTo(buffer[position..]);
        position += value.Length;
        return true;
    }

    public static bool AppendUnsigned(Span<byte> buffer, ref int position, uint value)
    {
        Span<byte> digits = stackalloc byte[10];
        int count = 0;
        do
        {
            digits[count++] = (byte)('0' + value % 10u);
            value /= 10u;
        } while (value != 0u);

        if (count > buffer.Length - position) return false;
        while (count != 0) buffer[position++] = digits[--count];
        return true;
    }

    public static bool LogApplication(
        GuideXosHost host,
        ReadOnlySpan<byte> name,
        uint count,
        GuideXosLaunchContext context)
    {
        Span<byte> line = stackalloc byte[128];
        int position = 0;
        if (!Append(line, ref position, "C112-"u8) ||
            !Append(line, ref position, name) ||
            !Append(line, ref position, " count="u8) ||
            !AppendUnsigned(line, ref position, count) ||
            !Append(line, ref position, " context="u8)) return false;
        if (context.Length == 0) Append(line, ref position, "<empty>"u8);
        else Append(line, ref position, context.Utf8);
        return host.TryLog(line[..position]) == GuideXosResult.Success;
    }
}
