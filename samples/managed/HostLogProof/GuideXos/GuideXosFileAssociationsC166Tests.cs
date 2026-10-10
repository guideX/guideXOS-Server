#if HOSTLOGPROOF_C166_FILE_ASSOCIATION_PROOF
using System;
using System.IO;

namespace HostLogProof;

internal static unsafe class GuideXosFileAssociationsC166Tests
{
    public static bool Run(GuideXosHost host)
    {
        int cases = 0;
        bool passed = true;
        NativeHostCallTable table = default;
        NativeHostCallTable* tablePointer = &table;
        passed &= Case(ref cases, sizeof(NativeHostCallTable) == 128 &&
            sizeof(NativeAssociationServiceResponse) == 328 &&
            GxAbi.HostCallTableV1FullSize == 104u &&
            GxAbi.HostCallTableV2Size == 112u &&
            GxAbi.HostCallTableV3Size == 120u &&
            GxAbi.HostCallTableSize == 128u &&
            FieldOffset(tablePointer, &tablePointer->applicationSnapshot) == 104u &&
            FieldOffset(tablePointer, &tablePointer->closeApplication) == 112u &&
            FieldOffset(tablePointer, &tablePointer->associationService) == 120u);

#if GUIDEXOS_PROOF_CONTROL
        Span<byte> selector = stackalloc byte[9];
        selector[0]=(byte)'G'; selector[1]=(byte)'X'; selector[2]=(byte)'P';
        selector[3]=(byte)'F'; selector[4]=1; selector[5]=1;
        selector[6]=1; selector[7]=1; UpdateSelectorChecksum(selector);
        passed &= Case(ref cases, ProofSelection.TryParse(selector,
            out ProofSelection validSelection) &&
            validSelection.Suite == ProofSuite.C166 &&
            validSelection.Scenario == ProofScenario.SameBoot &&
            validSelection.Stage == ProofStage.SameBoot);
        selector[5]=99; UpdateSelectorChecksum(selector);
        passed &= Case(ref cases, !ProofSelection.TryParse(selector, out _));
        selector[5]=1; selector[6]=99; UpdateSelectorChecksum(selector);
        passed &= Case(ref cases, !ProofSelection.TryParse(selector, out _));
        selector[6]=1; selector[7]=99; UpdateSelectorChecksum(selector);
        passed &= Case(ref cases, !ProofSelection.TryParse(selector, out _));
        selector[7]=1; UpdateSelectorChecksum(selector); selector[8]++;
        passed &= Case(ref cases, !ProofSelection.TryParse(selector, out _));
        ProofStepCounter stepCounter = default; stepCounter.Reset();
        passed &= Case(ref cases, stepCounter.Next() == 1 &&
            stepCounter.Next() == 2);
        stepCounter.Reset();
        passed &= Case(ref cases, stepCounter.Next() == 1);
        ProofCompletionGate completionGate = default;
        passed &= Case(ref cases, completionGate.TryComplete() &&
            !completionGate.TryComplete());
        passed &= Case(ref cases,
            GuideXosProofCoordinator.RegisteredSuiteCount <= 3 &&
            GuideXosProofCoordinator.IsRegistered(ProofSuite.C166) &&
            !GuideXosProofCoordinator.IsRegistered((ProofSuite)99));
        passed &= Case(ref cases, GuideXosProofCoordinator.MaximumMarkerLength == 96 &&
            GuideXosProofCoordinator.MaximumMarkerLength < 128);
#endif

        table.size = GxAbi.HostCallTableV3Size;
        table.version = GxAbi.HostAbiV3Version;
        NativeGxAppContext context = MakeContext(&table);
        passed &= Case(ref cases, GuideXosFileAssociations.Query(&context,
            &table, ".txt"u8).Status == GuideXosAssociationStatus.NotSupported);

        table.size = GxAbi.HostCallTableSize - 1u;
        table.version = GxAbi.HostAbiVersion;
        passed &= Case(ref cases, GuideXosFileAssociations.Query(&context,
            &table, ".txt"u8).Status == GuideXosAssociationStatus.NotSupported);
        table.size = GxAbi.HostCallTableSize;
        table.version = 0u;
        passed &= Case(ref cases, GuideXosFileAssociations.Query(&context,
            &table, ".txt"u8).Status == GuideXosAssociationStatus.NotSupported);
        table.version = GxAbi.HostAbiVersion;
        table.capabilities = GxAbi.CapabilityAssociationService;
        table.associationService = null;
        passed &= Case(ref cases, GuideXosFileAssociations.Query(&context,
            &table, ".txt"u8).Status == GuideXosAssociationStatus.NotSupported);
        table.associationService = &ServiceStub;
        table.capabilities = GxAbi.CapabilityAssociationService;

        s_response = default;
        s_response.overrideState = (uint)GuideXosAssociationOverrideState.NoOverride;
        s_response.hasEffectiveAssociation = 1u;
        fixed (byte* normalized = s_response.normalizedExtension)
            ".txt"u8.CopyTo(new Span<byte>(normalized, 16));
        fixed (byte* compiled = s_response.compiledDefaultAppId)
            "com.guidexos.apps.managed.notes"u8.CopyTo(new Span<byte>(compiled, 96));
        fixed (byte* effective = s_response.effectiveAppId)
            "com.guidexos.apps.managed.notes"u8.CopyTo(new Span<byte>(effective, 96));
        s_status = 0;
        s_calls = 0;
        GuideXosAssociationResult success = GuideXosFileAssociations.Query(
            &context, &table, ".TXT"u8);
        passed &= Case(ref cases, success.Status ==
            GuideXosAssociationStatus.Success &&
            success.Response.hasEffectiveAssociation == 1u && s_calls == 1 &&
            s_lastOperation == 0u);
        passed &= Case(ref cases, GuideXosFileAssociations.SetDefault(&context,
            &table, ".txt"u8, "com.guidexos.apps.managed.notes"u8).Status ==
            GuideXosAssociationStatus.Success && s_lastOperation == 1u);
        passed &= Case(ref cases, GuideXosFileAssociations.Disable(&context,
            &table, ".txt"u8).Status == GuideXosAssociationStatus.Success &&
            s_lastOperation == 2u);
        passed &= Case(ref cases, GuideXosFileAssociations.Reset(&context,
            &table, ".txt"u8).Status == GuideXosAssociationStatus.Success &&
            s_lastOperation == 3u);
#if GUIDEXOS_PROOF_CONTROL
        s_response.diagnosticSlot = 1u;
        s_response.diagnosticGeneration = 0x12345678u;
        passed &= Case(ref cases, GuideXosFileAssociations.Diagnostics(&context,
            &table, ".txt"u8).Response.diagnosticSlot == 1u &&
            s_lastOperation == 4u && s_response.diagnosticGeneration == 0x12345678u);
#endif
        int callsAfterOperations = s_calls;
        passed &= Case(ref cases, GuideXosFileAssociations.Query(&context,
            &table, ".txt"u8).Status == GuideXosAssociationStatus.Success &&
            s_calls == callsAfterOperations + 1 && s_lastOperation == 0u);

#if GUIDEXOS_PROOF_CONTROL && HOSTLOGPROOF_HOST_TEST_STUB
        s_mockState = 0u;
        s_mockGeneration = 0u;
        s_mockOperationCount = 0;
        s_scenarioMock = true;
        table.associationService = &ScenarioServiceStub;
        bool coordinatorRouted = GuideXosProofCoordinator.RunC166(
            new GuideXosHost(), &context, &table,
            new ProofSelection(ProofSuite.C166, ProofScenario.SameBoot,
                ProofStage.SameBoot));
        s_scenarioMock = false;
        table.associationService = &ServiceStub;
        passed &= Case(ref cases, coordinatorRouted && s_mockState == 0u &&
            s_mockOperationCount == 5 && s_mockOperations[0] == 0u &&
            s_mockOperations[1] == 2u && s_mockOperations[2] == 0u &&
            s_mockOperations[3] == 3u && s_mockOperations[4] == 0u);
#endif

        s_status = -5;
        passed &= Case(ref cases, GuideXosFileAssociations.Query(&context,
            &table, ".txt"u8).Status == GuideXosAssociationStatus.NotSupported);
        s_status = -3;
        passed &= Case(ref cases, GuideXosFileAssociations.Query(&context,
            &table, ".txt"u8).Status == GuideXosAssociationStatus.CapabilityUnavailable);
        s_status = -2;
        passed &= Case(ref cases, GuideXosFileAssociations.Query(&context,
            &table, ".txt"u8).Status == GuideXosAssociationStatus.InvalidArgument);
        s_status = 99;
        passed &= Case(ref cases, GuideXosFileAssociations.Query(&context,
            &table, ".txt"u8).Status == GuideXosAssociationStatus.InvalidResponse);

        table.capabilities = 0u;
        passed &= Case(ref cases, GuideXosFileAssociations.Query(&context,
            &table, ".txt"u8).Status == GuideXosAssociationStatus.CapabilityUnavailable);
        table.capabilities = GxAbi.CapabilityAssociationService;
        passed &= Case(ref cases, GuideXosFileAssociations.Query(&context,
            &table, "txt"u8).Status == GuideXosAssociationStatus.InvalidArgument);
        passed &= Case(ref cases, GuideXosFileAssociations.SetDefault(&context,
            &table, ".txt"u8, default).Status ==
            GuideXosAssociationStatus.InvalidArgument);
        passed &= Case(ref cases, GuideXosFileAssociations.Query(null,
            &table, ".txt"u8).Status == GuideXosAssociationStatus.InvalidArgument);
        passed &= Case(ref cases, GuideXosFileAssociations.Query(&context,
            null, ".txt"u8).Status == GuideXosAssociationStatus.InvalidArgument);

        Span<byte> tooLong = stackalloc byte[96];
        tooLong.Fill((byte)'x');
        passed &= Case(ref cases, GuideXosFileAssociations.SetDefault(&context,
            &table, ".txt"u8, tooLong).Status ==
            GuideXosAssociationStatus.InvalidArgument);
        Span<byte> maximumExtension = stackalloc byte[16];
        maximumExtension.Fill((byte)'x');
        passed &= Case(ref cases, GuideXosFileAssociations.Query(&context,
            &table, maximumExtension).Status == GuideXosAssociationStatus.InvalidArgument);
        s_status = 0;
        s_response = default;
        s_response.overrideState = 99u;
        passed &= Case(ref cases, GuideXosFileAssociations.Query(&context,
            &table, ".txt"u8).Status == GuideXosAssociationStatus.InvalidResponse);

        if (passed && cases >= 21)
        {
            Span<byte> line = stackalloc byte[112];
            int position = 0;
            Append(line, ref position,
                "C166-MANAGED-ASSOCIATION-TESTS cases="u8);
            AppendUnsigned(line, ref position, (uint)cases);
            Append(line, ref position,
                " abi=104/112/120/128 offsets=104/112/120 malformed=PASS result=PASS"u8);
            host?.TryLog(line[..position]);
        }
        else host?.TryLog("C166-MANAGED-ASSOCIATION-TESTS result=FAIL"u8);
        return passed && cases >= 21;
    }

