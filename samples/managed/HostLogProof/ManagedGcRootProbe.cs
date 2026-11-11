using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace HostLogProof;

[StructLayout(LayoutKind.Sequential, Pack = 4, Size = 44)]
public struct ManagedGcRootProbeRecord
{
    public volatile uint EntryMarker;
    public volatile uint Input;
    public volatile uint InitialResult;
    public volatile uint RequestMarker;
    public volatile uint CounterStatusBefore;
    public volatile uint CollectionCountBefore;
    public volatile uint CollectionCountAfter;
    public volatile uint CounterStatusAfter;
    public volatile uint CompletionMarker;
    public volatile uint FinalResult;
    public volatile uint ReturnMarker;
}

public sealed class DisplaySettingsProbe
{
    private const int ConstructedMarker = unchecked((int)0x5231334F);

    public int Width;
    public int Height;
    public int Scale;
    private int _constructedMarker;

    [MethodImpl(MethodImplOptions.NoInlining)]
    public DisplaySettingsProbe(int width, int height, int scale)
    {
        Width = width;
        Height = height;
        Scale = scale;
        _constructedMarker = ConstructedMarker;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public int Calculate()
    {
        return _constructedMarker == ConstructedMarker
            ? Width + Height + Scale
            : -1;
    }
}

public static unsafe class ManagedSettingsObjectProbe
{
    private const uint EntryMarker = 0x52313345;
    private const uint RequestMarker = 0x52313351;
    private const uint CompletionMarker = 0x52313343;
    private const uint ReturnMarker = 0x52313352;

    [DllImport("__Internal", EntryPoint = "guideXosNativeAotC188R12R4R5ReadCounters",
        CallingConvention = CallingConvention.Cdecl)]
    private static extern int ReadNativeCounters(
        out uint collectionCount, out nuint heapAllocatedBytes,
        out nuint threadAllocatedBytes);

    public static void Main()
    {
    }

    [UnmanagedCallersOnly(
        EntryPoint = "ManagedMain",
        CallConvs = new[] { typeof(CallConvCdecl) })]
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static int ManagedMain(ManagedGcRootProbeRecord* record, uint seed)
    {
        record->EntryMarker = EntryMarker;
        record->Input = seed;

        DisplaySettingsProbe settings = new(640, 480, 2);
        int initial = settings.Calculate();
        record->InitialResult = unchecked((uint)initial);
        if (initial != 1122)
            return -1;

        int beforeStatus = ReadNativeCounters(
            out uint countBefore, out _, out _);
        record->CounterStatusBefore = unchecked((uint)beforeStatus);
        record->CollectionCountBefore = countBefore;
        if (beforeStatus != 0)
            return -2;

        record->RequestMarker = RequestMarker;
        GC.Collect();

        int afterStatus = ReadNativeCounters(
            out uint countAfter, out _, out _);
        record->CounterStatusAfter = unchecked((uint)afterStatus);
        record->CollectionCountAfter = countAfter;
        if (afterStatus == 0 && countAfter > countBefore)
            record->CompletionMarker = CompletionMarker;

        settings.Scale = 3;
        int result = settings.Calculate();
        record->FinalResult = unchecked((uint)result);
        record->ReturnMarker = ReturnMarker;
        GC.KeepAlive(settings);
        return result;
    }
}
