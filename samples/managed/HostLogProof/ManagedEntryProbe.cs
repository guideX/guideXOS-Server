using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace HostLogProof;

[StructLayout(LayoutKind.Sequential, Pack = 4, Size = 16)]
public struct ManagedEntryProbeRecord
{
    public volatile uint EntryMarker;
    public volatile uint Input;
    public volatile uint Computed;
    public volatile uint CompletionMarker;
}

public static unsafe class ManagedEntryProbe
{
    private const uint EntryMarker = 0x5237454E; // R7EN
    private const uint CompletionMarker = 0x5237444E; // R7DN

    public static void Main()
    {
    }

    [UnmanagedCallersOnly(
        EntryPoint = "ManagedMain",
        CallConvs = new[] { typeof(CallConvCdecl) })]
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static int ManagedMain(ManagedEntryProbeRecord* record, uint seed)
    {
        record->EntryMarker = EntryMarker;
        record->Input = seed;
        uint result = unchecked(seed * 3u + 7u);
        record->Computed = result;
        record->CompletionMarker = CompletionMarker;
        return unchecked((int)result);
    }
}
