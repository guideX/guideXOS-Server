#if GUIDEXOS_PROOF_CONTROL
using System;

namespace HostLogProof;

// This small protocol is compiled only into explicitly requested proof images.
public enum ProofSuite : byte { None = 0, C166 = 1 }
public enum ProofScenario : byte { None = 0, SameBoot = 1, Persistence = 2 }
public enum ProofStage : byte { None = 0, SameBoot = 1, PreReboot = 2, PostReboot = 3 }
public enum ProofOperation : byte { Query = 1, SetManagedNotes = 2, Disable = 3, Reset = 4, Diagnostics = 5 }

public readonly struct ProofSelection
{
    public readonly ProofSuite Suite;
    public readonly ProofScenario Scenario;
    public readonly ProofStage Stage;
    public ProofSelection(ProofSuite suite, ProofScenario scenario, ProofStage stage)
    { Suite = suite; Scenario = scenario; Stage = stage; }

    public static bool TryParse(ReadOnlySpan<byte> bytes, out ProofSelection selection)
    {
        selection = default;
        // Fixed binary record: GXPF, version, suite, scenario, stage, checksum.
        if (bytes.Length != 9 || bytes[0] != 'G' || bytes[1] != 'X' ||
            bytes[2] != 'P' || bytes[3] != 'F' || bytes[4] != 1 ||
            bytes[8] != (byte)(bytes[4] ^ bytes[5] ^ bytes[6] ^ bytes[7])) return false;
        var suite = (ProofSuite)bytes[5];
        var scenario = (ProofScenario)bytes[6];
        var stage = (ProofStage)bytes[7];
        if (suite != ProofSuite.C166 ||
            !((scenario == ProofScenario.SameBoot && stage == ProofStage.SameBoot) ||
              (scenario == ProofScenario.Persistence &&
               (stage == ProofStage.PreReboot || stage == ProofStage.PostReboot)))) return false;
        selection = new ProofSelection(suite, scenario, stage);
        return true;
    }
}

internal struct ProofStepCounter
{
    private int _next;
    public void Reset() => _next = 1;
    public int Next() { if (_next == 0) _next = 1; return _next++; }
}

internal struct ProofCompletionGate
{
    private bool _completed;
    public bool TryComplete()
    { if (_completed) return false; _completed = true; return true; }
}

public static unsafe class GuideXosProofCoordinator
{
    public const int MaximumMarkerLength = 96;
    private static readonly ProofSuite[] s_registeredSuites = { ProofSuite.C166 };
    private static ProofCompletionGate s_completionGate;
    private static ProofStepCounter s_steps;
    public static int RegisteredSuiteCount => s_registeredSuites.Length;

    internal static bool IsRegistered(ProofSuite suite)
    {
        for (int i = 0; i < s_registeredSuites.Length; i++)
            if (s_registeredSuites[i] == suite) return true;
        return false;
    }

    public static bool Run(GuideXosHost host, NativeGxAppContext* context,
        NativeHostCallTable* table, ProofSelection selection)
    {
        if (host == null || context == null || table == null ||
            !s_completionGate.TryComplete()) return false;
        s_steps.Reset();
        bool managedTestsPass = true;
#if HOSTLOGPROOF_C166_FILE_ASSOCIATION_PROOF
        managedTestsPass = GuideXosFileAssociationsC166Tests.Run(host);
#endif
        bool pass = managedTestsPass && IsRegistered(selection.Suite) &&
            selection.Suite == ProofSuite.C166 && RunC166(host, context, table, selection);
        Span<byte> line = stackalloc byte[MaximumMarkerLength];
        int p = 0;
        Append(line, ref p, pass ? "PROOF-SCENARIO-PASS C166 "u8 : "PROOF-SCENARIO-FAIL C166 "u8);
        AppendScenarioStage(line, ref p, selection);
        host.TryLog(line[..p]);
        return pass;
    }

