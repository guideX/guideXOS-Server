#if HOSTLOGPROOF_C188R12R4_FACTORY_TESTS && HOSTLOGPROOF_C188R12R3_LIFETIME_TESTS
using System;
using System.Text;
using HostLogProof.Applications;

namespace HostLogProof;

public static class GuideXosManagedApplicationFactoryC188R12R4Tests
{
    public const int CaseCount = 14;
    public static int FirstFailedCase { get; private set; }
    private static string[] s_lastSuccessfulFactoryLines = Array.Empty<string>();

    public static string[] CapturedFactoryLines()
    {
        if (s_lastSuccessfulFactoryLines.Length != 0)
            return s_lastSuccessfulFactoryLines;
        return CaptureCurrentFactoryLines();
    }

    private static string[] CaptureCurrentFactoryLines()
    {
        string[] lines = new string[C188R12R4FactoryDiagnostics.TestLineCount];
        for (int index = 0; index < lines.Length; index++)
            lines[index] = Encoding.ASCII.GetString(
                C188R12R4FactoryDiagnostics.TestLine(index));
        return lines;
    }

    public static bool Run()
    {
        FirstFailedCase = 0;
        int cases = 0;
        bool passed = true;
        GuideXosManagedApplicationLifetime lifetime = new();
        GuideXosApplicationDescriptor notes = new(4u, "Managed Notes"u8,
            static () => new ManagedNotes());
        GuideXosApplicationDescriptor settings = new(5u,
            "Managed Settings Center"u8,
            static () => C188R12R4FactoryDiagnostics.CreateSettingsCenter());

        C188R12R3LifetimeDiagnostics.BeginTestCapture();
        C188R12R4FactoryDiagnostics.BeginTestCapture(2u);
        bool notesStarted = lifetime.TryStartDiagnostic(notes, 1u,
            out GuideXosApplication notesApplication,
            out uint notesGeneration, null, 1u,
            C188R12R3LifetimeDiagnostics.IdentityFor(4u, notes.Name));
        bool settingsStarted = lifetime.TryStartDiagnostic(settings, 2u,
            out GuideXosApplication settingsApplication,
            out uint settingsGeneration, null, 2u,
            C188R12R3LifetimeDiagnostics.IdentityFor(5u, settings.Name));
        int factoryLineCount = C188R12R4FactoryDiagnostics.TestLineCount;
        s_lastSuccessfulFactoryLines = CaptureCurrentFactoryLines();
        C188R12R4FactoryDiagnostics.EndTestCapture();
        int lifetimeLineCount = C188R12R3LifetimeDiagnostics.TestLineCount;
        C188R12R3LifetimeDiagnostics.EndTestCapture();

        passed &= Case(ref cases, notesStarted && notesApplication != null &&
            notesGeneration == 1u);
        passed &= Case(ref cases, settingsStarted &&
            settingsApplication is ManagedSettingsCenter &&
            settingsGeneration == 2u);
        passed &= Case(ref cases, lifetime.TryGet(4u, 1u,
            out GuideXosApplication retainedNotes) &&
            ReferenceEquals(notesApplication, retainedNotes));
        passed &= Case(ref cases, lifetime.TryGet(5u, 2u,
            out GuideXosApplication registeredSettings) &&
            ReferenceEquals(settingsApplication, registeredSettings));
        passed &= Case(ref cases, lifetime.C188R12R3ResidentMask == 3u &&
            lifetime.C188R12R3Generation == 2u);
        passed &= Case(ref cases, factoryLineCount == 15 &&
            HasOrderedFactoryStages("factory-invoke", "type-init-enter",
                "type-init-return", "object-allocation-request",
                "base-constructor-enter", "constructor-enter",
                "model-init-enter", "model-init-return",
                "controls-init-enter", "controls-init-return",
                "dialogs-init-enter", "dialogs-init-return",
                "constructor-return", "object-construction-result",
                "factory-return"));
        passed &= Case(ref cases, FactoryMarkerContains("stage=factory-invoke") &&
            FactoryMarkerContains("stage=type-init-enter") &&
            FactoryMarkerContains("stage=type-init-return"));
        passed &= Case(ref cases, FactoryMarkerContains(
            "stage=object-allocation-request") &&
            FactoryMarkerContains("stage=object-construction-result"));
        passed &= Case(ref cases, HasOrderedFactoryStages(
            "base-constructor-enter", "constructor-enter",
            "model-init-enter", "model-init-return", "controls-init-enter",
            "controls-init-return", "constructor-return"));
        passed &= Case(ref cases, FactoryMarkerContains(
            "invocation=2 selector=5 operation=11 stage=factory-return code=0 result=created"));
        passed &= Case(ref cases, lifetimeLineCount == 22 &&
            LifetimeMarkerHasStage(2u, "factory-return") &&
            LifetimeMarkerHasStage(2u, "resident-register-return") &&
            LifetimeMarkerHasStage(2u, "exit-success"));
        passed &= Case(ref cases, !FactoryMarkerContains("stage=factory-failure") &&
            !LifetimeMarkerHasStage(2u, "factory-throw"));

        GuideXosManagedApplicationLifetime failingLifetime = new();
        GuideXosApplicationDescriptor failingSettings = new(5u,
            "Failing Settings factory"u8,
            static () => throw new InvalidOperationException("synthetic"));
        C188R12R3LifetimeDiagnostics.BeginTestCapture();
        C188R12R4FactoryDiagnostics.BeginTestCapture(9u);
        bool exceptionObserved = false;
        try
        {
            _ = failingLifetime.TryStartDiagnostic(failingSettings, 9u,
                out _, out _, null, 9u,
                C188R12R3LifetimeDiagnostics.IdentityFor(5u,
                    failingSettings.Name));
        }
        catch (InvalidOperationException)
        {
            exceptionObserved = true;
        }
        passed &= Case(ref cases, exceptionObserved &&
            FactoryMarkerContains("stage=factory-failure") &&
            LifetimeMarkerHasStage(9u, "factory-throw"));
        passed &= Case(ref cases, failingLifetime.C188R12R3ResidentMask == 0u &&
            !failingLifetime.TryGet(5u, out _));
        C188R12R4FactoryDiagnostics.EndTestCapture();
        C188R12R3LifetimeDiagnostics.EndTestCapture();

        return cases == CaseCount && passed;
    }

