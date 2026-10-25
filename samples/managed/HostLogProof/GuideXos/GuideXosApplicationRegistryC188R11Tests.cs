#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE
using System;
using System.Runtime.InteropServices;

namespace HostLogProof;

public static unsafe class GuideXosApplicationRegistryC188R11Tests
{
    public const int CaseCount = 13;
    public static int FirstFailedCase { get; private set; }
    private static int s_notesHandlerEntries;
    private static ulong s_nextWindow = 0xC1881100u;

    public static bool Run()
    {
        FirstFailedCase = 0;
        s_notesHandlerEntries = 0;
        int cases = 0;
        bool passed = true;

        C188R11SequenceCounter boundedSequence = new(3u);
        uint sequence1 = boundedSequence.Next();
        uint sequence2 = boundedSequence.Next();
        uint sequence3 = boundedSequence.Next();
        bool sequenceIsMonotonicAndBounded = sequence1 == 1u &&
            sequence2 == sequence1 + 1u &&
            sequence3 == sequence2 + 1u &&
            boundedSequence.Next() == 0u && boundedSequence.Next() == 0u;
        passed &= Case(ref cases, sequenceIsMonotonicAndBounded);

        object original = new();
        C188R11ObjectIdentityTracker repeatedTracker = new();
        uint originalIdentity = repeatedTracker.Observe(original);
        passed &= Case(ref cases,
            originalIdentity != 0u &&
            repeatedTracker.Observe(original) == originalIdentity &&
            repeatedTracker.Capture(original).ReplacementEvents == 0u);

        object replacement = new();
        uint replacementIdentity = repeatedTracker.Observe(replacement);
        passed &= Case(ref cases, replacementIdentity != originalIdentity &&
            repeatedTracker.Capture(replacement).ReplacementEvents == 1u);

        C188R11ObjectIdentityTracker initializationTracker = new();
        object initializedStorage = new();
        initializationTracker.RecordInitialization(initializedStorage);
        uint beforeReinitialization = initializationTracker.Observe(
            initializedStorage);
        initializationTracker.RecordInitialization(initializedStorage);
        C188R11ObjectIdentitySnapshot afterReinitialization =
            initializationTracker.Capture(initializedStorage);
        passed &= Case(ref cases,
            afterReinitialization.Identity == beforeReinitialization &&
            afterReinitialization.InitializationEpoch == 2u &&
            afterReinitialization.ReplacementEvents == 0u);

        initializationTracker.RecordClear(initializedStorage);
        C188R11ObjectIdentitySnapshot afterClear =
            initializationTracker.Capture(initializedStorage);
        passed &= Case(ref cases,
            afterClear.Identity == beforeReinitialization &&
            afterClear.InitializationEpoch == 2u &&
            afterClear.ClearEvents == 1u &&
            afterClear.ReplacementEvents == 0u);

        C188R11ProvenanceSnapshot initial =
            GuideXosApplicationRegistry.C188R11CaptureState();
        C188R11ProvenanceSnapshot repeated =
            GuideXosApplicationRegistry.C188R11CaptureState();
        passed &= Case(ref cases,
            initial.RegistryIdentity.Identity != 0u &&
            initial.PoolIdentity.Identity != 0u &&
            initial.PoolStorageIdentity.Identity != 0u &&
            initial.RegistryIdentity.Identity ==
                repeated.RegistryIdentity.Identity &&
            initial.PoolIdentity.Identity == repeated.PoolIdentity.Identity &&
            initial.PoolStorageIdentity.Identity ==
                repeated.PoolStorageIdentity.Identity);

        bool allDescriptors = initial.DescriptorCount == 5u;
        for (uint selector = 4u; selector <= 8u; selector++)
        {
            allDescriptors &= GuideXosApplicationRegistry.TryFind(selector,
                out GuideXosApplicationDescriptor descriptor) &&
                descriptor.Selector == selector;
        }
        passed &= Case(ref cases, allDescriptors);

        NativeHostCallTable table = CreateHostTable();
        NativeGxAppContext context = CreateContext(&table, 4u, 1u, 0u);
        int launchResult = Program.DispatchC188ExternalAppIdentity(&context);
        C188R11ProvenanceSnapshot afterLaunch =
            GuideXosApplicationRegistry.C188R11CaptureState();
        bool launchResidentFound =
            GuideXosApplicationRegistry.C188R11TryGetResident(4u, 1u,
                out GuideXosApplication notes);
        bool launchStateRetained = launchResult == (int)GuideXosResult.Success &&
            afterLaunch.DescriptorCount == 5u &&
            GuideXosApplicationRegistry.TryFind(4u, out _) &&
            afterLaunch.ResidentMask == 1u &&
            afterLaunch.NotesOwnerGeneration == 1u &&
            afterLaunch.GenerationHighWatermark >= 1u &&
            launchResidentFound &&
            notes is Applications.ManagedNotes;
        if (!launchStateRetained)
        {
            Console.WriteLine($"C188R11-DEBUG launch rc={launchResult} descriptors={afterLaunch.DescriptorCount} notesDescriptor={GuideXosApplicationRegistry.TryFind(4u, out _)} mask={afterLaunch.ResidentMask} generation={afterLaunch.GenerationHighWatermark} resident={launchResidentFound} type={notes?.GetType().FullName}");
        }
        passed &= Case(ref cases, launchStateRetained);

        GuideXosApplication notesBeforeSnapshot = notes;
        C188R11ProvenanceSnapshot snapshotBefore =
            GuideXosApplicationRegistry.C188R11CaptureState();
        C188R11ProvenanceSnapshot snapshotAfter =
            GuideXosApplicationRegistry.C188R11CaptureState();
        bool snapshotsReadOnly =
            GuideXosApplicationRegistry.C188R11TryGetResident(4u, 1u,
                out GuideXosApplication notesAfterSnapshot) &&
            ReferenceEquals(notesBeforeSnapshot, notesAfterSnapshot) &&
            snapshotBefore.ResidentMask == snapshotAfter.ResidentMask &&
            snapshotBefore.GenerationHighWatermark ==
                snapshotAfter.GenerationHighWatermark &&
            snapshotBefore.NotesOwnerGeneration ==
                snapshotAfter.NotesOwnerGeneration &&
            snapshotBefore.PoolClearEvents == snapshotAfter.PoolClearEvents &&
            snapshotBefore.TeardownEvents == snapshotAfter.TeardownEvents &&
            snapshotBefore.RegistryIdentity.Identity ==
                snapshotAfter.RegistryIdentity.Identity &&
            snapshotBefore.PoolIdentity.Identity ==
                snapshotAfter.PoolIdentity.Identity;
        passed &= Case(ref cases, snapshotsReadOnly);

        NativeGxAppContext staleContext = CreateContext(&table, 4u, 2u,
            GxAbi.LaunchFlagInput | GxAbi.LaunchFlagInputKeyChar |
                (uint)'s');
        int handlerCountBeforeStale = s_notesHandlerEntries;
        int staleResult = Program.DispatchC188ExternalAppIdentity(
            &staleContext);
        C188R11ProvenanceSnapshot afterStale =
            GuideXosApplicationRegistry.C188R11CaptureState();
        bool staleCallbackReadOnly = staleResult ==
                GxAbi.ErrorInvalidApplicationId &&
            s_notesHandlerEntries == handlerCountBeforeStale &&
            afterStale.ResidentMask == snapshotAfter.ResidentMask &&
            afterStale.GenerationHighWatermark ==
                snapshotAfter.GenerationHighWatermark &&
            afterStale.PoolClearEvents == snapshotAfter.PoolClearEvents &&
            afterStale.TeardownEvents == snapshotAfter.TeardownEvents &&
            afterStale.RegistryIdentity.Identity ==
                snapshotAfter.RegistryIdentity.Identity &&
            afterStale.PoolIdentity.Identity ==
                snapshotAfter.PoolIdentity.Identity &&
            GuideXosApplicationRegistry.TryFind(4u, out _);
        passed &= Case(ref cases, staleCallbackReadOnly);

        NativeGxAppContext crossSelectorContext = CreateContext(&table, 5u,
            1u, GxAbi.LaunchFlagInput | GxAbi.LaunchFlagInputKeyChar |
                (uint)'x');
        int handlerCountBeforeCrossSelector = s_notesHandlerEntries;
        bool sameNotesFound = GuideXosApplicationRegistry
            .C188R11TryGetResident(4u, 1u, out GuideXosApplication sameNotes);
        bool noCrossSelectorDelivery =
            Program.DispatchC188ExternalAppIdentity(&crossSelectorContext) ==
                GxAbi.ErrorInvalidApplicationId &&
            s_notesHandlerEntries == handlerCountBeforeCrossSelector &&
            sameNotesFound &&
            ReferenceEquals(notes, sameNotes);
        passed &= Case(ref cases, noCrossSelectorDelivery);

        NativeGxAppContext closeContext = CreateContext(&table, 4u, 1u,
            GxAbi.LaunchFlagAction |
                GuideXosApplicationRegistry.LifetimeCloseActionId);
        int closeResult = Program.DispatchC188ExternalAppIdentity(&closeContext);
        C188R11ProvenanceSnapshot afterClose =
            GuideXosApplicationRegistry.C188R11CaptureState();
        bool closeIsActualTeardown = closeResult == 0 &&
            afterClose.ResidentMask == 0u &&
            afterClose.NotesOwnerGeneration == 0u &&
            afterClose.PoolClearEvents == snapshotAfter.PoolClearEvents + 1u &&
            afterClose.ResidentReplacementEvents ==
                snapshotAfter.ResidentReplacementEvents &&
            afterClose.TeardownEvents == snapshotAfter.TeardownEvents + 1u &&
            afterClose.PoolIdentity.Identity == snapshotAfter.PoolIdentity.Identity &&
            afterClose.PoolStorageIdentity.Identity ==
                snapshotAfter.PoolStorageIdentity.Identity;
        passed &= Case(ref cases, closeIsActualTeardown);

        NativeGxAppContext reopenContext = CreateContext(&table, 4u, 2u, 0u);
        int reopenResult = Program.DispatchC188ExternalAppIdentity(&reopenContext);
        C188R11ProvenanceSnapshot afterReopen =
            GuideXosApplicationRegistry.C188R11CaptureState();
        bool reopenedNotesFound = GuideXosApplicationRegistry
            .C188R11TryGetResident(4u, 2u,
                out GuideXosApplication reopenedNotes);
        bool reopenedWithNewGeneration = reopenResult ==
                (int)GuideXosResult.Success &&
            afterReopen.ResidentMask == 1u &&
            afterReopen.GenerationHighWatermark == 2u &&
            afterReopen.PoolIdentity.Identity == snapshotAfter.PoolIdentity.Identity &&
            afterReopen.PoolStorageIdentity.Identity ==
                snapshotAfter.PoolStorageIdentity.Identity &&
            reopenedNotesFound &&
            reopenedNotes is Applications.ManagedNotes &&
            !ReferenceEquals(notes, reopenedNotes) &&
            !GuideXosApplicationRegistry.C188R11TryGetResident(4u, 1u, out _);
        passed &= Case(ref cases, reopenedWithNewGeneration);

        if (!passed && FirstFailedCase == 0) FirstFailedCase = cases;
        return passed && cases == CaseCount;
    }

