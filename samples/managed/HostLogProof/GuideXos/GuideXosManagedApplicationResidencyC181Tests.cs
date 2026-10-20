#if HOSTLOGPROOF_C181_MANAGED_APPLICATION_RESIDENCY
using System;
using HostLogProof.Applications;

namespace HostLogProof;

public static class GuideXosManagedApplicationResidencyC181Tests
{
    public const int CaseCount = 24;
    public const int StressCaseCount = 7;
    public const int SlotAllocationReleaseOperations = 1000;
    public const int CrossApplicationResizeRequests = 500;
    public const int CrossApplicationResizeValidationRequests = 500;
    public const int CapacityExhaustionCycles = 250;
    public const int StaleGenerationChecks = 250;
    public const int FullLifecycleSequences = 100;

    private static readonly uint[] Selectors = { 4u, 5u, 6u, 7u, 8u };
    private static readonly GuideXosManagedWindowResizeBoundsC174 ResizeBounds =
        new(100, 80, 300, 220);

    private sealed class ResizeProbeApplication : GuideXosApplication
    {
        public ResizeProbeApplication(uint identity)
        {
            Identity = identity;
        }

        public uint Identity { get; }
        public int Width { get; private set; } = 160;
        public int Height { get; private set; } = 120;
        public int State { get; set; }
        public int ResizeCount { get; private set; }
        public int TeardownCount { get; private set; }

        public override GuideXosResult Launch(GuideXosHost host) =>
            GuideXosResult.Success;

        public override void OnTearingDown() => TeardownCount++;

        internal override bool TryGetManagedResizeGeometryC180(
            out GuideXosManagedWindowResizeBoundsC174 bounds,
            out int outerWidth, out int outerHeight)
        {
            bounds = ResizeBounds;
            outerWidth = Width;
            outerHeight = Height;
            return true;
        }

        internal override GuideXosResult HandleManagedResizeC180(
            GuideXosHost host,
            ref GuideXosManagedWindowResizeTransactionC174 transaction)
        {
            _ = host;
            ResizeCount++;
            bool accepted = transaction.Complete(true,
                GuideXosResult.Success, true);
            if (accepted)
            {
                Width = transaction.Candidate.OuterWidth;
                Height = transaction.Candidate.OuterHeight;
            }
            return transaction.Result;
        }
    }

    public static int FirstFailedCase { get; private set; }
    public static string StressFailureDetail { get; private set; } = string.Empty;
    public static int AllocationReleaseOperationCount { get; private set; }
    public static int CrossApplicationResizeCount { get; private set; }
    public static int ExhaustionRecoveryCount { get; private set; }
    public static int StaleGenerationRejectionCount { get; private set; }
    public static int FullLifecycleCount { get; private set; }
    public static long FiveApplicationConstructorAllocationBytes { get; private set; }
    public static long FourthAndFifthConstructorAllocationBytes { get; private set; }
    public static long ResizeHotPathAllocationBytes { get; private set; }
    public static int GeometryDriftCount { get; private set; }