    private static bool Case(ref int cases, bool condition)
    {
        cases++;
        if (!condition && FirstFailedCase == 0) FirstFailedCase = cases;
        return condition;
    }

    private static bool FactoryMarkerContains(string value)
    {
        for (int index = 0;
             index < C188R12R4FactoryDiagnostics.TestLineCount; index++)
        {
            string line = Encoding.ASCII.GetString(
                C188R12R4FactoryDiagnostics.TestLine(index));
            if (line.Contains(value, StringComparison.Ordinal)) return true;
        }
        return false;
    }

    private static bool HasOrderedFactoryStages(params string[] stages)
    {
        int next = 0;
        for (int index = 0;
             index < C188R12R4FactoryDiagnostics.TestLineCount &&
             next < stages.Length; index++)
        {
            string line = Encoding.ASCII.GetString(
                C188R12R4FactoryDiagnostics.TestLine(index));
            if (line.Contains("stage=" + stages[next],
                    StringComparison.Ordinal))
                next++;
        }
        return next == stages.Length;
    }

    private static bool LifetimeMarkerHasStage(uint invocationId,
        string stage)
    {
        for (int index = 0;
             index < C188R12R3LifetimeDiagnostics.TestLineCount; index++)
        {
            string line = Encoding.ASCII.GetString(
                C188R12R3LifetimeDiagnostics.TestLine(index));
            if (line.Contains("invocation=" + invocationId + " stage=" + stage,
                    StringComparison.Ordinal))
                return true;
        }
        return false;
    }
}
#endif
