using System;

namespace HostLogProof;

#if HOSTLOGPROOF_MANAGED_APP_RETURN || HOSTLOGPROOF_C185_BOUNDARY
/// <summary>
/// The managed App Model keeps five bounded logical application objects so
/// all established managed desktop applications can coexist.
/// </summary>
internal sealed class GuideXosManagedApplicationLifetime
{
    internal const int Capacity = 5;
    private readonly Entry[] _entries = new Entry[Capacity];
    private uint _generation;

    private struct Entry
    {
        public uint Selector;
        public uint Generation;
        public GuideXosApplication Application;
    }

    public bool TryStart(
        GuideXosApplicationDescriptor descriptor,
        out GuideXosApplication application,
        out uint generation)
    {
        return TryStart(descriptor, 0u, out application, out generation);
    }

    public bool TryStart(
        GuideXosApplicationDescriptor descriptor,
        uint requestedGeneration,
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

        if (requestedGeneration == 0u && _generation == uint.MaxValue)
        {
            application = null;
            generation = _generation;
            return false;
        }

        application = descriptor.Factory?.Invoke();
        if (application == null)
        {
            generation = requestedGeneration == 0u
                ? _generation : requestedGeneration;
            return false;
        }

        uint assignedGeneration = requestedGeneration == 0u
            ? _generation + 1u : requestedGeneration;
        _entries[entryIndex].Application?.OnTearingDown();
        _entries[entryIndex] = default;
        _entries[entryIndex].Selector = descriptor.Selector;
        _entries[entryIndex].Generation = assignedGeneration;
        _entries[entryIndex].Application = application;
        if (assignedGeneration > _generation) _generation = assignedGeneration;
        generation = assignedGeneration;
        return true;
    }

    public bool TryGet(uint selector, out GuideXosApplication application)
    {
        int entryIndex = FindEntry(selector);
        application = entryIndex >= 0 ? _entries[entryIndex].Application : null;
        return application != null;
    }

    public bool TryGet(uint selector, uint generation,
        out GuideXosApplication application)
    {
        int entryIndex = FindEntry(selector);
        bool matches = entryIndex >= 0 && generation != 0u &&
            _entries[entryIndex].Generation == generation;
        application = matches ? _entries[entryIndex].Application : null;
        return application != null;
    }

    public bool Clear(uint selector, uint generation)
    {
        int entryIndex = FindEntry(selector);
        if (entryIndex < 0 || generation == 0u ||
            _entries[entryIndex].Generation != generation) return false;
        _entries[entryIndex].Application?.OnTearingDown();
        _entries[entryIndex] = default;
        return true;
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
        GuideXosApplicationDescriptor[] applications =
        {
            new(4u, "Managed Notes"u8, static () => new TestApplication()),
            new(5u, "Managed Settings Center"u8,
                static () => new TestApplication()),
            new(6u, "Managed Calculator"u8,
                static () => new TestApplication()),
            new(7u, "Managed Task Manager"u8,
                static () => new TestApplication()),
            new(8u, "Managed File Explorer"u8,
                static () => new TestApplication()),
        };
        GuideXosApplication[] residents = new GuideXosApplication[
            GuideXosManagedApplicationLifetime.Capacity];
        bool empty = !lifetime.TryGet(4u, out _);
        bool allStarted = true;
        bool generationsMonotonic = true;
        for (int index = 0; index < applications.Length; index++)
        {
            allStarted &= lifetime.TryStart(applications[index],
                out residents[index], out uint generation);
            generationsMonotonic &= generation == (uint)(index + 1);
        }

        bool distinctAndAvailable = true;
        for (int index = 0; index < applications.Length; index++)
        {
            distinctAndAvailable &= residents[index] != null &&
                lifetime.TryGet(applications[index].Selector,
                    out GuideXosApplication current) &&
                ReferenceEquals(residents[index], current);
            for (int prior = 0; prior < index; prior++)
                distinctAndAvailable &= !ReferenceEquals(
                    residents[index], residents[prior]);
        }

        int extraFactoryCalls = 0;
        GuideXosApplicationDescriptor extra = new(
            9u, "Host-only capacity probe"u8,
            () => { extraFactoryCalls++; return new TestApplication(); });
        bool capacityRejected = !lifetime.TryStart(extra, out _, out _);
        bool fullPoolPreserved = extraFactoryCalls == 0;
        for (int index = 0; index < applications.Length; index++)
        {
            fullPoolPreserved &= lifetime.TryGet(applications[index].Selector,
                out GuideXosApplication current) &&
                ReferenceEquals(residents[index], current);
        }

        lifetime.Clear(5u);
        lifetime.Clear(5u);
        bool clearIsolated = !lifetime.TryGet(5u, out _) &&
            lifetime.TryGet(4u, out GuideXosApplication notes) &&
            ReferenceEquals(residents[0], notes) &&
            lifetime.TryGet(6u, out GuideXosApplication calculator) &&
            ReferenceEquals(residents[2], calculator);
        bool recycledSlotFresh = lifetime.TryStart(extra,
            out GuideXosApplication recycled, out uint recycledGeneration) &&
            recycledGeneration == 6u && !ReferenceEquals(residents[1], recycled) &&
            lifetime.TryGet(9u, out GuideXosApplication currentRecycled) &&
            ReferenceEquals(recycled, currentRecycled);

        lifetime.Clear(9u);
        bool allCleared = true;
        foreach (GuideXosApplicationDescriptor application in applications)
        {
            lifetime.Clear(application.Selector);
            allCleared &= !lifetime.TryGet(application.Selector, out _);
        }

        return GuideXosManagedApplicationLifetime.Capacity == 5 && empty &&
            allStarted && generationsMonotonic &&
            distinctAndAvailable && capacityRejected && fullPoolPreserved &&
            clearIsolated && recycledSlotFresh && allCleared;
    }
}
#endif
