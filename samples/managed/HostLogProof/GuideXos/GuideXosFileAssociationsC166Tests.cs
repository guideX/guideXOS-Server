#if HOSTLOGPROOF_C166_FILE_ASSOCIATION_PROOF
using System;

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
            GxAbi.HostCallTableV1FullSize == 104u &&
            GxAbi.HostCallTableV2Size == 112u &&
            GxAbi.HostCallTableV3Size == 120u &&
            GxAbi.HostCallTableSize == 128u &&
            FieldOffset(tablePointer, &tablePointer->applicationSnapshot) == 104u &&
            FieldOffset(tablePointer, &tablePointer->closeApplication) == 112u &&
            FieldOffset(tablePointer, &tablePointer->associationService) == 120u);

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
        int callsAfterOperations = s_calls;
        passed &= Case(ref cases, GuideXosFileAssociations.Query(&context,
            &table, ".txt"u8).Status == GuideXosAssociationStatus.Success &&
            s_calls == callsAfterOperations + 1 && s_lastOperation == 0u);

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

        if (passed && cases >= 20)
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
        return passed && cases >= 20;
    }

    private static NativeAssociationServiceResponse s_response;
    private static int s_status;
    private static int s_calls;
    private static uint s_lastOperation;

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
