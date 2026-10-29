using System;

namespace HostLogProof;

public abstract unsafe class GuideXosApplication
{
#if HOSTLOGPROOF_C188R12R4_FACTORY
    protected GuideXosApplication()
    {
        C188R12R4FactoryDiagnostics.EmitBaseConstructorEnter();
    }
#endif

    public abstract GuideXosResult Launch(GuideXosHost host);
    public virtual void OnTearingDown() { }
    public virtual GuideXosResult HandleAction(GuideXosHost host, uint actionId)
    {
        return GuideXosResult.InvalidAction;
    }
    /// <summary>
    /// Explicit opt-in for C180 compositor resize actions. Applications
    /// provide their own bounds and current outer size; the registry owns
    /// candidate validation and transaction coordination.
    /// </summary>
    internal virtual bool TryGetManagedResizeGeometryC180(
        out GuideXosManagedWindowResizeBoundsC174 bounds,
        out int outerWidth, out int outerHeight)
    {
        bounds = default;
        outerWidth = 0;
        outerHeight = 0;
        return false;
    }
    internal virtual GuideXosResult HandleManagedResizeC180(
        GuideXosHost host,
        ref GuideXosManagedWindowResizeTransactionC174 transaction) =>
        GuideXosResult.InvalidAction;

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
#if HOSTLOGPROOF_MANAGED_APP_RETURN || HOSTLOGPROOF_C185_BOUNDARY
        Factory = null;
#endif
    }

#if HOSTLOGPROOF_MANAGED_APP_RETURN || HOSTLOGPROOF_C185_BOUNDARY
    public GuideXosApplicationDescriptor(
        uint selector,
        ReadOnlySpan<byte> name,
        Func<GuideXosApplication> factory)
    {
        Selector = selector;
        Name = name.ToArray();
        Application = null;
        Factory = factory;
    }
#endif

    public uint Selector { get; }
    public byte[] Name { get; }
    public GuideXosApplication Application { get; }
#if HOSTLOGPROOF_MANAGED_APP_RETURN || HOSTLOGPROOF_C185_BOUNDARY
    public Func<GuideXosApplication> Factory { get; }
#endif
}

#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
internal struct C188R11SequenceCounter
{
    private readonly uint _limit;
    private uint _value;

    public C188R11SequenceCounter(uint limit)
    {
        _limit = limit;
        _value = 0u;
    }

    public uint Next()
    {
        if (_value >= _limit) return 0u;
        return ++_value;
    }
}

internal static class C188R11DiagnosticSequence
{
    private static C188R11SequenceCounter s_counter = new(128u);

    // This counter belongs to the managed diagnostic image and lasts for its
    // static-state lifetime. Zero means the bounded sequence is full.
    internal static uint Next() => s_counter.Next();
}

internal readonly struct C188R11ObjectIdentitySnapshot
{
    public C188R11ObjectIdentitySnapshot(uint identity,
        uint initializationEpoch, uint replacementEvents, uint clearEvents)
    {
        Identity = identity;
        InitializationEpoch = initializationEpoch;
        ReplacementEvents = replacementEvents;
        ClearEvents = clearEvents;
    }

    public uint Identity { get; }
    public uint InitializationEpoch { get; }
    public uint ReplacementEvents { get; }
    public uint ClearEvents { get; }
}

/// <summary>
/// Proof-only identity based on reference equality. The current reference is
/// deliberately retained; no object address or GC pin is involved.
/// </summary>
internal sealed class C188R11ObjectIdentityTracker
{
    private object _current;
    private uint _nextIdentity;
    private uint _identity;
    private uint _initializationEpoch;
    private uint _replacementEvents;
    private uint _clearEvents;

    internal object Current => _current;

    public uint Observe(object current)
    {
        if (ReferenceEquals(_current, current)) return _identity;
        if (current == null)
        {
            _current = null;
            _identity = 0u;
            return 0u;
        }

        _current = current;
        _identity = ++_nextIdentity;
        if (_nextIdentity > 1u) _replacementEvents++;
        return _identity;
    }

    public void RecordInitialization(object storage)
    {
        Observe(storage);
        _initializationEpoch++;
    }

    public void RecordClear(object storage)
    {
        Observe(storage);
        _clearEvents++;
    }

    public C188R11ObjectIdentitySnapshot Capture(object current)
    {
        Observe(current);
        return new C188R11ObjectIdentitySnapshot(_identity,
            _initializationEpoch, _replacementEvents, _clearEvents);
    }
}

internal readonly struct C188R11ProvenanceSnapshot
{
    public C188R11ProvenanceSnapshot(
        C188R11ObjectIdentitySnapshot registryIdentity,
        C188R11ObjectIdentitySnapshot poolIdentity,
        C188R11ObjectIdentitySnapshot poolStorageIdentity,
        uint registryInitializationEpoch, uint poolInitializationEpoch,
        uint descriptorCount, uint notesOwnerGeneration,
        uint residentMask, uint poolClearEvents, uint residentReplacementEvents,
        uint resetEvents, uint teardownEvents, uint generationHighWatermark,
        uint sequence, uint poolWriteEvents, uint lastPoolWriteSite,
        uint managedGcCollections, nuint registryObjectAddress,
        nuint poolOwnerObjectAddress, nuint poolEntriesFieldAddress,
        nuint livePoolStorageObjectAddress,
        nuint trackedPoolStorageObjectAddress, uint registryGcGeneration,
        uint poolOwnerGcGeneration, uint livePoolStorageGcGeneration,
        uint trackedPoolStorageGcGeneration, uint trackedPoolStorageLength)
    {
        RegistryIdentity = registryIdentity;
        PoolIdentity = poolIdentity;
        PoolStorageIdentity = poolStorageIdentity;
        RegistryInitializationEpoch = registryInitializationEpoch;
        PoolInitializationEpoch = poolInitializationEpoch;
        DescriptorCount = descriptorCount;
        NotesOwnerGeneration = notesOwnerGeneration;
        ResidentMask = residentMask;
        PoolClearEvents = poolClearEvents;
        ResidentReplacementEvents = residentReplacementEvents;
        ResetEvents = resetEvents;
        TeardownEvents = teardownEvents;
        GenerationHighWatermark = generationHighWatermark;
        Sequence = sequence;
        PoolWriteEvents = poolWriteEvents;
        LastPoolWriteSite = lastPoolWriteSite;
        ManagedGcCollections = managedGcCollections;
        RegistryObjectAddress = registryObjectAddress;
        PoolOwnerObjectAddress = poolOwnerObjectAddress;
        PoolEntriesFieldAddress = poolEntriesFieldAddress;
        LivePoolStorageObjectAddress = livePoolStorageObjectAddress;
        TrackedPoolStorageObjectAddress = trackedPoolStorageObjectAddress;
        RegistryGcGeneration = registryGcGeneration;
        PoolOwnerGcGeneration = poolOwnerGcGeneration;
        LivePoolStorageGcGeneration = livePoolStorageGcGeneration;
        TrackedPoolStorageGcGeneration = trackedPoolStorageGcGeneration;
        TrackedPoolStorageLength = trackedPoolStorageLength;
    }

    public C188R11ObjectIdentitySnapshot RegistryIdentity { get; }
    public C188R11ObjectIdentitySnapshot PoolIdentity { get; }
    public C188R11ObjectIdentitySnapshot PoolStorageIdentity { get; }
    public uint RegistryInitializationEpoch { get; }
    public uint PoolInitializationEpoch { get; }
    public uint DescriptorCount { get; }
    public uint NotesOwnerGeneration { get; }
    public uint ResidentMask { get; }
    public uint PoolClearEvents { get; }
    public uint ResidentReplacementEvents { get; }
    public uint ResetEvents { get; }
    public uint TeardownEvents { get; }
    public uint GenerationHighWatermark { get; }
    public uint Sequence { get; }
    public uint PoolWriteEvents { get; }
    public uint LastPoolWriteSite { get; }
    public uint ManagedGcCollections { get; }
    public nuint RegistryObjectAddress { get; }
    public nuint PoolOwnerObjectAddress { get; }
    public nuint PoolEntriesFieldAddress { get; }
    public nuint LivePoolStorageObjectAddress { get; }
    public nuint TrackedPoolStorageObjectAddress { get; }
    public uint RegistryGcGeneration { get; }
    public uint PoolOwnerGcGeneration { get; }
    public uint LivePoolStorageGcGeneration { get; }
    public uint TrackedPoolStorageGcGeneration { get; }
    public uint TrackedPoolStorageLength { get; }
}
#endif