    private static bool Case(ref int count, bool result)
    {
        count++;
        if (!result && FirstFailedCase == 0) FirstFailedCase = count;
        return result;
    }

    private static NativeHostCallTable CreateHostTable()
    {
        NativeHostCallTable table = default;
        table.size = GxAbi.HostCallTableSize;
        table.version = GxAbi.HostAbiVersion;
        table.log = &Log;
        table.requestWindow = &RequestWindow;
        table.drawText = &DrawText;
        table.drawRect = &DrawRect;
        table.addButton = &AddButton;
        table.closeWindow = &CloseWindow;
        table.addActionButton = &AddActionButton;
        table.capabilities = (ulong)(GuideXosCapability.Surface |
            GuideXosCapability.Text | GuideXosCapability.Primitive |
            GuideXosCapability.Action | GuideXosCapability.Close |
            GuideXosCapability.LaunchContext | GuideXosCapability.Log);
        return table;
    }

    private static NativeGxAppContext CreateContext(
        NativeHostCallTable* table, uint selector, uint generation,
        uint launchFlags)
    {
        NativeGxAppContext context = default;
        context.size = GxAbi.AppContextLaunchPrefixSize;
        context.apiVersion = GxAbi.ApiVersion;
        context.host = table;
        context.userData = (void*)(nuint)(((ulong)generation << 32) | selector);
        context.launchFlags = launchFlags;
        return context;
    }

