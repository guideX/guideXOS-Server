#if HOSTLOGPROOF_C188R9_REGISTRY_INPUT_REENTRY
using System;

namespace HostLogProof;

public static class GuideXosApplicationRegistryC188R9Tests
{
    private static readonly uint[] ProductionSelectors = { 4u, 5u, 6u, 7u, 8u };

    public const int CaseCount = 14;
    public static int FirstFailedCase { get; private set; }

    public static bool Run()
    {
        FirstFailedCase = 0;
        int cases = 0;
        bool passed = true;

        passed &= Case(ref cases,
            GuideXosApplicationRegistry.RegistrationCount ==
                ProductionSelectors.Length);

        bool allDescriptorsPresent = true;
        for (int index = 0; index < ProductionSelectors.Length; index++)
        {
            uint selector = ProductionSelectors[index];
            allDescriptorsPresent &= GuideXosApplicationRegistry.TryFind(
                selector, out GuideXosApplicationDescriptor descriptor) &&
                descriptor.Selector == selector && descriptor.Factory != null;
        }
        passed &= Case(ref cases, allDescriptorsPresent);

        bool notesDescriptor = GuideXosApplicationRegistry.TryFind(4u,
            out GuideXosApplicationDescriptor notes) &&
            notes.Selector == 4u &&
            notes.Name.AsSpan().SequenceEqual("Managed Notes"u8) &&
            notes.Factory != null;
        passed &= Case(ref cases, notesDescriptor);

        bool zeroMissing = !GuideXosApplicationRegistry.TryFind(0u,
            out GuideXosApplicationDescriptor zero) && zero.Selector == 0u &&
            zero.Application == null && zero.Factory == null;
        passed &= Case(ref cases, zeroMissing);

        bool developmentSelectorMissing =
            !GuideXosApplicationRegistry.TryFind(9u,
                out GuideXosApplicationDescriptor development) &&
            development.Selector == 0u && development.Application == null &&
            development.Factory == null;
        passed &= Case(ref cases, developmentSelectorMissing);

        bool repeatedLookupStable = true;
        for (int repeat = 0; repeat < 8; repeat++)
        {
            repeatedLookupStable &= GuideXosApplicationRegistry.TryFind(4u,
                out GuideXosApplicationDescriptor found) &&
                found.Selector == notes.Selector &&
                ReferenceEquals(found.Name, notes.Name) &&
                ReferenceEquals(found.Factory, notes.Factory);
        }
        passed &= Case(ref cases, repeatedLookupStable &&
            GuideXosApplicationRegistry.RegistrationCount ==
                ProductionSelectors.Length);

        GuideXosManagedApplicationLifetime lifetime = new();
        GuideXosApplication first = null;
        uint firstGeneration = 0u;
        bool started = GuideXosApplicationRegistry.TryFind(4u,
                out GuideXosApplicationDescriptor launchDescriptor) &&
            lifetime.TryStart(launchDescriptor, out first,
                out firstGeneration) && first is Applications.ManagedNotes &&
            firstGeneration != 0u;
        passed &= Case(ref cases, started);

        bool inputReentryResolved = started &&
            GuideXosApplicationRegistry.TryFind(4u,
                out GuideXosApplicationDescriptor inputDescriptor) &&
            inputDescriptor.Selector == launchDescriptor.Selector &&
            lifetime.TryGet(4u, firstGeneration,
                out GuideXosApplication inputApplication) &&
            ReferenceEquals(first, inputApplication);
        passed &= Case(ref cases, inputReentryResolved);

        bool staleGenerationRejected = started &&
            !lifetime.TryGet(4u, firstGeneration + 1u, out _) &&
            !lifetime.Clear(4u, firstGeneration + 1u);
        passed &= Case(ref cases, staleGenerationRejected);

        bool closeRejectedCurrent = started && lifetime.Clear(4u, firstGeneration) &&
            !lifetime.TryGet(4u, firstGeneration, out _) &&
            GuideXosApplicationRegistry.TryFind(4u, out _);
        passed &= Case(ref cases, closeRejectedCurrent);

        GuideXosApplication reopened = null;
        uint reopenedGeneration = 0u;
        bool reopenedWithNewGeneration = closeRejectedCurrent &&
            GuideXosApplicationRegistry.TryFind(4u,
                out GuideXosApplicationDescriptor reopenDescriptor) &&
            lifetime.TryStart(reopenDescriptor, out reopened,
                out reopenedGeneration) &&
            reopenedGeneration > firstGeneration &&
            !ReferenceEquals(first, reopened) &&
            !lifetime.TryGet(4u, firstGeneration, out _) &&
            lifetime.TryGet(4u, reopenedGeneration, out _);
        passed &= Case(ref cases, reopenedWithNewGeneration);

        bool independentLiveIdentity =
            GuideXosApplicationRegistry.TryFind(5u,
                out GuideXosApplicationDescriptor settingsDescriptor) &&
            lifetime.TryStart(settingsDescriptor, out GuideXosApplication settings,
                out uint settingsGeneration) &&
            settings is Applications.ManagedSettingsCenter &&
            settingsGeneration != reopenedGeneration &&
            lifetime.TryGet(4u, reopenedGeneration,
                out GuideXosApplication currentNotes) &&
            lifetime.TryGet(5u, settingsGeneration,
                out GuideXosApplication currentSettings) &&
            !ReferenceEquals(currentNotes, currentSettings) &&
            !lifetime.TryGet(4u, settingsGeneration, out _) &&
            !lifetime.TryGet(5u, reopenedGeneration, out _);
        passed &= Case(ref cases, independentLiveIdentity);

        bool allProductionSelectorsRemainResolvable = true;
        for (int index = 0; index < ProductionSelectors.Length; index++)
        {
            allProductionSelectorsRemainResolvable &=
                GuideXosApplicationRegistry.TryFind(ProductionSelectors[index],
                    out GuideXosApplicationDescriptor descriptor) &&
                descriptor.Selector == ProductionSelectors[index];
        }
        passed &= Case(ref cases, allProductionSelectorsRemainResolvable);

        GuideXosManagedApplicationLifetime fullPool = new();
        GuideXosApplication[] capacityApplications =
            new GuideXosApplication[ProductionSelectors.Length];
        uint[] capacityGenerations = new uint[ProductionSelectors.Length];
        bool capacityFilled = true;
        for (int index = 0; index < ProductionSelectors.Length; index++)
        {
            uint selector = ProductionSelectors[index];
            capacityFilled &= GuideXosApplicationRegistry.TryFind(selector,
                    out GuideXosApplicationDescriptor descriptor) &&
                fullPool.TryStart(descriptor,
                    out capacityApplications[index],
                    out capacityGenerations[index]);
        }
        GuideXosApplicationDescriptor overflowDescriptor = new(
            0xC188C100u, "C188 capacity probe"u8,
            static () => new Applications.ManagedNotes());
        bool capacityExhaustionRejected = capacityFilled &&
            !fullPool.TryStart(overflowDescriptor, out _, out _);
        bool capacityEntriesRemainLive = capacityFilled;
        for (int index = 0; index < ProductionSelectors.Length; index++)
        {
            capacityEntriesRemainLive &= fullPool.TryGet(
                    ProductionSelectors[index], capacityGenerations[index],
                    out GuideXosApplication active) &&
                ReferenceEquals(active, capacityApplications[index]);
        }
        passed &= Case(ref cases, capacityExhaustionRejected &&
            capacityEntriesRemainLive);

        if (!passed && FirstFailedCase == 0) FirstFailedCase = cases;
        return passed && cases == CaseCount;
    }

    private static bool Case(ref int count, bool result)
    {
        count++;
        if (!result && FirstFailedCase == 0) FirstFailedCase = count;
        return result;
    }
}
#endif
