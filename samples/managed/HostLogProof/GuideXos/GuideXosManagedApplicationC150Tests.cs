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
#if HOSTLOGPROOF_C188R12R3_LIFETIME
    private uint _c188r12r3TracedStarts;
    internal uint C188R12R3Generation => _generation;
    internal uint C188R12R3ResidentMask
    {
        get
        {
            uint mask = 0u;
            for (int index = 0; index < _entries.Length; index++)
            {
                if (_entries[index].Application != null)
                    mask |= 1u << index;
            }
            return mask;
        }
    }
#endif
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
    private uint _c188r11InitializationEpoch;
    private uint _c188r11ClearEvents;
    private uint _c188r11ReplacementEvents;
    private uint _c188r11TeardownEvents;
    private uint _c188r11WriteEvents;
    private uint _c188r11LastWriteSite;

    public GuideXosManagedApplicationLifetime()
    {
        // This constructor is the actual allocation/initialization boundary
        // for one C181 pool owner.
        _c188r11InitializationEpoch++;
    }

    internal object C188R11StorageObject => _entries;
    // This is a scalar snapshot of a field location for a QAC-owned debugger
    // watchpoint. It is neither retained nor dereferenced by managed code;
    // callers correlate it with the GC count and re-read it at each boundary.
    internal unsafe nuint C188R11EntriesFieldAddress
    {
        get
        {
            ref Entry[] entriesReference = ref
                System.Runtime.CompilerServices.Unsafe.AsRef(in _entries);
            return (nuint)System.Runtime.CompilerServices.Unsafe.AsPointer(
                ref entriesReference);
        }
    }
    internal uint C188R11InitializationEpoch => _c188r11InitializationEpoch;
    internal uint C188R11ClearEvents => _c188r11ClearEvents;
    internal uint C188R11ReplacementEvents => _c188r11ReplacementEvents;
    internal uint C188R11TeardownEvents => _c188r11TeardownEvents;
    internal uint C188R11WriteEvents => _c188r11WriteEvents;
    internal uint C188R11LastWriteSite => _c188r11LastWriteSite;
    internal uint C188R11Generation => _generation;
    internal uint C188R11NotesOwnerGeneration
    {
        get
        {
            for (int index = 0; index < _entries.Length; index++)
            {
                if (_entries[index].Selector == 4u &&
                    _entries[index].Application != null)
                {
                    return _entries[index].Generation;
                }
            }
            return 0u;
        }
    }
    internal uint C188R11ResidentMask
    {
        get
        {
            uint mask = 0u;
            for (int index = 0; index < _entries.Length; index++)
            {
                if (_entries[index].Application != null)
                {
                    mask |= 1u << index;
                }
            }
            return mask;
        }
    }
#endif

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
        return TryStartCore(descriptor, requestedGeneration, out application,
            out generation, null, 0u, default);
    }

#if HOSTLOGPROOF_C188R12R3_LIFETIME
    internal bool TryStartDiagnostic(
        GuideXosApplicationDescriptor descriptor,
        uint requestedGeneration,
        out GuideXosApplication application,
        out uint generation,
        GuideXosHost diagnosticHost,
        uint invocationId,
        ReadOnlySpan<byte> applicationIdentity)
    {
        // Keep this proof-only trace bounded per lifetime manager. The
        // production path uses TryStart and never emits these records.
        if (_c188r12r3TracedStarts >= 8u)
        {
            return TryStartCore(descriptor, requestedGeneration,
                out application, out generation, null, 0u, default);
        }
        _c188r12r3TracedStarts++;
        if (invocationId == 0u)
        {
            invocationId = requestedGeneration != 0u
                ? requestedGeneration
                : _generation == uint.MaxValue ? uint.MaxValue : _generation + 1u;
        }
        if (applicationIdentity.IsEmpty)
            applicationIdentity = descriptor.Name;
        return TryStartCore(descriptor, requestedGeneration, out application,
            out generation, diagnosticHost, invocationId, applicationIdentity);
    }
