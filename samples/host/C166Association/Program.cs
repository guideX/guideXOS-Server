using System;

namespace HostLogProof;

internal static class Program
{
    private static int Main(string[] args)
    {
        bool passed = GuideXosFileAssociationsC166Tests.Run(new GuideXosHost());
        if (args.Length > 0)
        {
            bool agreement = GuideXosFileAssociationsC166Tests.RunNativeAgreement(new GuideXosHost(), args[0]);
            Console.WriteLine($"C166 native-managed agreement cases=7 result={(agreement ? "PASS" : "FAIL")} statusMapping=PASS abi=v4/128 associationOffset=120");
            passed &= agreement;
        }
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
