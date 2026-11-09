using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace HostLogProof;

[StructLayout(LayoutKind.Sequential, Pack = 4, Size = 16)]
public struct ManagedSettingsObjectProbeRecord
{
    public volatile uint EntryMarker;
    public volatile uint Input;
    public volatile uint Computed;
    public volatile uint CompletionMarker;
}

public sealed class DisplaySettingsProbe
{
    private const int ConstructedMarker = unchecked((int)0x52313243); // R12C

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
    private const uint EntryMarker = 0x52313245; // R12E
    private const uint CompletionMarker = 0x52313244; // R12D

    public static void Main()
    {
    }

    [UnmanagedCallersOnly(
        EntryPoint = "ManagedMain",
        CallConvs = new[] { typeof(CallConvCdecl) })]
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static int ManagedMain(ManagedSettingsObjectProbeRecord* record, uint seed)
    {
        record->EntryMarker = EntryMarker;
        record->Input = seed;

        DisplaySettingsProbe settings = new(640, 480, 2);
        int result = settings.Calculate();

        record->Computed = unchecked((uint)result);
        record->CompletionMarker = CompletionMarker;
        GC.KeepAlive(settings);
        return result;
    }
}
