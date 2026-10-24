using System;

namespace HostLogProof.Applications;

/// <summary>Separates Notes proof UI state from explicit host-test invocation.</summary>
public static class GuideXosNotesProofTestInvocation
{
    public static bool IsC133ComboTestContext(ReadOnlySpan<byte> launchContext)
    {
        return launchContext.SequenceEqual("c133-combo-tests"u8) ||
            launchContext.SequenceEqual("c133-api"u8);
    }

    public static bool ShouldRunC133ComboTests(
        ReadOnlySpan<byte> launchContext, bool alreadyRun)
    {
        return !alreadyRun && IsC133ComboTestContext(launchContext);
    }

    public static bool IsC133HostTestContext(ReadOnlySpan<byte> launchContext)
    {
        return launchContext.SequenceEqual("c133-combo-host-tests"u8) ||
            launchContext.SequenceEqual("c133-host"u8);
    }

    public static bool ShouldRunC133HostTests(
        ReadOnlySpan<byte> launchContext, bool alreadyRun)
    {
        return !alreadyRun && IsC133HostTestContext(launchContext);
    }

    public static bool IsC134HostTestContext(ReadOnlySpan<byte> launchContext)
    {
        return launchContext.SequenceEqual("c134-host-tests"u8);
    }

    public static bool ShouldRunC134HostTests(
        ReadOnlySpan<byte> launchContext, bool alreadyRun)
    {
        return !alreadyRun &&
            (IsC133HostTestContext(launchContext) ||
                IsC134HostTestContext(launchContext));
    }

    public static bool IsC137QualificationContext(
        ReadOnlySpan<byte> launchContext)
    {
        return launchContext.SequenceEqual("c137"u8) ||
            launchContext.SequenceEqual("c137-native"u8) ||
            launchContext.SequenceEqual("c137-api"u8) ||
            launchContext.SequenceEqual("c137-host-tests"u8) ||
            launchContext.SequenceEqual("c137-relaunch"u8);
    }

    public static bool IsC137ProofInvocationAuthorized(
        ReadOnlySpan<byte> launchContext, bool c138SetupContext)
    {
        return !c138SetupContext &&
            IsC137QualificationContext(launchContext);
    }

    public static bool ShouldRunC137MouseWheelTests(
        ReadOnlySpan<byte> launchContext,
        bool c137SetupContext,
        bool c138SetupContext,
        bool alreadyRun)
    {
        return c137SetupContext && !alreadyRun &&
            IsC137ProofInvocationAuthorized(launchContext,
                c138SetupContext);
    }
}