#endif

    private bool TryStartCore(
        GuideXosApplicationDescriptor descriptor,
        uint requestedGeneration,
        out GuideXosApplication application,
        out uint generation,
        GuideXosHost diagnosticHost,
        uint invocationId,
        ReadOnlySpan<byte> applicationIdentity)
    {
#if HOSTLOGPROOF_C188R12R3_LIFETIME
        EmitC188R12R3(diagnosticHost, invocationId, "enter"u8,
            descriptor.Selector, applicationIdentity, -1, "preflight"u8,
            requestedGeneration);
#endif
        int entryIndex = FindEntry(descriptor.Selector);
#if HOSTLOGPROOF_C188R12R3_LIFETIME
        EmitC188R12R3(diagnosticHost, invocationId, "existing-resident"u8,
            descriptor.Selector, applicationIdentity, entryIndex,
            entryIndex >= 0 ? "found"u8 : "miss"u8, requestedGeneration);
#endif
        if (entryIndex < 0) entryIndex = FindFreeEntry();
#if HOSTLOGPROOF_C188R12R3_LIFETIME
        EmitC188R12R3(diagnosticHost, invocationId, "capacity-check"u8,
            descriptor.Selector, applicationIdentity,
            entryIndex >= 0 ? 0 : 1,
            entryIndex >= 0 ? "available"u8 : "full"u8,
            requestedGeneration);
#endif
        if (entryIndex < 0)
        {
            application = null;
            generation = _generation;
#if HOSTLOGPROOF_C188R12R3_LIFETIME
            EmitC188R12R3(diagnosticHost, invocationId, "exit-failure"u8,
                descriptor.Selector, applicationIdentity, 1, "capacity"u8,
                generation);
#endif
            return false;
        }

        if (requestedGeneration == 0u && _generation == uint.MaxValue)
        {
            application = null;
            generation = _generation;
#if HOSTLOGPROOF_C188R12R3_LIFETIME
            EmitC188R12R3(diagnosticHost, invocationId, "generation-check"u8,
                descriptor.Selector, applicationIdentity, 2, "overflow"u8,
                generation);
            EmitC188R12R3(diagnosticHost, invocationId, "exit-failure"u8,
                descriptor.Selector, applicationIdentity, 2, "generation"u8,
                generation);
#endif
            return false;
        }

#if HOSTLOGPROOF_C188R12R3_LIFETIME
        EmitC188R12R3(diagnosticHost, invocationId, "generation-check"u8,
            descriptor.Selector, applicationIdentity, 0, "valid"u8,
            requestedGeneration);
        Func<GuideXosApplication> factory = descriptor.Factory;
        EmitC188R12R3(diagnosticHost, invocationId, "factory-resolve"u8,
            descriptor.Selector, applicationIdentity,
            factory == null ? 3 : 0,
            factory == null ? "missing"u8 : "resolved"u8,
            requestedGeneration);
        if (factory == null)
        {
            application = null;
            generation = requestedGeneration == 0u
                ? _generation : requestedGeneration;
            EmitC188R12R3(diagnosticHost, invocationId, "exit-failure"u8,
                descriptor.Selector, applicationIdentity, 3, "factory-missing"u8,
                generation);
            return false;
        }

        EmitC188R12R3(diagnosticHost, invocationId, "factory-enter"u8,
            descriptor.Selector, applicationIdentity, -1, "invoking"u8,
            requestedGeneration);
        try
        {
            application = factory();
        }
        catch (Exception)
        {
            EmitC188R12R3(diagnosticHost, invocationId, "factory-throw"u8,
                descriptor.Selector, applicationIdentity, -1, "exception"u8,
                requestedGeneration);
            EmitC188R12R3(diagnosticHost, invocationId, "exit-failure"u8,
                descriptor.Selector, applicationIdentity, -1, "factory-exception"u8,
                requestedGeneration);
            throw;
        }
        EmitC188R12R3(diagnosticHost, invocationId, "factory-return"u8,
            descriptor.Selector, applicationIdentity,
            application == null ? 4 : 0,
            application == null ? "null"u8 : "created"u8,
            requestedGeneration);
#else
        application = descriptor.Factory?.Invoke();
#endif
        if (application == null)
        {
            generation = requestedGeneration == 0u
                ? _generation : requestedGeneration;
#if HOSTLOGPROOF_C188R12R3_LIFETIME
            EmitC188R12R3(diagnosticHost, invocationId, "exit-failure"u8,
                descriptor.Selector, applicationIdentity, 4, "factory-null"u8,
                generation);
#endif
            return false;
        }

        uint assignedGeneration = requestedGeneration == 0u
            ? _generation + 1u : requestedGeneration;
        GuideXosApplication previousApplication =
            _entries[entryIndex].Application;
#if HOSTLOGPROOF_C188R12R3_LIFETIME
        EmitC188R12R3(diagnosticHost, invocationId,
            "resident-register-enter"u8, descriptor.Selector,
            applicationIdentity, -1,
            previousApplication == null ? "free-slot"u8 : "replacement"u8,
            assignedGeneration);
#endif
        if (previousApplication != null)
        {
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
            _c188r11ReplacementEvents++;
            _c188r11TeardownEvents++;
#endif
#if HOSTLOGPROOF_C188R12R3_LIFETIME
            EmitC188R12R3(diagnosticHost, invocationId,
                "teardown-enter"u8, descriptor.Selector,
                applicationIdentity, -1, "callback"u8,
                _entries[entryIndex].Generation);
            try
            {
                previousApplication.OnTearingDown();
            }
            catch (Exception)
            {
                EmitC188R12R3(diagnosticHost, invocationId,
                    "teardown-throw"u8, descriptor.Selector,
                    applicationIdentity, -1, "exception"u8,
                    _entries[entryIndex].Generation);
                EmitC188R12R3(diagnosticHost, invocationId,
                    "exit-failure"u8, descriptor.Selector,
                    applicationIdentity, -1, "teardown-exception"u8,
                    _entries[entryIndex].Generation);
                throw;
            }
            EmitC188R12R3(diagnosticHost, invocationId,
                "teardown-return"u8, descriptor.Selector,
                applicationIdentity, 0, "complete"u8,
                _entries[entryIndex].Generation);
#else
            previousApplication.OnTearingDown();
#endif
        }
        _entries[entryIndex] = default;
        _entries[entryIndex].Selector = descriptor.Selector;
        _entries[entryIndex].Generation = assignedGeneration;
        _entries[entryIndex].Application = application;
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
        RecordC188R11Write(1u); // Completed TryStart entry assignment.
#endif
#if HOSTLOGPROOF_C188R12R3_LIFETIME
        EmitC188R12R3(diagnosticHost, invocationId,
            "resident-register-return"u8, descriptor.Selector,
            applicationIdentity, 0, "registered"u8, assignedGeneration);
#endif
        if (assignedGeneration > _generation) _generation = assignedGeneration;
        generation = assignedGeneration;
#if HOSTLOGPROOF_C188R12R3_LIFETIME
        EmitC188R12R3(diagnosticHost, invocationId, "owner-assign"u8,
            descriptor.Selector, applicationIdentity, 0, "generation-committed"u8,
            generation);
        EmitC188R12R3(diagnosticHost, invocationId, "exit-success"u8,
            descriptor.Selector, applicationIdentity, 0, "resident"u8,
            generation);
#endif
        return true;
    }