    public static bool Run(GuideXosHost host)
    {
        _ = host;
        FirstFailedCase = 0;
        FiveApplicationConstructorAllocationBytes = -1;
        FourthAndFifthConstructorAllocationBytes = -1;
        int cases = 0;
        bool passed = true;

        bool registryCount = GuideXosApplicationRegistry.RegistrationCount == 5;
        bool selectorsStable = true;
        bool factoryBacked = true;
        for (int index = 0; index < Selectors.Length; index++)
        {
            bool found = GuideXosApplicationRegistry.TryFind(Selectors[index],
                out GuideXosApplicationDescriptor descriptor);
            registryCount &= found;
            selectorsStable &= found && descriptor.Selector == Selectors[index];
            factoryBacked &= found && descriptor.Factory != null;
        }
        passed &= Case(ref cases, registryCount);
        passed &= Case(ref cases, selectorsStable);
        passed &= Case(ref cases, factoryBacked);
        passed &= Case(ref cases,
            GuideXosManagedApplicationLifetime.Capacity == 5);
        passed &= Case(ref cases,
            GxAbi.ApplicationSnapshotCapacity == 22u &&
            GxAbi.ApplicationSnapshotBufferBytes == 3696u);
        passed &= Case(ref cases,
            GxAbi.HostAbiVersion == 4u && GxAbi.HostCallTableSize == 128u &&
            GxAbi.ApplicationSnapshotOffset == 104u &&
            GxAbi.ApplicationCloseOffset == 112u &&
            GxAbi.AssociationServiceOffset == 120u);

        _ = MeasureRegistryConstruction(5);
        long threeAppBytes = MeasureRegistryConstruction(3);
        FiveApplicationConstructorAllocationBytes =
            MeasureRegistryConstruction(5);
        FourthAndFifthConstructorAllocationBytes =
            FiveApplicationConstructorAllocationBytes - threeAppBytes;
        passed &= Case(ref cases,
            FiveApplicationConstructorAllocationBytes >= 0 &&
            FiveApplicationConstructorAllocationBytes < 1024 * 1024 &&
            FourthAndFifthConstructorAllocationBytes >= 0 &&
            FourthAndFifthConstructorAllocationBytes < 1024 * 1024);

        GuideXosManagedApplicationLifetime threeAppLifetime = new();
        bool existingThreeAppBehavior = true;
        uint previousAutoGeneration = 0u;
        for (int index = 0; index < 3; index++)
        {
            existingThreeAppBehavior &=
                GuideXosApplicationRegistry.TryFind(Selectors[index],
                    out GuideXosApplicationDescriptor descriptor) &&
                threeAppLifetime.TryStart(descriptor,
                    out GuideXosApplication application, out uint generation) &&
                application != null && generation == (uint)(index + 1) &&
                generation > previousAutoGeneration;
            previousAutoGeneration = (uint)(index + 1);
        }
        existingThreeAppBehavior &= threeAppLifetime.TryGet(4u, out _) &&
            threeAppLifetime.TryGet(5u, out _) &&
            threeAppLifetime.TryGet(6u, out _);
        passed &= Case(ref cases, existingThreeAppBehavior);

        threeAppLifetime.Clear(4u);
        threeAppLifetime.Clear(5u);
        threeAppLifetime.Clear(6u);
        bool threeAppCleanup = !threeAppLifetime.TryGet(4u, out _) &&
            !threeAppLifetime.TryGet(5u, out _) &&
            !threeAppLifetime.TryGet(6u, out _);
        passed &= Case(ref cases, threeAppCleanup);

        GuideXosManagedApplicationLifetime lifetime = new();
        GuideXosApplication[] residents = new GuideXosApplication[Selectors.Length];
        uint[] generations = new uint[Selectors.Length];
        bool allStarted = true;
        bool distinct = true;
        bool generationLookup = true;
        for (int index = 0; index < Selectors.Length; index++)
        {
            bool found = GuideXosApplicationRegistry.TryFind(Selectors[index],
                out GuideXosApplicationDescriptor descriptor);
            allStarted &= found && lifetime.TryStart(descriptor,
                (uint)(100 + index), out residents[index], out generations[index]);
            generationLookup &= generations[index] == (uint)(100 + index) &&
                lifetime.TryGet(Selectors[index], generations[index], out _);
            for (int prior = 0; prior < index; prior++)
                distinct &= !ReferenceEquals(residents[index], residents[prior]);
        }
        passed &= Case(ref cases, allStarted);
        passed &= Case(ref cases, distinct);
        passed &= Case(ref cases, generationLookup);
        passed &= Case(ref cases, residents[0] is ManagedNotes &&
            residents[1] is ManagedSettingsCenter &&
            residents[2] is ManagedCalculatorC158 &&
            residents[3] is ManagedTaskManagerC161 &&
            residents[4] is ManagedFileExplorerC163);

        bool threeAndFiveIndependent = lifetime.TryGet(4u,
                generations[0], out GuideXosApplication notes) &&
            lifetime.TryGet(5u, generations[1], out GuideXosApplication settings) &&
            lifetime.TryGet(6u, generations[2], out GuideXosApplication calculator) &&
            lifetime.TryGet(7u, generations[3], out GuideXosApplication taskManager) &&
            lifetime.TryGet(8u, generations[4], out GuideXosApplication explorer) &&
            !ReferenceEquals(notes, settings) && !ReferenceEquals(notes, calculator) &&
            !ReferenceEquals(notes, taskManager) && !ReferenceEquals(notes, explorer) &&
            !ReferenceEquals(settings, calculator) &&
            !ReferenceEquals(settings, taskManager) &&
            !ReferenceEquals(settings, explorer) &&
            !ReferenceEquals(calculator, taskManager) &&
            !ReferenceEquals(calculator, explorer) &&
            !ReferenceEquals(taskManager, explorer);
        passed &= Case(ref cases, threeAndFiveIndependent);

        int extraFactoryCalls = 0;
        GuideXosApplicationDescriptor extra = new(9u,
            "Host-only sixth-slot probe"u8,
            () => { extraFactoryCalls++; return new ResizeProbeApplication(9u); });
        passed &= Case(ref cases,
            !lifetime.TryStart(extra, out _, out _) && extraFactoryCalls == 0);

        bool existingRetained = true;
        bool staleRejected = true;
        for (int index = 0; index < Selectors.Length; index++)
        {
            existingRetained &= lifetime.TryGet(Selectors[index],
                generations[index], out GuideXosApplication current) &&
                ReferenceEquals(residents[index], current);
            staleRejected &= !lifetime.TryGet(Selectors[index],
                generations[index] + 1u, out _) &&
                !lifetime.Clear(Selectors[index], generations[index] + 1u);
        }
        passed &= Case(ref cases, existingRetained);
        passed &= Case(ref cases, staleRejected);

        bool released = lifetime.Clear(5u, generations[1]) &&
            residents[1] is not null && !lifetime.TryGet(5u, generations[1], out _) &&
            !lifetime.Clear(5u, generations[1]);
        passed &= Case(ref cases, released);

        bool survivorsRetained = true;
        for (int index = 0; index < Selectors.Length; index++)
        {
            if (index == 1) continue;
            survivorsRetained &= lifetime.TryGet(Selectors[index],
                generations[index], out GuideXosApplication current) &&
                ReferenceEquals(residents[index], current);
        }
        passed &= Case(ref cases, survivorsRetained);

        bool failedFactorySafe = lifetime.TryStart(new GuideXosApplicationDescriptor(
                5u, "Failed host initialization"u8,
                static () => null), 200u, out _, out _) == false &&
            lifetime.TryGet(4u, generations[0], out _);
        passed &= Case(ref cases, failedFactorySafe);

        bool slotReusable = lifetime.TryStart(extra, 201u,
            out GuideXosApplication replacement, out uint replacementGeneration) &&
            replacementGeneration == 201u &&
            lifetime.TryGet(9u, 201u, out GuideXosApplication currentReplacement) &&
            ReferenceEquals(replacement, currentReplacement) &&
            !lifetime.TryGet(9u, generations[1], out _);
        passed &= Case(ref cases, slotReusable);

        bool untouchedIdentity = true;
        for (int index = 0; index < Selectors.Length; index++)
        {
            if (index == 1) continue;
            untouchedIdentity &= lifetime.TryGet(Selectors[index],
                generations[index], out GuideXosApplication current) &&
                ReferenceEquals(residents[index], current);
        }
        passed &= Case(ref cases, untouchedIdentity);

        bool clearAll = lifetime.Clear(9u, replacementGeneration);
        for (int index = 0; index < Selectors.Length; index++)
        {
            if (index == 1) continue;
            clearAll &= lifetime.Clear(Selectors[index], generations[index]);
            clearAll &= !lifetime.TryGet(Selectors[index], generations[index], out _);
        }
        passed &= Case(ref cases, clearAll);

        GuideXosManagedApplicationLifetime replacementPool = new();
        GuideXosApplicationDescriptor probe = ProbeDescriptor(4u);
        bool initialReplacementStart = replacementPool.TryStart(probe,
            out GuideXosApplication oldOccupant, out uint oldOccupantGeneration);
        bool replacementStart = replacementPool.TryStart(probe,
            out GuideXosApplication newOccupant, out uint newOccupantGeneration);
        bool replacementIsolated = initialReplacementStart && replacementStart &&
            !ReferenceEquals(oldOccupant, newOccupant) &&
            oldOccupant is ResizeProbeApplication oldProbe &&
            oldProbe.TeardownCount == 1 &&
            newOccupantGeneration != oldOccupantGeneration &&
            replacementPool.TryGet(4u, newOccupantGeneration,
                out GuideXosApplication currentOccupant) &&
            ReferenceEquals(newOccupant, currentOccupant) &&
            !replacementPool.TryGet(4u, oldOccupantGeneration, out _) &&
            !replacementPool.Clear(4u, oldOccupantGeneration);
        passed &= Case(ref cases, replacementIsolated);
        _ = replacementPool.Clear(4u, newOccupantGeneration);

        if (!passed && FirstFailedCase == 0) FirstFailedCase = cases;
        return passed && cases == CaseCount;
    }

