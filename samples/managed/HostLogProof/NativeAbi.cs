using System.Runtime.InteropServices;

namespace HostLogProof;

public static class GxAbi
{
    public const uint ApiVersion = 0u;
    public const int Ok = 0;
    public const int ErrorInvalidArgument = -2;
    public const int ErrorUnsupported = -3;
    public const int ErrorFailed = -4;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeGxEvent
{
    public uint size;
    public uint type;
    public ulong window;
    public int param1;
    public int param2;
    public int param3;
    public int param4;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeHostCallTable
{
    public uint size;
    public uint version;
    public delegate* unmanaged<NativeGxAppContext*, byte*, int> log;
    public delegate* unmanaged<NativeGxAppContext*, uint> get_api_version;
    public delegate* unmanaged<NativeGxAppContext*, byte*, int, int, ulong*, int> request_window;
    public delegate* unmanaged<NativeGxAppContext*, ulong, int, int, int, int, byte*, int> draw_text;
    public delegate* unmanaged<NativeGxAppContext*, ulong, int, int, int, int, uint, int> draw_rect;
    public delegate* unmanaged<NativeGxAppContext*, ulong, int, int> wait_for_close;
    public delegate* unmanaged<NativeGxAppContext*, NativeGxEvent*, int, int> poll_event;
    public delegate* unmanaged<NativeGxAppContext*, int, int> exit;
    public delegate* unmanaged<NativeGxAppContext*, byte*, void*, uint, uint*, int> file_read_all;
    public delegate* unmanaged<NativeGxAppContext*, byte*, uint*, int> file_exists;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeGxAppContext
{
    public uint size;
    public uint apiVersion;
    public NativeHostCallTable* host;
    public void* userData;
}