#if HOSTLOGPROOF_C188R12R3_LIFETIME
    private void EmitC188R12R3(
        GuideXosHost host,
        uint invocationId,
        ReadOnlySpan<byte> stage,
        uint selector,
        ReadOnlySpan<byte> identity,
        int resultCode,
        ReadOnlySpan<byte> state,
        uint generation)
    {
        uint mask = 0u;
        uint count = 0u;
        for (int index = 0; index < _entries.Length; index++)
        {
            if (_entries[index].Application == null) continue;
            count++;
            mask |= 1u << index;
        }
        C188R12R3LifetimeDiagnostics.Emit(host, invocationId, stage,
            selector, identity, count, mask, generation, resultCode, state);
    }
#endif

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
        GuideXosApplication application = _entries[entryIndex].Application;
        if (application != null)
        {
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
            _c188r11ClearEvents++;
            _c188r11TeardownEvents++;
#endif
            application.OnTearingDown();
        }
        _entries[entryIndex] = default;
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
        RecordC188R11Write(2u); // Completed generation-checked Clear.
#endif
        return true;
    }

    public void Clear(uint selector)
    {
        int entryIndex = FindEntry(selector);
        if (entryIndex < 0) return;
        GuideXosApplication application = _entries[entryIndex].Application;
        if (application != null)
        {
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
            _c188r11ClearEvents++;
            _c188r11TeardownEvents++;
#endif
            application.OnTearingDown();
        }
        _entries[entryIndex] = default;
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
        RecordC188R11Write(3u); // Completed selector Clear.
#endif
    }