/// <summary>
/// Compile-time, bounded registry. There is no reflection or assembly scan.
/// </summary>
public static unsafe class GuideXosApplicationRegistry
{
    public const uint LifetimeCloseActionId = 0x01500002u;
#if HOSTLOGPROOF_C188R8_NOTES_INPUT_ROUTING
    private static uint s_c188r8InputTraceRecords;
#endif
#if HOSTLOGPROOF_C188R9_REGISTRY_INPUT_REENTRY
    private static uint s_c188r9RegistryTraceRecords;
#endif
#if HOSTLOGPROOF_C188R10_MANAGED_STATE_REENTRY
    private static uint s_c188r10RegistryTraceRecords;
    private static uint s_c188r10TeardownCalls;
    private static uint s_c188r10RegistryInitializationGeneration = 1u;
#endif
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
    private static uint s_c188r12r1SettingsDispatchTraceRecords;
    private static uint s_c188r12r1SettingsLaunchEntryTraceRecords;
    private static uint s_c188r12r1SettingsLaunchReturnTraceRecords;
    private static uint s_c188r12r2SettingsLaunchTraceRequests;
    private static uint s_c188r11NotesRegistrationSequence;
    private static uint s_c188r11SettingsRegistrationSequence;
    private static uint s_c188r11CalculatorRegistrationSequence;
    private static uint s_c188r11TaskManagerRegistrationSequence;
    private static uint s_c188r11FileExplorerRegistrationSequence;
    private static readonly C188R11ObjectIdentityTracker
        s_c188r11RegistryIdentity = new();
    private static readonly C188R11ObjectIdentityTracker
        s_c188r11PoolIdentity = new();
    private static readonly C188R11ObjectIdentityTracker
        s_c188r11PoolStorageIdentity = new();
    private static uint s_c188r11RegistryInitializationEpoch;
    private static uint s_c188r11RegistryInitializationSequence;
    private static uint s_c188r11ResetEvents = 0u;
    private static bool s_c188r11InitializationEventReported;
    private static uint s_c188r11RegistryTraceRecords;
    private static uint s_c188r11MemoryTraceRecords;
    private static uint s_c188r11r2KeyboardRecords;
#endif
#if GUIDEXOS_PROOF_CONTROL
    private static bool s_proofConfigConsumed;
#endif

#if HOSTLOGPROOF_MANAGED_APP_RETURN
    private static readonly GuideXosApplicationDescriptor[] s_entries =
    {
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
        RegisterC188R11Descriptor(4u, "Managed Notes"u8,
            static () => new Applications.ManagedNotes()),
        RegisterC188R11Descriptor(5u, "Managed Settings Center"u8,
#if HOSTLOGPROOF_C188R12R4_FACTORY
            static () => C188R12R4FactoryDiagnostics.CreateSettingsCenter()),
#else
            static () => new Applications.ManagedSettingsCenter()),
#endif
#if HOSTLOGPROOF_C158_MANAGED_CALCULATOR
        RegisterC188R11Descriptor(6u, "Managed Calculator"u8,
            static () => new Applications.ManagedCalculatorC158()),
#endif
#if HOSTLOGPROOF_C161_MANAGED_TASK_MANAGER
        RegisterC188R11Descriptor(7u, "Managed Task Manager"u8,
            static () => new Applications.ManagedTaskManagerC161()),
#endif
#if HOSTLOGPROOF_C163_MANAGED_FILE_EXPLORER
        RegisterC188R11Descriptor(8u, "Managed File Explorer"u8,
            static () => new Applications.ManagedFileExplorerC163()),
#endif
#else
        new(4u, "Managed Notes"u8, static () => new Applications.ManagedNotes()),
        new(5u, "Managed Settings Center"u8,
#if HOSTLOGPROOF_C188R12R4_FACTORY
            static () => C188R12R4FactoryDiagnostics.CreateSettingsCenter()),
#else
            static () => new Applications.ManagedSettingsCenter()),
#endif
#if HOSTLOGPROOF_C158_MANAGED_CALCULATOR
        new(6u, "Managed Calculator"u8,
            static () => new Applications.ManagedCalculatorC158()),
#endif
#if HOSTLOGPROOF_C161_MANAGED_TASK_MANAGER
        new(7u, "Managed Task Manager"u8,
            static () => new Applications.ManagedTaskManagerC161()),
#endif
#if HOSTLOGPROOF_C163_MANAGED_FILE_EXPLORER
        new(8u, "Managed File Explorer"u8,
            static () => new Applications.ManagedFileExplorerC163()),
#endif
#endif
    };
    private static readonly GuideXosManagedApplicationLifetime s_lifetime = new();
#if HOSTLOGPROOF_C150_MANAGED_APP_RETURN
    private static bool s_lifecycleTestsRun;
#endif
#elif HOSTLOGPROOF_C147_RUNTIME_SETTINGS
    private static readonly GuideXosApplicationDescriptor[] s_entries =
    {
        new(4u, "Managed Notes"u8, new Applications.ManagedNotes()),
        new(5u, "Managed Settings Center"u8, new Applications.ManagedSettingsCenter()),
    };
#elif HOSTLOGPROOF_C146_PERSISTENT_SETTINGS
    private static readonly GuideXosApplicationDescriptor[] s_entries =
    {
        new(5u, "Managed Settings Center"u8, new Applications.ManagedSettingsCenter()),
    };
#else
    private static readonly GuideXosApplicationDescriptor[] s_entries =
    {
        new(1u, "Managed Workspace"u8, new Applications.ManagedWorkspace()),
        new(2u, "Managed Status"u8, new Applications.ManagedStatus()),
        new(3u, "Managed Counter"u8, new Applications.ManagedCounter()),
        new(4u, "Managed Notes"u8, new Applications.ManagedNotes()),
#if HOSTLOGPROOF_C145_MANAGED_MODAL_DIALOG
        new(5u, "Managed Settings Center"u8, new Applications.ManagedSettingsCenter()),
#elif HOSTLOGPROOF_C144_MANAGED_SETTINGS_CENTER
        new(5u, "Managed Settings Center"u8, new Applications.ManagedSettingsCenter()),
#elif HOSTLOGPROOF_C143_MANAGED_GROUP_BOX
        new(5u, "Managed GroupBox"u8, new Applications.ManagedGroupBoxDemo()),
#elif HOSTLOGPROOF_C142_MANAGED_VERTICAL_STACK
        new(5u, "Managed Vertical Stack"u8, new Applications.ManagedVerticalStackDemo()),
#elif HOSTLOGPROOF_C141_MANAGED_VERTICAL_STACK
        new(5u, "Managed Vertical Stack"u8, new Applications.ManagedVerticalStackDemo()),
#elif HOSTLOGPROOF_C140_MANAGED_SCROLL_VIEW
        new(5u, "Managed ScrollView"u8, new Applications.ManagedScrollViewDemo()),
#endif
    };
#endif

