using System;

namespace HostLogProof;

/// <summary>Opaque managed handle for the current compositor-backed surface.</summary>
public sealed unsafe class GuideXosSurface
{
    private readonly GuideXosHost _host;

    internal GuideXosSurface(GuideXosHost host, ulong handle)
    {
        _host = host;
        Handle = handle;
    }

    public ulong Handle { get; }

    public GuideXosResult TrySetText(int x, int y, ReadOnlySpan<byte> text)
    {
        NativeHostCallTable* table = _host.HostTable;
        if (!_host.HasCapability(GuideXosCapability.Text) || table->drawText == null)
        {
            return GuideXosResult.CapabilityUnavailable;
        }
        if (x < 0 || y < 0 || text.Length > 63) return GuideXosResult.InvalidArgument;

        Span<byte> buffer = stackalloc byte[text.Length + 1];
        text.CopyTo(buffer);
        buffer[text.Length] = 0;
        fixed (byte* pointer = buffer)
        {
            return table->drawText(_host.Context, Handle, x, y, pointer) == 0
                ? GuideXosResult.Success
                : GuideXosResult.InvalidArgument;
        }
    }

    public GuideXosResult TryFillRect(
        int x,
        int y,
        int width,
        int height,
        uint color)
    {
        NativeHostCallTable* table = _host.HostTable;
        if (!_host.HasCapability(GuideXosCapability.Primitive) || table->drawRect == null)
        {
            return GuideXosResult.CapabilityUnavailable;
        }
        if (x < 0 || y < 0 || width <= 0 || height <= 0)
        {
            return GuideXosResult.InvalidArgument;
        }

        return table->drawRect(_host.Context, Handle, x, y, width, height, color) == 0
            ? GuideXosResult.Success
            : GuideXosResult.InvalidArgument;
    }

    public GuideXosResult TryAddButton(
        int x,
        int y,
        int width,
        int height,
        ReadOnlySpan<byte> label,
        uint actionId,
        out int widget)
    {
        widget = -1;
        NativeHostCallTable* table = _host.HostTable;
        if (!_host.HasCapability(GuideXosCapability.Action) ||
            table->addActionButton == null)
        {
            return GuideXosResult.CapabilityUnavailable;
        }
        if (actionId == 0u || actionId > GxAbi.LaunchFlagPayloadMask ||
            x < 0 || y < 0 || width <= 0 || height <= 0 || label.Length > 63)
        {
            return GuideXosResult.InvalidArgument;
        }

        Span<byte> buffer = stackalloc byte[label.Length + 1];
        label.CopyTo(buffer);
        buffer[label.Length] = 0;
        int widgetValue = -1;
        fixed (byte* pointer = buffer)
        {
            if (table->addActionButton(
                    _host.Context, Handle, x, y, width, height, pointer,
                    actionId, &widgetValue) != 0 || widgetValue < 0)
            {
                return GuideXosResult.InvalidAction;
            }
        }
        widget = widgetValue;
        return GuideXosResult.Success;
    }

    public GuideXosResult TryClose()
    {
        NativeHostCallTable* table = _host.HostTable;
        if (!_host.HasCapability(GuideXosCapability.Close) || table->closeWindow == null)
        {
            return GuideXosResult.CapabilityUnavailable;
        }
        return table->closeWindow(_host.Context, Handle) == 0
            ? GuideXosResult.Success
            : GuideXosResult.InvalidArgument;
    }
}
