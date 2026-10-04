using System;

namespace HostLogProof;

#if HOSTLOGPROOF_MANAGED_APP_RETURN
/// <summary>
/// The managed App Model keeps three bounded logical application objects so
/// Notes, Managed Calculator, and Managed Task Manager can coexist.
/// </summary>
internal sealed class GuideXosManagedApplicationLifetime
{
    private const int Capacity = 3;
    private readonly Entry[] _entries = new Entry[Capacity];
    private uint _generation;

    private struct Entry
    {
        public uint Selector;
        public GuideXosApplication Application;
    }

    public bool TryStart(
        GuideXosApplicationDescriptor descriptor,
        out GuideXosApplication application,
        out uint generation)
    {
        int entryIndex = FindEntry(descriptor.Selector);
        if (entryIndex < 0) entryIndex = FindFreeEntry();
        if (entryIndex < 0)
        {
            application = null;
            generation = _generation;
            return false;
        }

        _entries[entryIndex].Application?.OnTearingDown();
        _entries[entryIndex] = default;
        application = descriptor.Factory?.Invoke();
        if (application == null)
        {
            generation = _generation;
            return false;
        }
        _entries[entryIndex].Selector = descriptor.Selector;
        _entries[entryIndex].Application = application;
        generation = ++_generation;
        return true;
    }

    public bool TryGet(uint selector, out GuideXosApplication application)
    {
        int entryIndex = FindEntry(selector);
        application = entryIndex >= 0 ? _entries[entryIndex].Application : null;
        return application != null;
    }

    public void Clear(uint selector)
    {
        int entryIndex = FindEntry(selector);
        if (entryIndex < 0) return;
        _entries[entryIndex].Application?.OnTearingDown();
        _entries[entryIndex] = default;
    }

    private int FindEntry(uint selector)
    {
        for (int index = 0; index < _entries.Length; index++)
            if (_entries[index].Selector == selector &&
                _entries[index].Application != null) return index;
        return -1;
    }

    private int FindFreeEntry()
    {
        for (int index = 0; index < _entries.Length; index++)
            if (_entries[index].Application == null) return index;
        return -1;
    }
}
#endif

#if HOSTLOGPROOF_C150_MANAGED_APP_RETURN
internal static class GuideXosManagedApplicationC150Tests
{
    private sealed class TestApplication : GuideXosApplication
    {
        public override GuideXosResult Launch(GuideXosHost host) =>
            GuideXosResult.Success;
    }

    public static bool Run()
    {
        GuideXosManagedApplicationLifetime lifetime = new();
        GuideXosApplicationDescriptor notes = new(
            4u, "Managed Notes"u8, static () => new TestApplication());
        GuideXosApplicationDescriptor settings = new(
            5u, "Managed Settings Center"u8,
            static () => new TestApplication());

        bool empty = !lifetime.TryGet(4u, out _);
        bool firstStart = lifetime.TryStart(notes, out GuideXosApplication first,
            out uint firstGeneration) && firstGeneration == 1u;
        bool currentAvailable = lifetime.TryGet(4u, out GuideXosApplication current) &&
            ReferenceEquals(first, current);
        bool initiallyDistinct = !lifetime.TryGet(5u, out _);
        bool secondStart = lifetime.TryStart(settings,
            out GuideXosApplication second, out uint secondGeneration) &&
            secondGeneration == 2u && !ReferenceEquals(first, second);
        bool bothAvailable = lifetime.TryGet(4u, out current) &&
            ReferenceEquals(first, current) && lifetime.TryGet(5u, out current) &&
            ReferenceEquals(second, current);
        GuideXosApplicationDescriptor calculator = new(
            6u, "Managed Calculator"u8, static () => new TestApplication());
        bool calculatorStart = lifetime.TryStart(calculator,
            out GuideXosApplication third, out uint thirdGeneration) &&
            thirdGeneration == 3u && !ReferenceEquals(first, third) &&
            !ReferenceEquals(second, third);
        bool threeAvailable = lifetime.TryGet(4u, out current) &&
            ReferenceEquals(first, current) && lifetime.TryGet(5u, out current) &&
            ReferenceEquals(second, current) && lifetime.TryGet(6u, out current) &&
            ReferenceEquals(third, current);
        GuideXosApplicationDescriptor taskManager = new(
            7u, "Managed Task Manager"u8,
            static () => new TestApplication());
        bool capacityRejected = !lifetime.TryStart(taskManager, out _, out _);
        // Release the intermediate Settings surface, retaining Notes and
        // Managed Calculator while Task Manager takes the newly free slot.
        lifetime.Clear(5u);
        bool coexistence = lifetime.TryGet(4u, out current) &&
            ReferenceEquals(first, current) && lifetime.TryGet(6u, out current) &&
            ReferenceEquals(third, current) && !lifetime.TryGet(5u, out _);
        bool recycledSlotFresh = lifetime.TryStart(taskManager,
            out GuideXosApplication fourth, out uint fourthGeneration) &&
            fourthGeneration == 4u && lifetime.TryGet(7u, out current) &&
            ReferenceEquals(fourth, current) && lifetime.TryGet(4u, out current) &&
            ReferenceEquals(first, current) && lifetime.TryGet(6u, out current) &&
            ReferenceEquals(third, current);
        lifetime.Clear(7u);
        lifetime.Clear(7u);
        lifetime.Clear(4u);
        lifetime.Clear(6u);
        bool cleared = !lifetime.TryGet(5u, out _) && !lifetime.TryGet(6u, out _) &&
            !lifetime.TryGet(7u, out _) && !lifetime.TryGet(4u, out _);

        return empty && firstStart && currentAvailable && initiallyDistinct &&
            secondStart && bothAvailable && calculatorStart && threeAvailable &&
            capacityRejected && coexistence && recycledSlotFresh && cleared;
    }
}
#endif