#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
    static GuideXosApplicationRegistry()
    {
        // The descriptor and lifetime field initializers have allocated their
        // backing objects before this actual type-initialization event.
        s_c188r11RegistryInitializationEpoch++;
        s_c188r11RegistryIdentity.RecordInitialization(s_entries);
        s_c188r11PoolIdentity.RecordInitialization(s_lifetime);
        s_c188r11PoolStorageIdentity.RecordInitialization(
            s_lifetime.C188R11StorageObject);
        s_c188r11RegistryInitializationSequence =
            C188R11DiagnosticSequence.Next();
    }

    private static GuideXosApplicationDescriptor RegisterC188R11Descriptor(
        uint selector, ReadOnlySpan<byte> name,
        Func<GuideXosApplication> factory)
    {
        uint sequence = C188R11DiagnosticSequence.Next();
        switch (selector)
        {
            case 4u: s_c188r11NotesRegistrationSequence = sequence; break;
            case 5u: s_c188r11SettingsRegistrationSequence = sequence; break;
            case 6u: s_c188r11CalculatorRegistrationSequence = sequence; break;
            case 7u: s_c188r11TaskManagerRegistrationSequence = sequence; break;
            case 8u: s_c188r11FileExplorerRegistrationSequence = sequence; break;
        }
        return new GuideXosApplicationDescriptor(selector, name, factory);
    }

    internal static C188R11ProvenanceSnapshot C188R11CaptureState(
        uint sequence = 0u)
    {
        // These addresses are converted from live, rooted references to scalar
        // diagnostics only. They are never dereferenced by managed code or
        // retained across calls; cross-boundary comparison is valid only when
        // the GC collection count is unchanged.
        // Retain the previous pool-array reference locally before Observe(null)
        // clears the identity tracker. This lets the input snapshot distinguish
        // a lost owner field from storage reclamation or an invalid object.
        object trackedPoolStorage = s_c188r11PoolStorageIdentity.Current;
        object livePoolStorage = s_lifetime.C188R11StorageObject;
        C188R11ObjectIdentitySnapshot registry =
            s_c188r11RegistryIdentity.Capture(s_entries);
        C188R11ObjectIdentitySnapshot pool =
            s_c188r11PoolIdentity.Capture(s_lifetime);
        C188R11ObjectIdentitySnapshot poolStorage =
            s_c188r11PoolStorageIdentity.Capture(
                s_lifetime.C188R11StorageObject);
        uint residentMask = s_lifetime.C188R11ResidentMask;
        C188R11ProvenanceSnapshot snapshot = new(
            registry, pool, poolStorage,
            s_c188r11RegistryInitializationEpoch,
            s_lifetime.C188R11InitializationEpoch,
            (uint)s_entries.Length, s_lifetime.C188R11NotesOwnerGeneration,
            residentMask, s_lifetime.C188R11ClearEvents,
            s_lifetime.C188R11ReplacementEvents,
            s_c188r11ResetEvents, s_lifetime.C188R11TeardownEvents,
            s_lifetime.C188R11Generation, sequence,
            s_lifetime.C188R11WriteEvents,
            s_lifetime.C188R11LastWriteSite,
            unchecked((uint)GC.CollectionCount(0)),
            C188R11ObjectAddress(s_entries),
            C188R11ObjectAddress(s_lifetime),
            s_lifetime.C188R11EntriesFieldAddress,
            C188R11ObjectAddress(livePoolStorage),
            C188R11ObjectAddress(trackedPoolStorage),
            C188R11GcGeneration(s_entries),
            C188R11GcGeneration(s_lifetime),
            C188R11GcGeneration(livePoolStorage),
            C188R11GcGeneration(trackedPoolStorage),
            trackedPoolStorage is Array trackedArray
                ? unchecked((uint)trackedArray.Length) : 0u);
        GC.KeepAlive(trackedPoolStorage);
        return snapshot;
    }

    private static nuint C188R11ObjectAddress(object value)
    {
        return value == null ? (nuint)0 : unchecked((nuint)
            System.Runtime.CompilerServices.Unsafe.As<object, nint>(ref value));
    }

    private static uint C188R11GcGeneration(object value)
    {
        return value == null ? uint.MaxValue :
            unchecked((uint)GC.GetGeneration(value));
    }

    private static void LogC188R12R2SettingsLaunch(
        GuideXosHost host,
        ReadOnlySpan<byte> stage,
        ReadOnlySpan<byte> result,
        uint generation,
        int resultCode = int.MinValue)
    {
        if (host == null) return;
        Span<byte> line = stackalloc byte[144];
        int position = 0;
        if (!GuideXosText.Append(line, ref position,
                "C188R12R2_LAUNCH stage="u8) ||
            !GuideXosText.Append(line, ref position, stage) ||
            !GuideXosText.Append(line, ref position,
                " appId=com.guidexos.apps.managed.settingscenter selector=5 generation="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position, generation) ||
            !GuideXosText.Append(line, ref position, " result="u8) ||
            !GuideXosText.Append(line, ref position, result))
        {
            return;
        }
        if (resultCode != int.MinValue &&
            (!GuideXosText.Append(line, ref position, " code="u8) ||
             !AppendSigned(line, ref position, resultCode)))
        {
            return;
        }
        host.TryLog(line[..position]);
    }

#if HOSTLOGPROOF_MANAGED_APP_RETURN
    internal static bool C188R11TryGetResident(uint selector,
        uint generation, out GuideXosApplication application) =>
        s_lifetime.TryGet(selector, generation, out application);
#endif

    private static void LogC188R11InitializationEvent(GuideXosHost host)
    {
        if (s_c188r11InitializationEventReported) return;
        s_c188r11InitializationEventReported = true;
        LogC188R11Registration(host, 4u, s_c188r11NotesRegistrationSequence);
        LogC188R11Registration(host, 5u, s_c188r11SettingsRegistrationSequence);
        LogC188R11Registration(host, 6u, s_c188r11CalculatorRegistrationSequence);
        LogC188R11Registration(host, 7u, s_c188r11TaskManagerRegistrationSequence);
        LogC188R11Registration(host, 8u, s_c188r11FileExplorerRegistrationSequence);
        C188R11ProvenanceSnapshot state =
            C188R11CaptureState(s_c188r11RegistryInitializationSequence);
        LogC188R11Memory(host, "registry-init"u8, state);
        Span<byte> line = stackalloc byte[112];
        int position = 0;
        if (GuideXosText.Append(line, ref position, "C188R11-INIT seq="u8) &&
            GuideXosText.AppendUnsigned(line, ref position, state.Sequence) &&
            GuideXosText.Append(line, ref position, " re="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.RegistryInitializationEpoch) &&
            GuideXosText.Append(line, ref position, " pe="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.PoolInitializationEpoch) &&
            GuideXosText.Append(line, ref position, " rg="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.RegistryIdentity.Identity) &&
            GuideXosText.Append(line, ref position, " p="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.PoolIdentity.Identity) &&
            GuideXosText.Append(line, ref position, " ps="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.PoolStorageIdentity.Identity))
        {
            host.TryLog(line[..position]);
        }
    }

    private static void LogC188R11Registration(
        GuideXosHost host, uint selector, uint sequence)
    {
        if (sequence == 0u) return;
        Span<byte> line = stackalloc byte[64];
        int position = 0;
        if (GuideXosText.Append(line, ref position,
                "C188R11-REG seq="u8) &&
            GuideXosText.AppendUnsigned(line, ref position, sequence) &&
            GuideXosText.Append(line, ref position, " selector="u8) &&
            GuideXosText.AppendUnsigned(line, ref position, selector))
        {
            host.TryLog(line[..position]);
        }
    }

#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
    internal static void LogC188R11R2Keyboard(GuideXosHost host,
        ReadOnlySpan<byte> stage, int resultCode)
    {
        if (host == null || host.Selector != 4u ||
            !host.LaunchContext.IsInput ||
            (host.LaunchContext.InputKind != GuideXosInputKind.KeyDown &&
             host.LaunchContext.InputKind != GuideXosInputKind.KeyChar) ||
            s_c188r11r2KeyboardRecords >= 16u)
        {
            return;
        }
        uint sequence = C188R11DiagnosticSequence.Next();
        if (sequence == 0u) return;
        s_c188r11r2KeyboardRecords++;
        ReadOnlySpan<byte> kind = host.LaunchContext.InputKind ==
                GuideXosInputKind.KeyChar
            ? "key-char"u8 : "key-down"u8;
        Span<byte> line = stackalloc byte[160];
        int position = 0;
        if (GuideXosText.Append(line, ref position,
                "C188R11R2_KEY seq="u8) &&
            GuideXosText.AppendUnsigned(line, ref position, sequence) &&
            GuideXosText.Append(line, ref position, " stage="u8) &&
            GuideXosText.Append(line, ref position, stage) &&
            GuideXosText.Append(line, ref position, " selector="u8) &&
            GuideXosText.AppendUnsigned(line, ref position, host.Selector) &&
            GuideXosText.Append(line, ref position, " kind="u8) &&
            GuideXosText.Append(line, ref position, kind) &&
            GuideXosText.Append(line, ref position, " value="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                host.LaunchContext.InputKeyCode) &&
            GuideXosText.Append(line, ref position, " shift="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                host.LaunchContext.InputShift ? 1u : 0u) &&
            GuideXosText.Append(line, ref position, " ctrl="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                host.LaunchContext.InputControl ? 1u : 0u) &&
            GuideXosText.Append(line, ref position, " result="u8) &&
            AppendC188R11Signed(line, ref position, resultCode))
        {
            host.TryLog(line[..position]);
        }
    }