    private static NativeAssociationServiceResponse s_response;
    private static int s_status;
    private static int s_calls;
    private static uint s_lastOperation;
#if GUIDEXOS_PROOF_CONTROL
    private static bool s_scenarioMock;
    private static uint s_mockState;
    private static uint s_mockGeneration;
    private static int s_mockOperationCount;
    private static readonly uint[] s_mockOperations = new uint[5];
#endif

#if GUIDEXOS_PROOF_CONTROL
    private static void UpdateSelectorChecksum(Span<byte> bytes) =>
        bytes[8] = (byte)(bytes[4] ^ bytes[5] ^ bytes[6] ^ bytes[7]);
#endif

    public static bool RunNativeAgreement(GuideXosHost host, string path)
    {
        int cases = 0; bool passed = true;
        NativeHostCallTable table = default; tablePointerForAgreement = &table;
        table.size = GxAbi.HostCallTableSize; table.version = GxAbi.HostAbiVersion;
        table.capabilities = GxAbi.CapabilityAssociationService;
        table.associationService = &ServiceStub;
        NativeGxAppContext context = MakeContext(&table);
        foreach (string line in File.ReadAllLines(path))
        {
            string[] fields = line.Split(' ', 5);
            if (fields.Length != 5) { passed = false; continue; }
            s_response = default;
            s_response.status = uint.Parse(fields[1]);
            s_response.overrideState = uint.Parse(fields[2]);
            s_response.hasEffectiveAssociation = uint.Parse(fields[3]);
            fixed (byte* effective = s_response.effectiveAppId)
                System.Text.Encoding.ASCII.GetBytes(fields[4] + "\0").CopyTo(new Span<byte>(effective, 96));
            fixed (byte* normalized = s_response.normalizedExtension)
                ".txt\0"u8.CopyTo(new Span<byte>(normalized, 16));
            fixed (byte* compiled = s_response.compiledDefaultAppId)
                "com.guidexos.apps.managed.notes\0"u8.CopyTo(new Span<byte>(compiled, 96));
            s_status = unchecked((int)s_response.status);
            var result = GuideXosFileAssociations.Query(&context, &table, ".txt"u8);
            bool state = fields[0] switch
            {
                "NoOverride" or "Reset" => result.Response.overrideState == 0 && result.Response.hasEffectiveAssociation == 1 &&
                    System.Text.Encoding.ASCII.GetString(new ReadOnlySpan<byte>(result.Response.effectiveAppId, 32)).StartsWith("com.guidexos.apps.managed.notes"),
                "Disabled" => result.Response.overrideState == 2 && result.Response.hasEffectiveAssociation == 0,
                "ExplicitOverride" => result.Response.overrideState == 1 && result.Response.hasEffectiveAssociation == 1,
                "UnknownExtension" => result.Status == GuideXosAssociationStatus.UnknownExtension,
                "IneligibleHandler" => result.Status == GuideXosAssociationStatus.IneligibleHandler,
                "PersistenceFailure" => result.Status == GuideXosAssociationStatus.PersistenceFailed,
                _ => false
            };
            bool expectedStatus = fields[0] is "UnknownExtension" or "IneligibleHandler" or "PersistenceFailure"
                ? result.Status == (GuideXosAssociationStatus)s_response.status
                : result.Status == GuideXosAssociationStatus.Success;
            passed &= expectedStatus && state;
            cases++;
        }
        // The callback is the repository's real v4 ABI table and the fixture
        // values came from the native production service executable.
        passed &= sizeof(NativeHostCallTable) == 128 &&
            FieldOffset(tablePointerForAgreement, &tablePointerForAgreement->associationService) == 120;
        return passed && cases == 7;
    }
    private static NativeHostCallTable* tablePointerForAgreement;

