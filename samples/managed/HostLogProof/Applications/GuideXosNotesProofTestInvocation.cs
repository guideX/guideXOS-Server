using System;

namespace HostLogProof.Applications;

/// <summary>Separates Notes proof UI state from explicit host-test invocation.</summary>
public static class GuideXosNotesProofTestInvocation
{
    public static bool IsC133HostTestContext(ReadOnlySpan<byte> launchContext)
    {
        return launchContext.SequenceEqual("c133-combo-host-tests"u8) ||
            launchContext.SequenceEqual("c133-host"u8);
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
}