#endif

    private static void LogC188R11State(GuideXosHost host,
        ReadOnlySpan<byte> stage, uint selector, uint generation,
        int resultCode, uint sequence = 0u)
    {
        if ((selector != 4u && selector != 5u) ||
            s_c188r11RegistryTraceRecords >= 48u ||
            (host.LaunchContext.IsInput &&
             (host.LaunchContext.InputKind == GuideXosInputKind.PointerMove ||
              host.LaunchContext.InputKind == GuideXosInputKind.Wheel)))
        {
            return;
        }
        s_c188r11RegistryTraceRecords++;
        if (sequence == 0u) sequence = C188R11DiagnosticSequence.Next();
        if (sequence == 0u) return;
        C188R11ProvenanceSnapshot state = C188R11CaptureState(sequence);
        Span<byte> line = stackalloc byte[352];
        int position = 0;
        if (GuideXosText.Append(line, ref position, "C188R11 state seq="u8) &&
            GuideXosText.AppendUnsigned(line, ref position, state.Sequence) &&
            GuideXosText.Append(line, ref position, " st="u8) &&
            GuideXosText.Append(line, ref position, stage) &&
            GuideXosText.Append(line, ref position, " selector="u8) &&
            GuideXosText.AppendUnsigned(line, ref position, selector) &&
            GuideXosText.Append(line, ref position, " ownerGen="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.NotesOwnerGeneration) &&
            GuideXosText.Append(line, ref position, " rg="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.RegistryIdentity.Identity) &&
            GuideXosText.Append(line, ref position, " re="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.RegistryInitializationEpoch) &&
            GuideXosText.Append(line, ref position, " rr="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.RegistryIdentity.ReplacementEvents) &&
            GuideXosText.Append(line, ref position, " rcl="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.RegistryIdentity.ClearEvents) &&
            GuideXosText.Append(line, ref position, " p="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.PoolIdentity.Identity) &&
            GuideXosText.Append(line, ref position, " pr="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.PoolIdentity.ReplacementEvents) &&
            GuideXosText.Append(line, ref position, " ps="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.PoolStorageIdentity.Identity) &&
            GuideXosText.Append(line, ref position, " psr="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.PoolStorageIdentity.ReplacementEvents) &&
            GuideXosText.Append(line, ref position, " pe="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.PoolInitializationEpoch) &&
            GuideXosText.Append(line, ref position, " sr="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.ResidentReplacementEvents) &&
            GuideXosText.Append(line, ref position, " pc="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.PoolClearEvents) &&
            GuideXosText.Append(line, ref position, " reset="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.ResetEvents) &&
            GuideXosText.Append(line, ref position, " m="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.ResidentMask) &&
            GuideXosText.Append(line, ref position, " g="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.GenerationHighWatermark) &&
            GuideXosText.Append(line, ref position, " td="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.TeardownEvents) &&
            GuideXosText.Append(line, ref position, " n="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.DescriptorCount) &&
            GuideXosText.Append(line, ref position, " result="u8) &&
            AppendC188R11Signed(line, ref position, resultCode) &&
            GuideXosText.Append(line, ref position, " gc="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.ManagedGcCollections) &&
            GuideXosText.Append(line, ref position, " w="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.PoolWriteEvents) &&
            GuideXosText.Append(line, ref position, ":"u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.LastPoolWriteSite))
        {
            host.TryLog(line[..position]);
            LogC188R11Memory(host, stage, state);
        }
    }

    private static void LogC188R11Memory(GuideXosHost host,
        ReadOnlySpan<byte> stage, C188R11ProvenanceSnapshot state)
    {
        if (s_c188r11MemoryTraceRecords >= 48u) return;
        Span<byte> line = stackalloc byte[224];
        int position = 0;
        if (GuideXosText.Append(line, ref position,
                "C188R11-MEM seq="u8) &&
            GuideXosText.AppendUnsigned(line, ref position, state.Sequence) &&
            GuideXosText.Append(line, ref position, " st="u8) &&
            GuideXosText.Append(line, ref position, stage) &&
            GuideXosText.Append(line, ref position, " ra="u8) &&
            AppendC188R11Address(line, ref position,
                state.RegistryObjectAddress) &&
            GuideXosText.Append(line, ref position, " owner="u8) &&
            AppendC188R11Address(line, ref position,
                state.PoolOwnerObjectAddress) &&
            GuideXosText.Append(line, ref position, " slot="u8) &&
            AppendC188R11Address(line, ref position,
                state.PoolEntriesFieldAddress) &&
            GuideXosText.Append(line, ref position, " live="u8) &&
            AppendC188R11Address(line, ref position,
                state.LivePoolStorageObjectAddress) &&
            GuideXosText.Append(line, ref position, " held="u8) &&
            AppendC188R11Address(line, ref position,
                state.TrackedPoolStorageObjectAddress) &&
            GuideXosText.Append(line, ref position, " gen="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.RegistryGcGeneration) &&
            GuideXosText.Append(line, ref position, ":"u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.PoolOwnerGcGeneration) &&
            GuideXosText.Append(line, ref position, ":"u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.LivePoolStorageGcGeneration) &&
            GuideXosText.Append(line, ref position, ":"u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.TrackedPoolStorageGcGeneration) &&
            GuideXosText.Append(line, ref position, " gc="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.ManagedGcCollections) &&
            GuideXosText.Append(line, ref position, " heldLen="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                state.TrackedPoolStorageLength))
        {
            s_c188r11MemoryTraceRecords++;
            host.TryLog(line[..position]);
        }
    }

    private static bool AppendC188R11Address(Span<byte> buffer,
        ref int position, nuint value)
    {
        const string digits = "0123456789ABCDEF";
        if (buffer.Length - position < 16) return false;
        ulong address = unchecked((ulong)value);
        for (int shift = 60; shift >= 0; shift -= 4)
        {
            buffer[position++] = (byte)digits[(int)((address >> shift) & 0xFu)];
        }
        return true;
    }

    private static bool AppendC188R11Signed(
        Span<byte> buffer, ref int position, int value)
    {
        if (value < 0)
        {
            if (!GuideXosText.Append(buffer, ref position, "-"u8)) return false;
            return GuideXosText.AppendUnsigned(buffer, ref position,
                unchecked((uint)(-(long)value)));
        }
        return GuideXosText.AppendUnsigned(buffer, ref position, (uint)value);
    }

    private static void LogC188R11LifetimeLookup(GuideXosHost host,
        uint selector, uint generation, bool found)
    {
        if (selector != 4u) return;
        Span<byte> line = stackalloc byte[64];
        int position = 0;
        if (GuideXosText.Append(line, ref position,
                "C188R11-LIFETIME id=4 gen="u8) &&
            GuideXosText.AppendUnsigned(line, ref position, generation) &&
            GuideXosText.Append(line, ref position, " result="u8) &&
            GuideXosText.AppendUnsigned(line, ref position, found ? 0u : 1u))
        {
            host.TryLog(line[..position]);
        }
    }
#endif

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

    public static int RegistrationCount => s_entries.Length;

#if HOSTLOGPROOF_C188R10_MANAGED_STATE_REENTRY
    internal static uint C188R10RegistryOwnerIdentity =>
        C188R10OwnerToken(System.Runtime.CompilerServices.Unsafe.AsPointer(
            ref System.Runtime.CompilerServices.Unsafe.AsRef(in s_entries)));

    internal static uint C188R10LifetimeOwnerIdentity =>
        C188R10OwnerToken(System.Runtime.CompilerServices.Unsafe.AsPointer(
            ref System.Runtime.CompilerServices.Unsafe.AsRef(in s_lifetime)));

    internal static uint C188R10RegistryInitializationGeneration =>
        s_c188r10RegistryInitializationGeneration;

    internal static bool C188R10TryGetResident(uint selector,
        uint generation, out GuideXosApplication application) =>
        s_lifetime.TryGet(selector, generation, out application);

    private static uint C188R10OwnerToken(void* ownerSlot)
    {
        ulong value = (ulong)(nuint)ownerSlot;
        value ^= value >> 33;
        value *= 0xff51afd7ed558ccdUL;
        value ^= value >> 33;
        value *= 0xc4ceb9fe1a85ec53UL;
        value ^= value >> 33;
        return (uint)(value ^ (value >> 32));
    }

#endif