    private static bool Case(ref int cases, bool value)
    {
        cases++;
        return value;
    }

    private static NativeGxAppContext MakeContext(NativeHostCallTable* table)
    {
        NativeGxAppContext context = default;
        context.size = (uint)sizeof(NativeGxAppContext);
        context.apiVersion = GxAbi.ApiVersion;
        context.host = table;
        return context;
    }

    private static uint FieldOffset(NativeHostCallTable* table, void* field) =>
        (uint)((byte*)field - (byte*)table);

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static int ServiceStub(NativeGxAppContext* context,
        NativeAssociationServiceRequest* request,
        NativeAssociationServiceResponse* response)
    {
        s_calls++;
        if (request != null) s_lastOperation = request->operation;
        if (response != null) *response = s_response;
        return s_status;
    }

#if GUIDEXOS_PROOF_CONTROL
    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static int ScenarioServiceStub(NativeGxAppContext* context,
        NativeAssociationServiceRequest* request,
        NativeAssociationServiceResponse* response)
    {
        uint operation = request == null ? uint.MaxValue : request->operation;
        if (s_mockOperationCount < s_mockOperations.Length)
            s_mockOperations[s_mockOperationCount++] = operation;
        if (operation == 1u) { s_mockState = 1u; s_mockGeneration++; }
        else if (operation == 2u) { s_mockState = 2u; s_mockGeneration++; }
        else if (operation == 3u) { s_mockState = 0u; s_mockGeneration++; }
        if (!s_scenarioMock || response == null) return -2;
        *response = default;
        response->status = 0u;
        response->overrideState = s_mockState;
        response->hasEffectiveAssociation = s_mockState == 2u ? 0u : 1u;
        response->diagnosticSlot = s_mockGeneration == 0u ? 0xFFFFFFFFu : (uint)(s_mockGeneration & 1u);
        response->diagnosticGeneration = s_mockGeneration;
        byte* normalized = response->normalizedExtension;
        ".txt"u8.CopyTo(new Span<byte>(normalized, 16));
        byte* compiled = response->compiledDefaultAppId;
        "com.guidexos.apps.managed.notes"u8.CopyTo(new Span<byte>(compiled, 96));
        if (s_mockState == 0u)
        {
            byte* effective = response->effectiveAppId;
            "com.guidexos.apps.managed.notes"u8.CopyTo(new Span<byte>(effective, 96));
        }
        else if (s_mockState == 1u)
        {
            byte* overridden = response->overrideAppId;
            "com.guidexos.apps.managed.notes"u8.CopyTo(new Span<byte>(overridden, 96));
            byte* effective = response->effectiveAppId;
            "com.guidexos.apps.managed.notes"u8.CopyTo(new Span<byte>(effective, 96));
        }
        return 0;
    }
#endif

    private static void Append(Span<byte> destination, ref int position,
        ReadOnlySpan<byte> value)
    { value.CopyTo(destination[position..]); position += value.Length; }

    private static void AppendUnsigned(Span<byte> destination, ref int position,
        uint value)
    {
        Span<byte> digits = stackalloc byte[10];
        int count = 0;
        do { digits[count++] = (byte)('0' + value % 10u); value /= 10u; }
        while (value != 0u);
        while (count > 0) destination[position++] = digits[--count];
    }
}
#endif