    [UnmanagedCallersOnly]
    private static int Log(NativeGxAppContext* context, byte* message)
    {
        ReadOnlySpan<byte> handler = "C188R11 state seq="u8;
        int index = 0;
        while (index < handler.Length && message[index] == handler[index])
            index++;
        if (index == handler.Length &&
            Contains(message, " st=handler-enter"u8)) s_notesHandlerEntries++;
        return 0;
    }

    private static bool Contains(byte* value, ReadOnlySpan<byte> sought)
    {
        int length = 0;
        while (value[length] != 0) length++;
        for (int start = 0; start <= length - sought.Length; start++)
        {
            int index = 0;
            while (index < sought.Length && value[start + index] == sought[index])
                index++;
            if (index == sought.Length) return true;
        }
        return false;
    }

    [UnmanagedCallersOnly]
    private static int RequestWindow(NativeGxAppContext* context,
        byte* title, int width, int height, ulong* window)
    {
        if (window == null) return -2;
        *window = s_nextWindow++;
        return 0;
    }

    [UnmanagedCallersOnly]
    private static int DrawText(NativeGxAppContext* context, ulong window,
        int x, int y, byte* text) => 0;

    [UnmanagedCallersOnly]
    private static int DrawRect(NativeGxAppContext* context, ulong window,
        int x, int y, int width, int height, uint color) => 0;

    [UnmanagedCallersOnly]
    private static int AddButton(NativeGxAppContext* context, ulong window,
        int x, int y, int width, int height, byte* label, int* widget)
    {
        if (widget == null) return -2;
        *widget = 1;
        return 0;
    }

    [UnmanagedCallersOnly]
    private static int AddActionButton(NativeGxAppContext* context,
        ulong window, int x, int y, int width, int height, byte* label,
        uint action, int* widget)
    {
        if (widget == null) return -2;
        *widget = 1;
        return 0;
    }

    [UnmanagedCallersOnly]
    private static int CloseWindow(NativeGxAppContext* context, ulong window) => 0;
}
#endif