#if HOSTLOGPROOF_MANAGED_APP_RETURN
    private static bool ClearResident(
        GuideXosHost host, uint selector, uint generation)
    {
#if HOSTLOGPROOF_C188R10_MANAGED_STATE_REENTRY
        s_c188r10TeardownCalls++;
#endif
        bool cleared = s_lifetime.Clear(selector, generation);
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
        if (cleared && selector == 4u)
            LogC188R11State(host, "resident-clear-teardown"u8, selector,
                generation, 0);
#endif
        return cleared;
    }
#endif

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

#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
        if (selector == 5u && s_c188r12r1SettingsDispatchTraceRecords < 16u)
        {
            ++s_c188r12r1SettingsDispatchTraceRecords;
            host.TryLog("C188R12R1-MANAGED stage=dispatch-entry selector=5 result=BEGIN"u8);
            LogC188R11State(host, "settings-dispatch-entry"u8, selector,
                host.LaunchContext.ApplicationGeneration, -1);
        }
        if (selector == 4u)
        {
            LogC188R11InitializationEvent(host);
            LogC188R11State(host,
                host.LaunchContext.IsInput
                    ? "input-dispatch-entry"u8
                    : "launch-dispatch-entry"u8, selector,
                host.LaunchContext.ApplicationGeneration, -1);
            LogC188R11R2Keyboard(host, "managed-dispatch-entry"u8, -1);
        }
#endif
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
        uint launchGeneration = host.LaunchContext.ApplicationGeneration;
        bool traceSettingsLaunch = selector == 5u &&
            !host.LaunchContext.IsInput && !host.IsAction &&
            !host.IsCapabilityProbe && !host.IsAbiProbe &&
            s_c188r12r2SettingsLaunchTraceRequests < 4u;
        if (traceSettingsLaunch)
        {
            ++s_c188r12r2SettingsLaunchTraceRequests;
            LogC188R12R2SettingsLaunch(host, "dispatch-context"u8,
                "PASS"u8, launchGeneration);
        }
#endif
#if HOSTLOGPROOF_C188R9_REGISTRY_INPUT_REENTRY
        if (selector == 4u &&
            (host.LaunchContext.IsInput ||
             (!host.IsAction && !host.IsCapabilityProbe && !host.IsAbiProbe)))
        {
            LogC188R9RegistryState(host, "dispatch-entry"u8, selector,
                host.LaunchContext.ApplicationGeneration, -1);
        }
#endif
#if HOSTLOGPROOF_C188R10_MANAGED_STATE_REENTRY
        LogC188R10State(host, "dispatch"u8, selector,
            host.LaunchContext.ApplicationGeneration, -1);
#endif

#if HOSTLOGPROOF_C188R8_NOTES_INPUT_ROUTING
        if (host.LaunchContext.IsInput)
        {
            LogC188R8InputBoundary(host, "managed-entry"u8, selector, 0);
        }
#endif

#if GUIDEXOS_PROOF_CONTROL
        if (!s_proofConfigConsumed)
        {
            Span<byte> proofConfig = stackalloc byte[9];
            GuideXosFileResult proofConfigResult = host.TryReadInto(
                "/system/config/proof1.bin"u8, proofConfig,
                out int proofConfigLength);
            if (proofConfigResult == GuideXosFileResult.Success)
            {
                s_proofConfigConsumed = true;
                if (proofConfigLength != proofConfig.Length ||
                    !ProofSelection.TryParse(proofConfig, out ProofSelection selection))
                {
                    host.TryLog("PROOF-SCENARIO-FAIL INVALID-CONFIG"u8);
                    return GxAbi.ErrorInvalidArgument;
                }
                GuideXosProofCoordinator.Run(host, context, context->host, selection);
                return (int)GuideXosResult.Success;
            }
            if (proofConfigResult != GuideXosFileResult.NotFound)
            {
                s_proofConfigConsumed = true;
                host.TryLog("PROOF-SCENARIO-FAIL CONFIG-READ"u8);
                return GxAbi.ErrorInvalidArgument;
            }
        }
#endif

#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
        if (traceSettingsLaunch)
            LogC188R12R2SettingsLaunch(host, "dispatch-prelude"u8,
                "PASS"u8, launchGeneration);
        if (selector == 4u)
        {
            LogC188R11State(host, "before-tryfind"u8, selector,
                host.LaunchContext.ApplicationGeneration, -1);
        }
        if (traceSettingsLaunch)
            LogC188R12R2SettingsLaunch(host, "registration-lookup"u8,
                "BEGIN"u8, launchGeneration);
#endif
        bool descriptorFound = TryFind(selector,
            out GuideXosApplicationDescriptor descriptor);
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
        if (traceSettingsLaunch)
            LogC188R12R2SettingsLaunch(host, "registration-lookup"u8,
                descriptorFound ? "PASS"u8 : "FAIL"u8, launchGeneration,
                descriptorFound ? 0 : GxAbi.ErrorInvalidApplicationId);
#endif
#if HOSTLOGPROOF_C188R9_REGISTRY_INPUT_REENTRY
        if (selector == 4u &&
            (host.LaunchContext.IsInput ||
             (!host.IsAction && !host.IsCapabilityProbe && !host.IsAbiProbe)))
        {
            LogC188R9RegistryState(host, "descriptor-lookup"u8, selector,
                host.LaunchContext.ApplicationGeneration,
                descriptorFound ? 0 : GxAbi.ErrorInvalidApplicationId);
        }
#endif
#if HOSTLOGPROOF_C188R10_MANAGED_STATE_REENTRY
        LogC188R10State(host, "lookup"u8, selector,
            host.LaunchContext.ApplicationGeneration,
            descriptorFound ? 0 : GxAbi.ErrorInvalidApplicationId);
#endif
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
        if (selector == 4u)
        {
            LogC188R11State(host, "lookup"u8, selector,
                host.LaunchContext.ApplicationGeneration,
                descriptorFound ? 0 : GxAbi.ErrorInvalidApplicationId);
        }
#endif
        if (!descriptorFound)
        {
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
            if (traceSettingsLaunch)
                LogC188R12R2SettingsLaunch(host, "dispatch-return"u8,
                    "FAIL"u8, launchGeneration,
                    GxAbi.ErrorInvalidApplicationId);
#endif
#if HOSTLOGPROOF_C188R8_NOTES_INPUT_ROUTING
            if (host.LaunchContext.IsInput)
                LogC188R8InputBoundary(host, "selector-rejected"u8, selector,
                    GxAbi.ErrorInvalidApplicationId);
#endif
            host.TryLog("C112-UNKNOWN-SELECTOR"u8);
            return GxAbi.ErrorInvalidApplicationId;
        }
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE && HOSTLOGPROOF_MANAGED_APP_RETURN
        if (selector == 4u)
        {
            LogC188R11State(host, "before-resident"u8, selector,
                host.LaunchContext.ApplicationGeneration, -1);
        }
#endif
#if HOSTLOGPROOF_C188R10_MANAGED_STATE_REENTRY
        LogC188R10State(host, "resident"u8, selector,
            host.LaunchContext.ApplicationGeneration, -1);
#endif
#if HOSTLOGPROOF_C188R8_NOTES_INPUT_ROUTING
        if (host.LaunchContext.IsInput)
            LogC188R8InputBoundary(host, "application-found"u8, selector, 0);
#endif

#if HOSTLOGPROOF_C155_MANAGED_NOTES_SESSION
        // C150 may need to discard a pending semantic session after Notes was
        // destroyed. This host-owned one-shot operation intentionally runs
        // before the resident application lifetime lookup.
        if (selector == 4u && host.IsAction &&
            host.LaunchContext.ActionId ==
                GuideXosNotesReturnSessionC155.NativeClearActionId)
        {
            GuideXosNotesReturnSessionC155.Shared.Clear();
            host.TryLog("C155-SESSION-CLEAR pending=none result=PASS"u8);
            return (int)GuideXosResult.Success;
        }
        if (selector == 4u && host.IsAction &&
            host.LaunchContext.ActionId ==
                GuideXosNotesReturnSessionC155.NativeCheckActionId)
        {
            return GuideXosNotesReturnSessionC155.Shared.IsPending
                ? (int)GuideXosResult.Success : (int)GuideXosResult.InvalidAction;
        }
#endif

