using System;

namespace HostLogProof;

/// <summary>
/// Small versioned host-service facade. Applications never call a native
/// callback or retain a native context pointer directly.
/// </summary>
public sealed unsafe class GuideXosHost
{
    // NativeHostCallTable v1: two uint32 fields followed by six amd64
    // function pointers. The managed artifact is win-x64 only.
    private const nuint CapabilitiesOffset = 56u;
    private const nuint FileReadOffset = 72u;
    private const nuint FileWriteOffset = 80u;
    private readonly NativeGxAppContext* _context;
    private readonly NativeHostCallTable* _host;

    private GuideXosHost(
        NativeGxAppContext* context,
        NativeHostCallTable* host,
        GuideXosLaunchContext launchContext)
    {
        _context = context;
        _host = host;
        LaunchContext = launchContext;
    }

    public GuideXosLaunchContext LaunchContext { get; }
    public uint Selector => LaunchContext.Selector;
    public GuideXosCapability Capabilities =>
        HasHostField(CapabilitiesOffset)
            ? (GuideXosCapability)_host->capabilities
            : GuideXosCapability.None;
    public bool IsAction => LaunchContext.IsAction;
    public bool IsCapabilityProbe =>
        (LaunchContext.Flags & GxAbi.LaunchFlagCapabilityProbe) != 0;
    public bool IsAbiProbe =>
        (LaunchContext.Flags & GxAbi.LaunchFlagAbiProbe) != 0;

    public bool HasCapability(GuideXosCapability capability)
    {
        return (Capabilities & capability) == capability;
    }

    internal static bool TryCreate(
        NativeGxAppContext* context,
        uint selector,
        out GuideXosHost host,
        out GuideXosResult result)
    {
        host = null;
        result = GuideXosResult.InvalidArgument;
        if (context == null || context->size < GxAbi.LegacyContextSize ||
            context->apiVersion != GxAbi.ApiVersion || context->host == null ||
            context->host->size < GxAbi.HostCallTablePrefixSize)
        {
            return false;
        }

        if (context->host->version != GxAbi.HostAbiVersion ||
            context->host->size < GxAbi.HostCallTableV1Size)
        {
            result = GuideXosResult.AbiIncompatible;
            return false;
        }

        if (!GuideXosLaunchContext.TryCopy(context, selector, out GuideXosLaunchContext launchContext) ||
            launchContext == null)
        {
            return false;
        }

        host = new GuideXosHost(context, context->host, launchContext);
        result = GuideXosResult.Success;
        return true;
    }

    public GuideXosResult TryCreateSurface(
        ReadOnlySpan<byte> title,
        int width,
        int height,
        out GuideXosSurface surface)
    {
        surface = null;
        if (!HasCapability(GuideXosCapability.Surface) ||
            _host->requestWindow == null)
        {
            return GuideXosResult.CapabilityUnavailable;
        }
        if (title.Length == 0 || title.Length > 127 || width <= 0 || height <= 0)
        {
            return GuideXosResult.InvalidArgument;
        }

        Span<byte> buffer = stackalloc byte[title.Length + 1];
        title.CopyTo(buffer);
        buffer[title.Length] = 0;
        ulong window = 0u;
        fixed (byte* pointer = buffer)
        {
            if (_host->requestWindow(_context, pointer, width, height, &window) != 0 ||
                window == 0u)
            {
                return GuideXosResult.SurfaceCreationFailed;
            }
        }

        surface = new GuideXosSurface(this, window);
        return GuideXosResult.Success;
    }

    public GuideXosResult TryGetSurface(ulong window, out GuideXosSurface surface)
    {
        surface = null;
        if (!HasCapability(GuideXosCapability.Surface) || window == 0u)
        {
            return GuideXosResult.CapabilityUnavailable;
        }
        surface = new GuideXosSurface(this, window);
        return GuideXosResult.Success;
    }