#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
    private void RecordC188R11Write(uint site)
    {
        _c188r11LastWriteSite = site;
        if (_c188r11WriteEvents != uint.MaxValue) _c188r11WriteEvents++;
    }
#endif

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

#if HOSTLOGPROOF_C188R12R3_LIFETIME
/// <summary>
/// Bounded C188R12R3 trace formatter. Guest diagnostics use GuideXosHost's
/// existing synchronous C102 logger; the optional capture buffer exists only
/// in the hosted lifetime test build.
/// </summary>
internal static class C188R12R3LifetimeDiagnostics
{
#if HOSTLOGPROOF_C188R12R3_LIFETIME_TESTS
    private const int TestLineCapacity = 128;
    private static readonly byte[][] s_testLines = new byte[TestLineCapacity][];
    private static int s_testLineCount;
    private static bool s_testCaptureEnabled;

    internal static int TestLineCount => s_testLineCount;

    internal static ReadOnlySpan<byte> TestLine(int index) =>
        index >= 0 && index < s_testLineCount ? s_testLines[index] : default;

    internal static void BeginTestCapture()
    {
        Array.Clear(s_testLines);
        s_testLineCount = 0;
        s_testCaptureEnabled = true;
    }

    internal static void EndTestCapture() => s_testCaptureEnabled = false;
#endif

    internal static ReadOnlySpan<byte> IdentityFor(uint selector,
        byte[] descriptorName) => selector switch
    {
        4u => "com.guidexos.apps.managed.notes"u8,
        5u => "com.guidexos.apps.managed.settingscenter"u8,
        6u => "com.guidexos.apps.managed.calculator"u8,
        7u => "com.guidexos.apps.managed.taskmanager"u8,
        8u => "com.guidexos.apps.managed.fileexplorer"u8,
        _ => "other"u8,
    };

    internal static bool CaptureEnabled
    {
        get
        {
#if HOSTLOGPROOF_C188R12R3_LIFETIME_TESTS
            return s_testCaptureEnabled;
#else
            return false;
#endif
        }
    }

    internal static void Emit(
        GuideXosHost host,
        uint invocationId,
        ReadOnlySpan<byte> stage,
        uint selector,
        ReadOnlySpan<byte> identity,
        uint residentCount,
        uint residentMask,
        uint generation,
        int resultCode,
        ReadOnlySpan<byte> state)
    {
        if (host == null && !CaptureEnabled) return;

        Span<byte> line = stackalloc byte[224];
        int position = 0;
        if (!GuideXosText.Append(line, ref position,
                "C188R12R3_LIFETIME invocation="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position, invocationId) ||
            !GuideXosText.Append(line, ref position, " stage="u8) ||
            !GuideXosText.Append(line, ref position, stage) ||
            !GuideXosText.Append(line, ref position, " selector="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position, selector) ||
            !GuideXosText.Append(line, ref position, " identity="u8) ||
            !GuideXosText.Append(line, ref position,
                identity.IsEmpty ? "unknown"u8 : identity) ||
            !GuideXosText.Append(line, ref position, " count="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position, residentCount) ||
            !GuideXosText.Append(line, ref position, " mask="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position, residentMask) ||
            !GuideXosText.Append(line, ref position, " generation="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position, generation) ||
            !GuideXosText.Append(line, ref position, " code="u8) ||
            !AppendSigned(line, ref position, resultCode) ||
            !GuideXosText.Append(line, ref position, " state="u8) ||
            !GuideXosText.Append(line, ref position, state))
        {
            return;
        }

#if HOSTLOGPROOF_C188R12R3_LIFETIME_TESTS
        if (s_testCaptureEnabled && s_testLineCount < TestLineCapacity)
            s_testLines[s_testLineCount++] = line[..position].ToArray();
#endif
        if (host != null) _ = host.TryLog(line[..position]);
    }

    private static bool AppendSigned(Span<byte> line, ref int position,
        int value)
    {
        if (value < 0)
        {
            long magnitude = -(long)value;
            return GuideXosText.Append(line, ref position, "-"u8) &&
                GuideXosText.AppendUnsigned(line, ref position,
                    (uint)magnitude);
        }
        return GuideXosText.AppendUnsigned(line, ref position, (uint)value);
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