#if HOSTLOGPROOF_MANAGED_APP_RETURN
        if (host.IsAction && host.LaunchContext.ActionId ==
                LifetimeCloseActionId)
        {
            bool cleared = ClearResident(host, selector,
                host.LaunchContext.ApplicationGeneration);
            host.TryLog(cleared
                ? "C150-LIFETIME-CLOSE result=PASS"u8
                : "C150-LIFETIME-CLOSE result=REJECT stale-generation=true"u8);
            return cleared ? (int)GuideXosResult.Success :
                GxAbi.ErrorInvalidApplicationId;
        }
#endif

#if HOSTLOGPROOF_C147_RUNTIME_SETTINGS
        // The shared loader runs before the first managed application launch
        // or input callback; Settings Center is only one consumer of state.
        if (!host.IsCapabilityProbe)
        {
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
            if (traceSettingsLaunch)
                LogC188R12R2SettingsLaunch(host, "runtime-settings-init"u8,
                    "BEGIN"u8, launchGeneration);
#endif
            bool runtimeSettingsReady =
                Applications.GuideXosRuntimeSettings.EnsureInitialized(host);
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
            if (traceSettingsLaunch)
                LogC188R12R2SettingsLaunch(host, "runtime-settings-init"u8,
                    runtimeSettingsReady ? "PASS"u8 : "FAIL"u8,
                    launchGeneration,
                    runtimeSettingsReady ? 0 : GxAbi.ErrorInvalidArgument);
#endif
            if (!runtimeSettingsReady)
            {
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
                if (traceSettingsLaunch)
                    LogC188R12R2SettingsLaunch(host, "dispatch-return"u8,
                        "FAIL"u8, launchGeneration,
                        GxAbi.ErrorInvalidArgument);
#endif
                return GxAbi.ErrorInvalidArgument;
            }
        }
#endif

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
#if HOSTLOGPROOF_C150_MANAGED_APP_RETURN
        if (!s_lifecycleTestsRun)
        {
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
            if (traceSettingsLaunch)
                LogC188R12R2SettingsLaunch(host, "managed-lifecycle-tests"u8,
                    "BEGIN"u8, launchGeneration);
#endif
            s_lifecycleTestsRun = true;
            bool lifecycleTests = GuideXosManagedApplicationC150Tests.Run();
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
            if (traceSettingsLaunch)
                LogC188R12R2SettingsLaunch(host, "managed-lifecycle-tests"u8,
                    lifecycleTests ? "PASS"u8 : "FAIL"u8,
                    launchGeneration,
                    lifecycleTests ? 0 : GxAbi.ErrorInvalidArgument);
#endif
            host.TryLog(lifecycleTests
                ? "C150-MANAGED-LIFECYCLE-TESTS cases=8 coexist=true capacity=5 result=PASS"u8
                : "C150-MANAGED-LIFECYCLE-TESTS result=FAIL"u8);
            if (!lifecycleTests)
            {
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
                if (traceSettingsLaunch)
                    LogC188R12R2SettingsLaunch(host, "dispatch-return"u8,
                        "FAIL"u8, launchGeneration,
                        GxAbi.ErrorInvalidArgument);
#endif
                return GxAbi.ErrorInvalidArgument;
            }
        }
#endif

#if HOSTLOGPROOF_MANAGED_APP_RETURN
        GuideXosApplication application;
        bool isNewLaunch = !host.LaunchContext.IsInput && !host.IsAction;
        uint applicationGeneration = host.LaunchContext.ApplicationGeneration;
        if (isNewLaunch)
        {
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
            if (traceSettingsLaunch)
                LogC188R12R2SettingsLaunch(host, "lifetime-start"u8,
                    "BEGIN"u8, launchGeneration);
#endif
#if HOSTLOGPROOF_C188R10_MANAGED_STATE_REENTRY
            bool replacingResident = s_lifetime.TryGet(selector, out _);
#endif
#if HOSTLOGPROOF_C188R12R3_LIFETIME
            bool lifetimeStarted = s_lifetime.TryStartDiagnostic(descriptor,
                applicationGeneration, out application, out uint generation,
                host, applicationGeneration,
                C188R12R3LifetimeDiagnostics.IdentityFor(selector,
                    descriptor.Name));
#else
            bool lifetimeStarted = s_lifetime.TryStart(descriptor,
                applicationGeneration, out application,
                out uint generation);
#endif
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
            if (traceSettingsLaunch)
                LogC188R12R2SettingsLaunch(host, "lifetime-start"u8,
                    lifetimeStarted ? "PASS"u8 : "FAIL"u8,
                    launchGeneration,
                    lifetimeStarted ? 0 : GxAbi.ErrorInvalidApplicationId);
#endif
            if (!lifetimeStarted)
            {
                host.TryLog("C150-LIFETIME-START result=FAIL capacity=5"u8);
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
                if (traceSettingsLaunch)
                    LogC188R12R2SettingsLaunch(host, "dispatch-return"u8,
                        "FAIL"u8, launchGeneration,
                        GxAbi.ErrorInvalidApplicationId);
#endif
                return GxAbi.ErrorInvalidApplicationId;
            }
#if HOSTLOGPROOF_C188R10_MANAGED_STATE_REENTRY
            if (replacingResident)
                s_c188r10TeardownCalls++;
#endif
            applicationGeneration = generation;
#if HOSTLOGPROOF_C188R9_REGISTRY_INPUT_REENTRY
            if (selector == 4u)
            {
                LogC188R9RegistryState(host, "instance-start"u8, selector,
                    applicationGeneration, 0);
            }
#endif
#if HOSTLOGPROOF_C188R10_MANAGED_STATE_REENTRY
            LogC188R10State(host, "start"u8, selector,
                applicationGeneration, 0);
#endif
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
            LogC188R11State(host, "instance-start"u8, selector,
                applicationGeneration, 0);
#if HOSTLOGPROOF_C188R10_MANAGED_STATE_REENTRY
            if (replacingResident)
                LogC188R11State(host, "resident-replacement-teardown"u8,
                    selector, applicationGeneration, 0);
#endif
#endif
#if HOSTLOGPROOF_C150_MANAGED_APP_RETURN
            Span<byte> instanceMarker = stackalloc byte[96];
            int markerLength = 0;
            GuideXosText.Append(instanceMarker, ref markerLength,
                "C150-APP-INSTANCE id="u8);
            GuideXosText.AppendUnsigned(instanceMarker, ref markerLength, selector);
            GuideXosText.Append(instanceMarker, ref markerLength,
                " generation="u8);
            GuideXosText.AppendUnsigned(instanceMarker, ref markerLength, generation);
            host.TryLog(instanceMarker[..markerLength]);
#endif
        }
        else
        {
            bool residentFound = s_lifetime.TryGet(selector,
                applicationGeneration, out application);
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
            LogC188R11LifetimeLookup(host, selector,
                applicationGeneration, residentFound);
#endif
            if (residentFound)
            {
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
                LogC188R11State(host, "resident-found"u8, selector,
                    applicationGeneration, 0);
#endif
#if HOSTLOGPROOF_C188R8_NOTES_INPUT_ROUTING
                if (host.LaunchContext.IsInput)
                    LogC188R8InputBoundary(host, "lifetime-found"u8, selector, 0);
#endif
            }
            else
            {
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
                LogC188R11State(host, "resident-miss"u8, selector,
                    applicationGeneration, GxAbi.ErrorInvalidApplicationId);
#endif
#if HOSTLOGPROOF_C188R8_NOTES_INPUT_ROUTING
                if (host.LaunchContext.IsInput)
                    LogC188R8InputBoundary(host, "lifetime-rejected"u8, selector,
                        GxAbi.ErrorInvalidApplicationId);
#endif
#if HOSTLOGPROOF_C150_MANAGED_APP_RETURN
                host.TryLog("C150-DISPATCH rejected=stale-application result=FAIL"u8);
#endif
                return GxAbi.ErrorInvalidApplicationId;
            }
        }