    public GuideXosResult TryLog(ReadOnlySpan<byte> text)
    {
        if (!HasCapability(GuideXosCapability.Log) || _host->log == null)
        {
            return GuideXosResult.CapabilityUnavailable;
        }
        if (text.Length == 0 || text.Length > 127) return GuideXosResult.InvalidArgument;

        Span<byte> buffer = stackalloc byte[text.Length + 1];
        text.CopyTo(buffer);
        buffer[text.Length] = 0;
        fixed (byte* pointer = buffer)
        {
            return _host->log(_context, pointer) == 0
                ? GuideXosResult.Success
                : GuideXosResult.InvalidArgument;
        }
    }

    /// <summary>
    /// Reads one bounded application-data file.  The returned array is
    /// managed-owned; the native callback never retains the caller buffer.
    /// </summary>
    public GuideXosFileResult TryReadAllBytes(
        ReadOnlySpan<byte> path,
        out byte[] data)
    {
        data = null;
        if (!HasCapability(GuideXosCapability.FileRead) ||
            !HasHostField(FileReadOffset) || _host->fileReadAll == null)
        {
            return GuideXosFileResult.CapabilityUnavailable;
        }
        if (path.Length == 0 || path.Length > GxAbi.FilePathMaxBytes)
        {
            return GuideXosFileResult.InvalidPath;
        }

        byte[] pathBuffer = new byte[path.Length + 1];
        path.CopyTo(pathBuffer);
        uint length = 0u;
        int nativeResult;
        fixed (byte* pathPointer = pathBuffer)
        {
            nativeResult = _host->fileReadAll(_context, pathPointer,
                (uint)path.Length, null, 0u, &length);
        }
        if (nativeResult == (int)GuideXosFileResult.Success)
        {
            data = Array.Empty<byte>();
            return GuideXosFileResult.Success;
        }
        if (nativeResult != (int)GuideXosFileResult.BufferTooSmall ||
            length > GxAbi.MaxFileBytes)
        {
            return (GuideXosFileResult)nativeResult;
        }

        byte[] buffer = new byte[(int)length];
        fixed (byte* pathPointer = pathBuffer)
        fixed (byte* bufferPointer = buffer)
        {
            nativeResult = _host->fileReadAll(
                _context, pathPointer, (uint)path.Length, bufferPointer,
                (uint)buffer.Length, &length);
        }
        if (nativeResult != 0) return (GuideXosFileResult)nativeResult;
        if (length > GxAbi.MaxFileBytes) return GuideXosFileResult.IoFailure;

        data = new byte[(int)length];
        for (uint index = 0u; index < length; index++)
        {
            data[(int)index] = buffer[(int)index];
        }
        return GuideXosFileResult.Success;
    }

    /// <summary>
    /// Writes one bounded application-data file. Native code copies the data
    /// into the VFS before this call returns and retains no managed pointer.
    /// </summary>
    public GuideXosFileResult TryWriteAllBytes(
        ReadOnlySpan<byte> path,
        ReadOnlySpan<byte> data)
    {
        if (!HasCapability(GuideXosCapability.FileWrite) ||
            !HasHostField(FileWriteOffset) || _host->fileWriteAll == null)
        {
            return GuideXosFileResult.CapabilityUnavailable;
        }
        if (path.Length == 0 || path.Length > GxAbi.FilePathMaxBytes)
        {
            return GuideXosFileResult.InvalidPath;
        }
        if (data.Length > GxAbi.MaxFileBytes)
        {
            return GuideXosFileResult.FileTooLarge;
        }

        byte[] pathBuffer = new byte[path.Length + 1];
        path.CopyTo(pathBuffer);
        byte[] dataBuffer = data.ToArray();
        int nativeResult;
        fixed (byte* pathPointer = pathBuffer)
        fixed (byte* dataPointer = dataBuffer)
        {
            nativeResult = _host->fileWriteAll(
                _context, pathPointer, (uint)path.Length, dataPointer,
                (uint)data.Length);
        }
        return (GuideXosFileResult)nativeResult;
    }

    internal NativeGxAppContext* Context => _context;
    internal NativeHostCallTable* HostTable => _host;

    private bool HasHostField(nuint offset)
    {
        return _host != null && _host->size >= offset + (nuint)sizeof(ulong);
    }
}