    public static bool RunStress(GuideXosHost host)
    {
        _ = host;
        FirstFailedCase = 0;
        StressFailureDetail = string.Empty;
        AllocationReleaseOperationCount = 0;
        CrossApplicationResizeCount = 0;
        ExhaustionRecoveryCount = 0;
        StaleGenerationRejectionCount = 0;
        FullLifecycleCount = 0;
        ResizeHotPathAllocationBytes = -1;
        GeometryDriftCount = -1;
        int cases = 0;
        bool passed = true;

        GuideXosManagedApplicationLifetime allocationPool = new();
        bool allocationRelease = true;
        uint previousGeneration = 0u;
        for (int index = 0; index < SlotAllocationReleaseOperations; index++)
        {
            uint selector = Selectors[index % Selectors.Length];
            GuideXosApplicationDescriptor descriptor = ProbeDescriptor(selector);
            allocationRelease &= allocationPool.TryStart(descriptor,
                out GuideXosApplication application, out uint generation) &&
                application != null && generation > previousGeneration &&
                allocationPool.TryGet(selector, generation, out _);
            previousGeneration = generation;
            allocationRelease &= allocationPool.Clear(selector, generation) &&
                !allocationPool.TryGet(selector, generation, out _);
            AllocationReleaseOperationCount++;
        }
        passed &= Case(ref cases, allocationRelease &&
            AllocationReleaseOperationCount == SlotAllocationReleaseOperations);

        GuideXosManagedApplicationLifetime resizePool = new();
        ResizeProbeApplication[] resizeApps = new ResizeProbeApplication[Selectors.Length];
        for (int index = 0; index < Selectors.Length; index++)
        {
            uint selector = Selectors[index];
            _ = resizePool.TryStart(ProbeDescriptor(selector),
                out GuideXosApplication application, out _);
            resizeApps[index] = (ResizeProbeApplication)application;
            resizeApps[index].State = index + 10;
        }
        for (int index = 0; index < resizeApps.Length; index++)
        {
            _ = GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                resizeApps[index], null, Encode(200, 140));
            _ = GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                resizeApps[index], null, Encode(160, 120));
        }
        for (int index = 0; index < 100; index++)
        {
            int targetIndex = index % resizeApps.Length;
            _ = resizePool.TryGet(Selectors[targetIndex], out _);
            _ = GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                resizeApps[targetIndex], null,
                Encode((index & 1) == 0 ? 200 : 210,
                    (index & 1) == 0 ? 140 : 150));
        }
        int[] resizeCountsBefore = new int[resizeApps.Length];
        int[] widthsBefore = new int[resizeApps.Length];
        int[] heightsBefore = new int[resizeApps.Length];
        int[] statesBefore = new int[resizeApps.Length];
        long allocationBefore = GC.GetAllocatedBytesForCurrentThread();
        bool routedAndResized = true;
        for (int index = 0; index < CrossApplicationResizeRequests; index++)
        {
            int targetIndex = index % resizeApps.Length;
            routedAndResized &= resizePool.TryGet(Selectors[targetIndex],
                    out GuideXosApplication routed) &&
                ReferenceEquals(resizeApps[targetIndex], routed) &&
                GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                    routed, null, Encode((index & 1) == 0 ? 200 : 210,
                        (index & 1) == 0 ? 140 : 150)) ==
                    GuideXosResult.Success;
            CrossApplicationResizeCount++;
        }
        ResizeHotPathAllocationBytes =
            GC.GetAllocatedBytesForCurrentThread() - allocationBefore;

        bool resizeIsolation = true;
        for (int index = 0; index < CrossApplicationResizeRequests; index++)
        {
            int targetIndex = index % resizeApps.Length;
            ResizeProbeApplication target = resizeApps[targetIndex];
            for (int appIndex = 0; appIndex < resizeApps.Length; appIndex++)
            {
                resizeCountsBefore[appIndex] = resizeApps[appIndex].ResizeCount;
                widthsBefore[appIndex] = resizeApps[appIndex].Width;
                heightsBefore[appIndex] = resizeApps[appIndex].Height;
                statesBefore[appIndex] = resizeApps[appIndex].State;
            }
            int width = (index & 1) == 0 ? 200 : 210;
            int height = (index & 1) == 0 ? 140 : 150;
            uint action = Encode(width, height);
            resizeIsolation &= resizePool.TryGet(Selectors[targetIndex],
                    out GuideXosApplication routed) &&
                ReferenceEquals(target, routed) &&
                GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                    routed, null, action) == GuideXosResult.Success &&
                target.ResizeCount == resizeCountsBefore[targetIndex] + 1 &&
                target.Width == width && target.Height == height &&
                target.State == statesBefore[targetIndex] && target.Identity ==
                    (uint)(targetIndex + 4);
            for (int other = 0; other < resizeApps.Length; other++)
            {
                if (other != targetIndex)
                    resizeIsolation &=
                        resizeApps[other].ResizeCount == resizeCountsBefore[other] &&
                        resizeApps[other].Width == widthsBefore[other] &&
                        resizeApps[other].Height == heightsBefore[other] &&
                        resizeApps[other].State == statesBefore[other];
            }
            CrossApplicationResizeCount++;
        }
        passed &= Case(ref cases, routedAndResized && resizeIsolation &&
            CrossApplicationResizeCount ==
                CrossApplicationResizeRequests +
                    CrossApplicationResizeValidationRequests);

        GuideXosManagedApplicationLifetime exhaustionPool = new();
        ResizeProbeApplication[] exhaustionResidents = new ResizeProbeApplication[Selectors.Length];
        uint[] exhaustionGenerations = new uint[Selectors.Length];
        bool exhaustionRecovery = true;
        int extraCalls = 0;
        GuideXosApplicationDescriptor sixth = new(9u,
            "Host-only exhaustion probe"u8,
            () => { extraCalls++; return new ResizeProbeApplication(9u); });
        for (int cycle = 0; cycle < CapacityExhaustionCycles; cycle++)
        {
            if (cycle == 0)
            {
                for (int index = 0; index < Selectors.Length; index++)
                {
                    _ = exhaustionPool.TryStart(ProbeDescriptor(Selectors[index]),
                        out GuideXosApplication application,
                        out exhaustionGenerations[index]);
                    exhaustionResidents[index] = (ResizeProbeApplication)application;
                }
            }
            exhaustionRecovery &= !exhaustionPool.TryStart(sixth, out _, out _) &&
                extraCalls == cycle;
            int releasedIndex = cycle % Selectors.Length;
            exhaustionRecovery &= exhaustionPool.Clear(
                Selectors[releasedIndex], exhaustionGenerations[releasedIndex]);
            exhaustionRecovery &= exhaustionPool.TryStart(sixth,
                out GuideXosApplication sixthApp, out uint sixthGeneration) &&
                exhaustionPool.TryGet(9u, sixthGeneration,
                    out GuideXosApplication activeSixth) &&
                ReferenceEquals(sixthApp, activeSixth);
            exhaustionRecovery &= exhaustionPool.Clear(9u, sixthGeneration);
            exhaustionRecovery &= exhaustionPool.TryStart(
                ProbeDescriptor(Selectors[releasedIndex]),
                out GuideXosApplication replacement,
                out exhaustionGenerations[releasedIndex]);
            exhaustionResidents[releasedIndex] =
                (ResizeProbeApplication)replacement;
            for (int index = 0; index < Selectors.Length; index++)
            {
                exhaustionRecovery &= exhaustionPool.TryGet(Selectors[index],
                    exhaustionGenerations[index], out GuideXosApplication current) &&
                    ReferenceEquals(exhaustionResidents[index], current);
            }
            ExhaustionRecoveryCount++;
        }
        passed &= Case(ref cases, exhaustionRecovery &&
            ExhaustionRecoveryCount == CapacityExhaustionCycles);

        GuideXosManagedApplicationLifetime stalePool = new();
        bool staleRejected = true;
        for (int index = 0; index < StaleGenerationChecks; index++)
        {
            uint selector = Selectors[index % Selectors.Length];
            GuideXosApplicationDescriptor descriptor = ProbeDescriptor(selector);
            uint oldGeneration = 0u;
            uint currentGeneration = 0u;
            GuideXosApplication oldApp = null;
            GuideXosApplication currentApp = null;
            staleRejected &= stalePool.TryStart(descriptor,
                out oldApp, out oldGeneration) &&
                stalePool.Clear(selector, oldGeneration) &&
                stalePool.TryStart(descriptor, out currentApp,
                    out currentGeneration) &&
                currentGeneration != oldGeneration &&
                !ReferenceEquals(oldApp, currentApp) &&
                !stalePool.TryGet(selector, oldGeneration, out _) &&
                !stalePool.Clear(selector, oldGeneration) &&
                stalePool.TryGet(selector, currentGeneration, out _);
            _ = stalePool.Clear(selector, currentGeneration);
            StaleGenerationRejectionCount++;
        }
        passed &= Case(ref cases, staleRejected &&
            StaleGenerationRejectionCount == StaleGenerationChecks);

        bool fullLifecycles = true;
        for (int cycle = 0; cycle < FullLifecycleSequences; cycle++)
        {
            GuideXosManagedApplicationLifetime pool = new();
            ResizeProbeApplication[] applications =
                new ResizeProbeApplication[Selectors.Length];
            uint[] generations = new uint[Selectors.Length];
            for (int index = 0; index < Selectors.Length; index++)
            {
                fullLifecycles &= pool.TryStart(ProbeDescriptor(Selectors[index]),
                    out GuideXosApplication app, out generations[index]);
                applications[index] = (ResizeProbeApplication)app;
                applications[index].State = cycle * 5 + index;
            }
            for (int index = 0; index < Selectors.Length; index++)
            {
                int width = 180 + index * 10;
                int height = 120 + index * 10;
                fullLifecycles &= GuideXosManagedWindowResizeDispatchC180.
                    DispatchAction(applications[index], null,
                        Encode(width, height)) == GuideXosResult.Success &&
                    applications[index].Width == width &&
                    applications[index].Height == height &&
                    applications[index].State == cycle * 5 + index;
            }
            for (int index = Selectors.Length - 1; index >= 0; index--)
            {
                fullLifecycles &= pool.Clear(Selectors[index], generations[index]) &&
                    applications[index].TeardownCount == 1 &&
                    !pool.TryGet(Selectors[index], generations[index], out _);
            }
            FullLifecycleCount++;
        }
        passed &= Case(ref cases, fullLifecycles &&
            FullLifecycleCount == FullLifecycleSequences);

        bool noResizeAllocations = ResizeHotPathAllocationBytes == 0;
        passed &= Case(ref cases, noResizeAllocations);
        bool noGeometryDrift = true;
        for (int index = 0; index < resizeApps.Length; index++)
        {
            ResizeProbeApplication app = resizeApps[index];
            int originalWidth = app.Width;
            int originalHeight = app.Height;
            for (int cycle = 0; cycle < 100; cycle++)
            {
                noGeometryDrift &=
                    GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                        app, null, Encode(300, 220)) == GuideXosResult.Success &&
                    GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                        app, null, Encode(originalWidth, originalHeight)) ==
                    GuideXosResult.Success;
            }
            noGeometryDrift &= app.Width == originalWidth &&
                app.Height == originalHeight;
        }
        GeometryDriftCount = noGeometryDrift ? 0 : 1;
        passed &= Case(ref cases, noGeometryDrift && GeometryDriftCount == 0);

        if (!passed && FirstFailedCase == 0)
            StressFailureDetail = "residency-stress";
        return passed && cases == StressCaseCount;
    }

    private static long MeasureRegistryConstruction(int count)
    {
        GuideXosManagedApplicationLifetime pool = new();
        long before = GC.GetAllocatedBytesForCurrentThread();
        for (int index = 0; index < count; index++)
        {
            if (!GuideXosApplicationRegistry.TryFind(Selectors[index],
                    out GuideXosApplicationDescriptor descriptor) ||
                !pool.TryStart(descriptor, (uint)(300 + index), out _, out _))
                return -1;
        }
        return GC.GetAllocatedBytesForCurrentThread() - before;
    }

    private static GuideXosApplicationDescriptor ProbeDescriptor(uint selector) =>
        new(selector, "Host-only managed app"u8,
            () => new ResizeProbeApplication(selector));

    private static uint Encode(int width, int height) =>
        0x08000000u | ((uint)width << 12) | (uint)height;

    private static bool Case(ref int count, bool result)
    {
        count++;
        if (!result && FirstFailedCase == 0) FirstFailedCase = count;
        return result;
    }
}
#endif