#if HOSTLOGPROOF_C188R3_NOTES_STARTUP
        GuideXosResult result;
        if (host.LaunchContext.IsInput)
        {
#if HOSTLOGPROOF_C188R8_NOTES_INPUT_ROUTING
            LogC188R8InputBoundary(host, "notes-handler-enter"u8, selector, 0);
#endif
#if HOSTLOGPROOF_C188R10_MANAGED_STATE_REENTRY
            LogC188R10State(host, "handler-enter"u8, selector,
                applicationGeneration, -1);
#endif
            result = application.HandleInput(host, input);
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
            LogC188R11R2Keyboard(host, "managed-callback-return"u8,
                (int)result);
#endif
#if HOSTLOGPROOF_C188R10_MANAGED_STATE_REENTRY
            LogC188R10State(host, "input-return"u8, selector,
                applicationGeneration, (int)result);
#endif
#if HOSTLOGPROOF_C188R8_NOTES_INPUT_ROUTING
            LogC188R8InputBoundary(host, "notes-handler-return"u8, selector,
                (int)result);
#endif
        }
        else if (host.IsAction)
        {
            result = GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                application, host, host.LaunchContext.ActionId);
        }
        else
        {
            host.TryLog("C188R3-010 APPLICATION-LAUNCH-BEGIN"u8);
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
            if (selector == 5u &&
                s_c188r12r1SettingsLaunchEntryTraceRecords < 8u)
            {
                ++s_c188r12r1SettingsLaunchEntryTraceRecords;
                host.TryLog("C188R12R1-MANAGED stage=application-launch-entry selector=5 result=BEGIN"u8);
            }
#endif
#if HOSTLOGPROOF_C188R6_NOTES_POSTRENDER
            host.TryLog("C188R6-010 BEFORE APPLICATION-LAUNCH-CALL"u8);
#endif
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
            if (selector == 4u)
                LogC188R11State(host, "launch-entry"u8, selector,
                    applicationGeneration, -1);
            if (traceSettingsLaunch)
                LogC188R12R2SettingsLaunch(host, "application-entry"u8,
                    "BEGIN"u8, launchGeneration);
#endif
            result = application.Launch(host);
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
            if (traceSettingsLaunch)
                LogC188R12R2SettingsLaunch(host, "application-return"u8,
                    result == GuideXosResult.Success ? "PASS"u8 : "FAIL"u8,
                    launchGeneration, (int)result);
            if (selector == 5u &&
                s_c188r12r1SettingsLaunchReturnTraceRecords < 8u)
            {
                ++s_c188r12r1SettingsLaunchReturnTraceRecords;
                host.TryLog(result == GuideXosResult.Success
                    ? "C188R12R1-MANAGED stage=application-launch-return selector=5 result=PASS"u8
                    : "C188R12R1-MANAGED stage=application-launch-return selector=5 result=FAIL"u8);
                LogC188R11State(host, "settings-launch-return"u8, selector,
                    applicationGeneration, (int)result);
            }
#endif
#if HOSTLOGPROOF_C188R9_REGISTRY_INPUT_REENTRY
            if (selector == 4u)
            {
                LogC188R9RegistryState(host, "launch-return"u8, selector,
                    applicationGeneration, (int)result);
            }
#endif
#if HOSTLOGPROOF_C188R10_MANAGED_STATE_REENTRY
            LogC188R10State(host, "launch-return"u8, selector,
                applicationGeneration, (int)result);
#endif
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
            LogC188R11State(host, "launch-return"u8, selector,
                applicationGeneration, (int)result);
#endif
#if HOSTLOGPROOF_C188R6_NOTES_POSTRENDER
            host.TryLog("C188R6-011 AFTER APPLICATION-LAUNCH-RETURN"u8);
#endif
            host.TryLog("C188R3-011 APPLICATION-LAUNCH-RETURN"u8);
#if HOSTLOGPROOF_C188R6_NOTES_POSTRENDER
            host.TryLog("C188R6-012 BEFORE APPLICATION-RESULT-LOG"u8);
#endif
            host.TryLog(result == GuideXosResult.Success
                ? "C188R3-012 APPLICATION-RESULT-SUCCESS"u8
                : "C188R3-013 APPLICATION-RESULT-FAILURE"u8);
#if HOSTLOGPROOF_C188R6_NOTES_POSTRENDER
            host.TryLog("C188R6-013 AFTER APPLICATION-RESULT-LOG"u8);
#endif
        }
#else
#if HOSTLOGPROOF_C188R10_MANAGED_STATE_REENTRY
        GuideXosResult result;
        if (host.LaunchContext.IsInput)
        {
            LogC188R10State(host, "handler-enter"u8, selector,
                applicationGeneration, -1);
            result = application.HandleInput(host, input);
            LogC188R10State(host, "input-return"u8, selector,
                applicationGeneration, (int)result);
        }
        else if (host.IsAction)
        {
            result = GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                application, host, host.LaunchContext.ActionId);
        }
        else
        {
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
            if (traceSettingsLaunch)
                LogC188R12R2SettingsLaunch(host, "application-entry"u8,
                    "BEGIN"u8, launchGeneration);
#endif
            result = application.Launch(host);
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
            if (traceSettingsLaunch)
                LogC188R12R2SettingsLaunch(host, "application-return"u8,
                    result == GuideXosResult.Success ? "PASS"u8 : "FAIL"u8,
                    launchGeneration, (int)result);
#endif
            LogC188R10State(host, "launch-return"u8, selector,
                applicationGeneration, (int)result);
        }
#else
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
        if (traceSettingsLaunch)
            LogC188R12R2SettingsLaunch(host, "application-entry"u8,
                "BEGIN"u8, launchGeneration);
#endif
        GuideXosResult result = host.LaunchContext.IsInput
            ? application.HandleInput(host, input)
            : host.IsAction
                ? GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                    application, host, host.LaunchContext.ActionId)
                : application.Launch(host);
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
        if (traceSettingsLaunch)
            LogC188R12R2SettingsLaunch(host, "application-return"u8,
                result == GuideXosResult.Success ? "PASS"u8 : "FAIL"u8,
                launchGeneration, (int)result);
#endif
#endif
#endif
#if HOSTLOGPROOF_C188R7_NOTES_PROOF_AUTHORIZATION
        if (!host.LaunchContext.IsInput && !host.IsAction)
            host.TryLog("C188R7-208 AFTER APPLICATION-LAUNCH-RETURN"u8);
#endif
#if HOSTLOGPROOF_C160_APPLICATION_SNAPSHOT_PROOF
        if (result == GuideXosResult.Success && isNewLaunch &&
            !host.IsCapabilityProbe && !host.IsAbiProbe)
        {
            if (!GuideXosApplicationSnapshotC160Tests.Run(host, selector) ||
                !GuideXosApplicationSnapshotC160Tests.VerifyProductionLaunch(
                    host, selector, out _, out _))
            {
                host.TryLog("C160-SNAPSHOT result=FAIL"u8);
                return GxAbi.ErrorInvalidArgument;
            }
        }
        else if (result == GuideXosResult.Success &&
            (host.LaunchContext.IsInput || host.IsAction) &&
            !host.IsCapabilityProbe && !host.IsAbiProbe)
        {
            bool fileExplorerClosed = false;
            bool verified = false;
#if HOSTLOGPROOF_C163_MANAGED_FILE_EXPLORER
            if (selector == Applications.ManagedFileExplorerC163.ApplicationSelector &&
                application is Applications.ManagedFileExplorerC163 fileExplorer &&
                fileExplorer.CloseRequested)
            {
                verified = GuideXosApplicationSnapshotC160Tests.
                    VerifyProductionClose(host, selector);
                fileExplorerClosed = verified;
            }
            else
#endif
            {
                verified = GuideXosApplicationSnapshotC160Tests.
                    VerifyProductionDispatch(host, selector);
            }
            if (!verified)
            {
                host.TryLog("C160-SNAPSHOT result=FAIL"u8);
                return GxAbi.ErrorInvalidArgument;
            }
            if (fileExplorerClosed &&
                !ClearResident(host, selector, applicationGeneration))
            {
                host.TryLog("C150-LIFETIME-CLOSE result=REJECT stale-generation=true"u8);
                return GxAbi.ErrorInvalidApplicationId;
            }
        }
#endif
#if HOSTLOGPROOF_C158_MANAGED_CALCULATOR
        if (selector == 6u && host.IsAction &&
            host.LaunchContext.ActionId ==
                Applications.ManagedCalculatorC158.CloseActionId &&
            result == GuideXosResult.Success)
        {
            if (!ClearResident(host, selector, applicationGeneration))
                return GxAbi.ErrorInvalidApplicationId;
        }
#endif
#if HOSTLOGPROOF_C161_MANAGED_TASK_MANAGER
        if (selector == Applications.ManagedTaskManagerC161.ApplicationSelector &&
            host.IsAction && host.LaunchContext.ActionId ==
                Applications.ManagedTaskManagerC161.CloseActionId &&
            result == GuideXosResult.Success)
        {
            if (!ClearResident(host, selector, applicationGeneration))
                return GxAbi.ErrorInvalidApplicationId;
        }
