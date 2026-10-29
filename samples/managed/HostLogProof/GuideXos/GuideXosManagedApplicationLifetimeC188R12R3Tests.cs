#if HOSTLOGPROOF_C188R12R3_LIFETIME_TESTS
using System;
using System.Text;

namespace HostLogProof;

public static class GuideXosManagedApplicationLifetimeC188R12R3Tests
{
    public const int CaseCount = 23;
    public static int FirstFailedCase { get; private set; }
    public static uint SettingsConstructionBytes { get; private set; }

    private sealed class ProbeApplication : GuideXosApplication
    {
        public ProbeApplication(uint identity) => Identity = identity;
        public uint Identity { get; }
        public int TeardownCalls { get; private set; }
        public override GuideXosResult Launch(GuideXosHost host) =>
            GuideXosResult.Success;
        public override void OnTearingDown() => TeardownCalls++;
    }

    public static bool Run()
    {
        FirstFailedCase = 0;
        int cases = 0;
        bool passed = true;

        GuideXosManagedApplicationLifetime lifetime = new();
        GuideXosApplicationDescriptor notesDescriptor = new(4u,
            "Managed Notes"u8, static () => new ProbeApplication(4u));
        GuideXosApplicationDescriptor settingsDescriptor = new(5u,
            "Managed Settings Center"u8,
            static () => new ProbeApplication(5u));

        C188R12R3LifetimeDiagnostics.BeginTestCapture();
        bool notesStarted = lifetime.TryStartDiagnostic(notesDescriptor, 1u,
            out GuideXosApplication notes, out uint notesGeneration, null, 1u,
            C188R12R3LifetimeDiagnostics.IdentityFor(4u,
                notesDescriptor.Name));
        uint notesOnlyResidentMask = lifetime.C188R12R3ResidentMask;
        bool settingsStarted = lifetime.TryStartDiagnostic(settingsDescriptor,
            2u, out GuideXosApplication settings, out uint settingsGeneration,
            null, 2u, C188R12R3LifetimeDiagnostics.IdentityFor(5u,
                settingsDescriptor.Name));
        int dualStartMarkerCount =
            C188R12R3LifetimeDiagnostics.TestLineCount;
        C188R12R3LifetimeDiagnostics.EndTestCapture();

        passed &= Case(ref cases, notesStarted && notes != null &&
            notesGeneration == 1u);
        passed &= Case(ref cases, notesOnlyResidentMask == 1u);
        passed &= Case(ref cases, settingsStarted && settings != null &&
            settingsGeneration == 2u);
        passed &= Case(ref cases, lifetime.C188R12R3ResidentMask == 3u &&
            lifetime.TryGet(4u, 1u, out GuideXosApplication retainedNotes) &&
            ReferenceEquals(notes, retainedNotes));
        passed &= Case(ref cases, lifetime.C188R12R3Generation == 2u &&
            lifetime.C188R12R3ResidentMask == 3u);
        passed &= Case(ref cases, dualStartMarkerCount == 22 &&
            HasOrderedStages(1u, "enter", "existing-resident", "capacity-check",
                "generation-check", "factory-resolve", "factory-enter",
                "factory-return", "resident-register-enter",
                "resident-register-return", "owner-assign", "exit-success") &&
            HasOrderedStages(2u, "enter", "existing-resident", "capacity-check",
                "generation-check", "factory-resolve", "factory-enter",
                "factory-return", "resident-register-enter",
                "resident-register-return", "owner-assign", "exit-success"));
        passed &= Case(ref cases, MarkerContains(1u,
            "identity=com.guidexos.apps.managed.notes count=0 mask=0 generation=1 code=-1 state=preflight") &&
            MarkerContains(2u,
                "identity=com.guidexos.apps.managed.settingscenter count=1 mask=1 generation=2 code=0 state=created"));

        GuideXosManagedApplicationLifetime nullFactoryLifetime = new();
        GuideXosApplicationDescriptor nullFactoryDescriptor = new(21u,
            "Null factory"u8, static () => null);
        C188R12R3LifetimeDiagnostics.BeginTestCapture();
        bool nullFactoryStarted = nullFactoryLifetime.TryStartDiagnostic(
            nullFactoryDescriptor, 21u, out _, out _, null, 21u,
            C188R12R3LifetimeDiagnostics.IdentityFor(21u,
                nullFactoryDescriptor.Name));
        bool nullFactoryEvidence = MarkerHasStage(21u, "factory-return") &&
            MarkerHasStage(21u, "exit-failure") &&
            MarkerContains(21u, "stage=factory-return") &&
            MarkerContains(21u, "code=4 state=null");
        C188R12R3LifetimeDiagnostics.EndTestCapture();
        passed &= Case(ref cases, !nullFactoryStarted &&
            nullFactoryLifetime.C188R12R3ResidentMask == 0u);
        passed &= Case(ref cases, nullFactoryEvidence);

        GuideXosManagedApplicationLifetime missingFactoryLifetime = new();
        GuideXosApplicationDescriptor missingFactoryDescriptor = new(22u,
            "Missing factory"u8, new ProbeApplication(22u));
        C188R12R3LifetimeDiagnostics.BeginTestCapture();
        bool missingFactoryStarted = missingFactoryLifetime.TryStartDiagnostic(
            missingFactoryDescriptor, 22u, out _, out _, null, 22u,
            C188R12R3LifetimeDiagnostics.IdentityFor(22u,
                missingFactoryDescriptor.Name));
        bool missingFactoryEvidence = MarkerHasStage(22u, "factory-resolve") &&
            MarkerContains(22u, "code=3 state=missing") &&
            !MarkerHasStage(22u, "factory-enter");
        C188R12R3LifetimeDiagnostics.EndTestCapture();
        passed &= Case(ref cases, !missingFactoryStarted &&
            missingFactoryLifetime.C188R12R3ResidentMask == 0u);
        passed &= Case(ref cases, missingFactoryEvidence);

        GuideXosManagedApplicationLifetime exceptionLifetime = new();
        GuideXosApplicationDescriptor exceptionDescriptor = new(23u,
            "Throwing factory"u8,
            static () => throw new InvalidOperationException());
        C188R12R3LifetimeDiagnostics.BeginTestCapture();
        bool exceptionPreserved = false;
        try
        {
            _ = exceptionLifetime.TryStartDiagnostic(exceptionDescriptor,
                23u, out _, out _, null, 23u,
                C188R12R3LifetimeDiagnostics.IdentityFor(23u,
                    exceptionDescriptor.Name));
        }
        catch (InvalidOperationException)
        {
            exceptionPreserved = true;
        }
        bool exceptionEvidence = MarkerHasStage(23u, "factory-enter") &&
            MarkerHasStage(23u, "factory-throw") &&
            MarkerContains(23u, "state=factory-exception");
        C188R12R3LifetimeDiagnostics.EndTestCapture();
        passed &= Case(ref cases, exceptionPreserved &&
            exceptionLifetime.C188R12R3ResidentMask == 0u);
        passed &= Case(ref cases, exceptionEvidence);

        GuideXosManagedApplicationLifetime outOfMemoryLifetime = new();
        GuideXosApplicationDescriptor outOfMemoryDescriptor = new(24u,
            "Allocation exception"u8,
            static () => throw new OutOfMemoryException());
        C188R12R3LifetimeDiagnostics.BeginTestCapture();
        bool allocationExceptionPreserved = false;
        try
        {
            _ = outOfMemoryLifetime.TryStartDiagnostic(outOfMemoryDescriptor,
                24u, out _, out _, null, 24u,
                C188R12R3LifetimeDiagnostics.IdentityFor(24u,
                    outOfMemoryDescriptor.Name));
        }
        catch (OutOfMemoryException)
        {
            allocationExceptionPreserved = true;
        }
        bool allocationExceptionEvidence = MarkerHasStage(24u,
            "factory-throw") && MarkerContains(24u,
                "state=factory-exception");
        C188R12R3LifetimeDiagnostics.EndTestCapture();
        passed &= Case(ref cases, allocationExceptionPreserved &&
            outOfMemoryLifetime.C188R12R3ResidentMask == 0u);
        passed &= Case(ref cases, allocationExceptionEvidence);

        GuideXosManagedApplicationLifetime replacementLifetime = new();
        ProbeApplication previous = new(41u);
        GuideXosApplicationDescriptor previousDescriptor = new(41u,
            "Replacement probe"u8, () => previous);
        ProbeApplication replacement = new(42u);
        GuideXosApplicationDescriptor replacementDescriptor = new(41u,
            "Replacement probe"u8, () => replacement);
        C188R12R3LifetimeDiagnostics.BeginTestCapture();
        bool initial = replacementLifetime.TryStartDiagnostic(
            previousDescriptor, 41u, out _, out _, null, 41u,
            C188R12R3LifetimeDiagnostics.IdentityFor(41u,
                previousDescriptor.Name));
        bool replaced = replacementLifetime.TryStartDiagnostic(
            replacementDescriptor, 42u, out GuideXosApplication current,
            out uint replacementGeneration, null, 42u,
            C188R12R3LifetimeDiagnostics.IdentityFor(41u,
                replacementDescriptor.Name));
        bool replacementEvidence = MarkerHasStage(42u, "teardown-enter") &&
            MarkerHasStage(42u, "teardown-return") &&
            MarkerHasStage(42u, "resident-register-return") &&
            HasOrderedStages(42u, "resident-register-enter", "teardown-enter",
                "teardown-return", "resident-register-return", "owner-assign",
                "exit-success");
        C188R12R3LifetimeDiagnostics.EndTestCapture();
        passed &= Case(ref cases, initial && replaced &&
            ReferenceEquals(current, replacement) && replacementGeneration == 42u);
        passed &= Case(ref cases, previous.TeardownCalls == 1 &&
            replacementLifetime.C188R12R3ResidentMask == 1u &&
            replacementLifetime.C188R12R3Generation == 42u);
        passed &= Case(ref cases, replacementEvidence);

        GuideXosManagedApplicationLifetime fullLifetime = new();
        bool filled = true;
        for (uint selector = 30u; selector < 35u; selector++)
        {
            uint captured = selector;
            GuideXosApplicationDescriptor descriptor = new(selector,
                "capacity probe"u8, () => new ProbeApplication(captured));
            filled &= fullLifetime.TryStartDiagnostic(descriptor, selector,
                out _, out _, null, selector,
                C188R12R3LifetimeDiagnostics.IdentityFor(selector,
                    descriptor.Name));
        }
        int rejectedFactoryCalls = 0;
        GuideXosApplicationDescriptor overflowDescriptor = new(35u,
            "capacity overflow"u8, () =>
            {
                rejectedFactoryCalls++;
                return new ProbeApplication(35u);
            });
        C188R12R3LifetimeDiagnostics.BeginTestCapture();
        bool overflowStarted = fullLifetime.TryStartDiagnostic(
            overflowDescriptor, 35u, out _, out _, null, 35u,
            C188R12R3LifetimeDiagnostics.IdentityFor(35u,
                overflowDescriptor.Name));
        bool capacityEvidence = MarkerHasStage(35u, "capacity-check") &&
            MarkerContains(35u, "code=1 state=full") &&
            !MarkerHasStage(35u, "factory-enter");
        C188R12R3LifetimeDiagnostics.EndTestCapture();
        passed &= Case(ref cases, filled && !overflowStarted &&
            rejectedFactoryCalls == 0 && fullLifetime.C188R12R3ResidentMask == 31u);
        passed &= Case(ref cases, capacityEvidence);

        GuideXosManagedApplicationLifetime generationLifetime = new();
        GuideXosApplicationDescriptor maxGenerationDescriptor = new(51u,
            "generation probe"u8, static () => new ProbeApplication(51u));
        GuideXosApplicationDescriptor nextGenerationDescriptor = new(52u,
            "generation probe"u8, static () => new ProbeApplication(52u));
        _ = generationLifetime.TryStart(maxGenerationDescriptor,
            uint.MaxValue, out _, out _);
        C188R12R3LifetimeDiagnostics.BeginTestCapture();
        bool generationOverflowStarted = generationLifetime.TryStartDiagnostic(
            nextGenerationDescriptor, 0u, out _, out uint currentGeneration,
            null, 52u, C188R12R3LifetimeDiagnostics.IdentityFor(52u,
                nextGenerationDescriptor.Name));
        bool generationEvidence = MarkerContains(52u,
            "stage=generation-check") &&
            MarkerContains(52u, "code=2 state=overflow") &&
            MarkerContains(52u, "state=generation");
        C188R12R3LifetimeDiagnostics.EndTestCapture();
        passed &= Case(ref cases, !generationOverflowStarted &&
            currentGeneration == uint.MaxValue &&
            generationLifetime.C188R12R3ResidentMask == 1u);
        passed &= Case(ref cases, generationEvidence);

        long bytesBefore = GC.GetAllocatedBytesForCurrentThread();
        _ = new Applications.ManagedSettingsCenter();
        long bytesAfter = GC.GetAllocatedBytesForCurrentThread();
        SettingsConstructionBytes = (uint)Math.Clamp(bytesAfter - bytesBefore,
            0L, uint.MaxValue);
        passed &= Case(ref cases, SettingsConstructionBytes > 0u);

        if (cases != CaseCount) passed = false;
        return passed;
    }