    internal static bool RunC166(GuideXosHost host, NativeGxAppContext* context,
        NativeHostCallTable* table, ProofSelection s)
    {
        if (s.Scenario == ProofScenario.SameBoot)
        {
            return Step(host, context, table, s, ProofOperation.Query, 0) &&
                Step(host, context, table, s, ProofOperation.Disable, 2) &&
                Step(host, context, table, s, ProofOperation.Query, 2) &&
                Step(host, context, table, s, ProofOperation.Reset, 0) &&
                Step(host, context, table, s, ProofOperation.Query, 0);
        }
        if (s.Stage == ProofStage.PreReboot)
            return Step(host, context, table, s, ProofOperation.Query, 0) &&
                Step(host, context, table, s, ProofOperation.Disable, 2) &&
                Step(host, context, table, s, ProofOperation.Diagnostics, 2);
        return Step(host, context, table, s, ProofOperation.Query, 2) &&
            Step(host, context, table, s, ProofOperation.Diagnostics, 2) &&
            Step(host, context, table, s, ProofOperation.Reset, 0) &&
            Step(host, context, table, s, ProofOperation.Query, 0);
    }

    private static bool Step(GuideXosHost host, NativeGxAppContext* context,
        NativeHostCallTable* table, ProofSelection s, ProofOperation operation, int expectedState)
    {
        int step = s_steps.Next();
        GuideXosAssociationResult result;
        switch (operation)
        {
            case ProofOperation.Query: result = GuideXosFileAssociations.Query(context, table, ".txt"u8); break;
            case ProofOperation.SetManagedNotes: result = GuideXosFileAssociations.SetDefault(context, table, ".txt"u8, "com.guidexos.apps.managed.notes"u8); break;
            case ProofOperation.Disable: result = GuideXosFileAssociations.Disable(context, table, ".txt"u8); break;
            case ProofOperation.Reset: result = GuideXosFileAssociations.Reset(context, table, ".txt"u8); break;
            case ProofOperation.Diagnostics: result = GuideXosFileAssociations.Diagnostics(context, table, ".txt"u8); break;
            default: return false;
        }
        bool valid = result.Status == GuideXosAssociationStatus.Success &&
            (expectedState < 0 || result.Response.overrideState == (uint)expectedState);
        Span<byte> line = stackalloc byte[MaximumMarkerLength];
        int p = 0;
        Append(line, ref p, "PROOF C166 "u8); AppendScenarioStage(line, ref p, s);
        Append(line, ref p, " "u8); AppendNumber(line, ref p, (uint)step);
        Append(line, ref p, valid ? " PASS "u8 : " FAIL "u8);
        AppendOperation(line, ref p, operation);
        Append(line, ref p, " st="u8); AppendNumber(line, ref p, (uint)result.Status);
        Append(line, ref p, " ov="u8); AppendNumber(line, ref p, result.Response.overrideState);
        if (operation == ProofOperation.Diagnostics)
        {
            Append(line, ref p, " slot="u8);
            Append(line, ref p, result.Response.diagnosticSlot == 0xFFFFFFFFu ? "N"u8 : result.Response.diagnosticSlot == 0u ? "A"u8 : "B"u8);
            Append(line, ref p, " gen="u8); AppendUnsigned64(line, ref p, result.Response.diagnosticGeneration);
        }
        host.TryLog(line[..p]);
        return valid;
    }

    private static void AppendScenarioStage(Span<byte> b, ref int p, ProofSelection s)
    {
        Append(b, ref p, s.Scenario == ProofScenario.SameBoot ? "S"u8 : "P"u8);
        Append(b, ref p, " "u8);
        Append(b, ref p, s.Stage == ProofStage.PreReboot ? "Pre"u8 : s.Stage == ProofStage.PostReboot ? "Post"u8 : "Same"u8);
    }
    private static void AppendOperation(Span<byte> b, ref int p, ProofOperation op) => Append(b, ref p, op switch
    { ProofOperation.Query => "Q"u8, ProofOperation.SetManagedNotes => "Set"u8,
      ProofOperation.Disable => "Dis"u8, ProofOperation.Reset => "Rst"u8, _ => "Diag"u8 });
    private static bool Append(Span<byte> b, ref int p, ReadOnlySpan<byte> value)
    { if (value.Length > b.Length - p) return false; value.CopyTo(b[p..]); p += value.Length; return true; }
    private static void AppendNumber(Span<byte> b, ref int p, uint n)
    { Span<byte> d = stackalloc byte[10]; int c=0; do { d[c++]=(byte)('0'+n%10); n/=10; } while(n!=0); while(c!=0) b[p++]=d[--c]; }
    private static void AppendUnsigned64(Span<byte> b, ref int p, ulong n)
    { Span<byte> d = stackalloc byte[20]; int c=0; do { d[c++]=(byte)('0'+n%10); n/=10; } while(n!=0); while(c!=0) b[p++]=d[--c]; }
}
#endif