#endif
        if (isNewLaunch && result != GuideXosResult.Success)
        {
            _ = ClearResident(host, selector, applicationGeneration);
        }
#else
        GuideXosResult result = host.LaunchContext.IsInput
            ? descriptor.Application.HandleInput(host, input)
            : host.IsAction
                ? GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                    descriptor.Application, host,
                    host.LaunchContext.ActionId)
                : descriptor.Application.Launch(host);
#endif
#if HOSTLOGPROOF_C188R10_MANAGED_STATE_REENTRY
        LogC188R10State(host, "return"u8, selector,
            applicationGeneration, (int)result);
#endif
#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
        LogC188R11State(host, "managed-return"u8, selector,
            applicationGeneration, (int)result);
        if (traceSettingsLaunch)
            LogC188R12R2SettingsLaunch(host, "dispatch-return"u8,
                result == GuideXosResult.Success ? "PASS"u8 : "FAIL"u8,
                launchGeneration, (int)result);
#endif
        return (int)result;
    }

#if HOSTLOGPROOF_C188R10_MANAGED_STATE_REENTRY
    private static void LogC188R10State(
        GuideXosHost host,
        ReadOnlySpan<byte> stage,
        uint selector,
        uint generation,
        int resultCode)
    {
        if (selector != 4u || s_c188r10RegistryTraceRecords >= 48u ||
            (host.LaunchContext.IsInput &&
             (host.LaunchContext.InputKind == GuideXosInputKind.PointerMove ||
              host.LaunchContext.InputKind == GuideXosInputKind.Wheel)))
        {
            return;
        }
        s_c188r10RegistryTraceRecords++;

        uint residentMask = 0u;
        for (uint index = 0u; index < 5u; index++)
        {
            if (s_lifetime.TryGet(index + 4u, out _))
                residentMask |= 1u << (int)index;
        }
        bool generationValid = s_lifetime.TryGet(selector, generation, out _);

        Span<byte> line = stackalloc byte[127];
        int position = 0;
        if (!GuideXosText.Append(line, ref position,
                "C188R10 state stage="u8) ||
            !GuideXosText.Append(line, ref position, stage) ||
            !GuideXosText.Append(line, ref position, " reg="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position,
                C188R10RegistryOwnerIdentity) ||
            !GuideXosText.Append(line, ref position, " init="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position,
                s_c188r10RegistryInitializationGeneration) ||
            !GuideXosText.Append(line, ref position, " n="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position,
                (uint)s_entries.Length) ||
            !GuideXosText.Append(line, ref position, " pool="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position,
                C188R10LifetimeOwnerIdentity) ||
            !GuideXosText.Append(line, ref position, " mask="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position, residentMask) ||
            !GuideXosText.Append(line, ref position, " id=4 gen="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position, generation) ||
            !GuideXosText.Append(line, ref position, " valid="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position,
                generationValid ? 1u : 0u) ||
            !GuideXosText.Append(line, ref position, " td="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position,
                s_c188r10TeardownCalls) ||
            !GuideXosText.Append(line, ref position, " rc="u8) ||
            !AppendC188R10Signed(line, ref position, resultCode))
        {
            return;
        }
        host.TryLog(line[..position]);
    }

    private static bool AppendC188R10Signed(
        Span<byte> buffer, ref int position, int value)
    {
        if (value < 0)
        {
            if (!GuideXosText.Append(buffer, ref position, "-"u8)) return false;
            return GuideXosText.AppendUnsigned(buffer, ref position,
                unchecked((uint)(-(long)value)));
        }
        return GuideXosText.AppendUnsigned(buffer, ref position, (uint)value);
    }
#endif

#if HOSTLOGPROOF_C188R8_NOTES_INPUT_ROUTING
    private static void LogC188R8InputBoundary(
        GuideXosHost host,
        ReadOnlySpan<byte> stage,
        uint selector,
        int resultCode)
    {
        GuideXosInputKind kind = host.LaunchContext.InputKind;
        if (kind == GuideXosInputKind.PointerMove ||
            kind == GuideXosInputKind.Wheel ||
            s_c188r8InputTraceRecords >= 16u)
        {
            return;
        }
        s_c188r8InputTraceRecords++;

        Span<byte> line = stackalloc byte[124];
        int position = 0;
        if (!GuideXosText.Append(line, ref position,
                "C188R8-300 INPUT-BOUNDARY stage="u8) ||
            !GuideXosText.Append(line, ref position, stage) ||
            !GuideXosText.Append(line, ref position, " selector="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position, selector) ||
            !GuideXosText.Append(line, ref position, " generation="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position,
                host.LaunchContext.ApplicationGeneration) ||
            !GuideXosText.Append(line, ref position, " kind="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position, (uint)kind) ||
            !GuideXosText.Append(line, ref position, " flags="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position,
                host.LaunchContext.Flags) ||
            !GuideXosText.Append(line, ref position, " x="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position,
                (uint)host.LaunchContext.InputX) ||
            !GuideXosText.Append(line, ref position, " y="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position,
                (uint)host.LaunchContext.InputY) ||
            !GuideXosText.Append(line, ref position, " result="u8) ||
            !AppendSigned(line, ref position, resultCode))
        {
            return;
        }
        host.TryLog(line[..position]);
    }

    private static bool AppendSigned(
        Span<byte> buffer, ref int position, int value)
    {
        if (value < 0)
        {
            if (!GuideXosText.Append(buffer, ref position, "-"u8)) return false;
            return GuideXosText.AppendUnsigned(buffer, ref position,
                unchecked((uint)(-(long)value)));
        }
        return GuideXosText.AppendUnsigned(buffer, ref position, (uint)value);
    }
#endif

#if HOSTLOGPROOF_C188R9_REGISTRY_INPUT_REENTRY
    private static void LogC188R9RegistryState(
        GuideXosHost host,
        ReadOnlySpan<byte> stage,
        uint selector,
        uint generation,
        int resultCode)
    {
        if (s_c188r9RegistryTraceRecords >= 18u ||
            (host.LaunchContext.IsInput &&
             (host.LaunchContext.InputKind == GuideXosInputKind.PointerMove ||
              host.LaunchContext.InputKind == GuideXosInputKind.Wheel)))
        {
            return;
        }
        s_c188r9RegistryTraceRecords++;

        bool live = s_lifetime.TryGet(selector, generation, out _);
        // GuideXosHost.TryLog accepts at most 127 bytes. Keep the populated
        // five-slot snapshot inside that limit so launch and input snapshots
        // are equally observable.
        Span<byte> line = stackalloc byte[127];
        int position = 0;
        if (!GuideXosText.Append(line, ref position,
                "C188R9-300 REG owner=static stage="u8) ||
            !GuideXosText.Append(line, ref position, stage) ||
            !GuideXosText.Append(line, ref position, " selector="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position, selector) ||
            !GuideXosText.Append(line, ref position, " gen="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position, generation) ||
            !GuideXosText.Append(line, ref position, " input="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position,
                host.LaunchContext.IsInput ? 1u : 0u) ||
            !GuideXosText.Append(line, ref position, " count="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position,
                (uint)s_entries.Length) ||
            !GuideXosText.Append(line, ref position, " slots="u8))
        {
            return;
        }
        for (int index = 0; index < s_entries.Length; index++)
        {
            if (index != 0 && !GuideXosText.Append(line, ref position, ","u8))
                return;
            if (!GuideXosText.AppendUnsigned(line, ref position,
                    s_entries[index].Selector))
            {
                return;
            }
        }
        if (!GuideXosText.Append(line, ref position, " live="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position, live ? 1u : 0u) ||
            !GuideXosText.Append(line, ref position, " result="u8) ||
            !AppendC188R9Signed(line, ref position, resultCode))
        {
            return;
        }
        host.TryLog(line[..position]);
    }

    private static bool AppendC188R9Signed(
        Span<byte> buffer, ref int position, int value)
    {
        if (value < 0)
        {
            if (!GuideXosText.Append(buffer, ref position, "-"u8)) return false;
            return GuideXosText.AppendUnsigned(buffer, ref position,
                unchecked((uint)(-(long)value)));
        }
        return GuideXosText.AppendUnsigned(buffer, ref position, (uint)value);
    }
#endif
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
