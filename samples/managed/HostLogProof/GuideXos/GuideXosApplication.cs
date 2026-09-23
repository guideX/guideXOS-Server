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
    }

    public uint Selector { get; }
    public byte[] Name { get; }
    public GuideXosApplication Application { get; }
}

/// <summary>
/// Compile-time, bounded registry. There is no reflection or assembly scan.
/// </summary>
public static unsafe class GuideXosApplicationRegistry
{
    private static readonly GuideXosApplicationDescriptor[] s_entries =
    {
        new(1u, "Managed Workspace"u8, new Applications.ManagedWorkspace()),
        new(2u, "Managed Status"u8, new Applications.ManagedStatus()),
        new(3u, "Managed Counter"u8, new Applications.ManagedCounter()),
        new(4u, "Managed Notes"u8, new Applications.ManagedNotes()),
    };

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
        GuideXosResult result = host.LaunchContext.IsInput
            ? descriptor.Application.HandleInput(host, input)
            : host.IsAction
                ? descriptor.Application.HandleAction(host, host.LaunchContext.ActionId)
                : descriptor.Application.Launch(host);
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