    private static bool Case(ref int cases, bool condition)
    {
        cases++;
        if (!condition && FirstFailedCase == 0) FirstFailedCase = cases;
        return condition;
    }

    private static bool MarkerHasStage(uint invocation, string stage)
    {
        for (int index = 0;
             index < C188R12R3LifetimeDiagnostics.TestLineCount; index++)
        {
            string line = Encoding.ASCII.GetString(
                C188R12R3LifetimeDiagnostics.TestLine(index));
            if (line.Contains($"invocation={invocation} ",
                    StringComparison.Ordinal) &&
                line.Contains($"stage={stage} ", StringComparison.Ordinal))
                return true;
        }
        return false;
    }

    private static bool MarkerContains(uint invocation, string fragment)
    {
        for (int index = 0;
             index < C188R12R3LifetimeDiagnostics.TestLineCount; index++)
        {
            string line = Encoding.ASCII.GetString(
                C188R12R3LifetimeDiagnostics.TestLine(index));
            if (line.Contains($"invocation={invocation} ",
                    StringComparison.Ordinal) &&
                line.Contains(fragment, StringComparison.Ordinal))
                return true;
        }
        return false;
    }

    private static bool HasOrderedStages(uint invocation,
        params string[] stages)
    {
        int nextStage = 0;
        for (int index = 0;
             index < C188R12R3LifetimeDiagnostics.TestLineCount &&
                 nextStage < stages.Length; index++)
        {
            string line = Encoding.ASCII.GetString(
                C188R12R3LifetimeDiagnostics.TestLine(index));
            if (!line.Contains($"invocation={invocation} ",
                    StringComparison.Ordinal)) continue;
            if (line.Contains($"stage={stages[nextStage]} ",
                    StringComparison.Ordinal)) nextStage++;
        }
        return nextStage == stages.Length;
    }
}
#endif
