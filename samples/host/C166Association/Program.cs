using System;

namespace HostLogProof;

internal static class Program
{
    private static int Main()
    {
        bool passed = GuideXosFileAssociationsC166Tests.Run(new GuideXosHost());
        Console.WriteLine(passed
            ? "C166 managed host tests PASS ABI=v4/128 offsets=104/112/120"
            : "C166 managed host tests FAIL");
        return passed ? 0 : 1;
    }
}

public sealed class GuideXosHost
{
    public bool TryLog(ReadOnlySpan<byte> message)
    {
        Console.WriteLine(System.Text.Encoding.UTF8.GetString(message));
        return true;
    }
}
