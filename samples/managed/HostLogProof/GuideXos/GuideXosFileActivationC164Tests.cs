#if HOSTLOGPROOF_C164_FILE_ACTIVATION_PROOF
using System;
using System.Runtime.InteropServices;

namespace HostLogProof;

internal static unsafe class GuideXosFileActivationC164Tests
{
    private static readonly byte[] s_receivedPath = new byte[96];
    private static int s_stubResult;
    private static int s_stubCalls;
    private static uint s_receivedLength;

    public static bool Run(GuideXosHost host)
    {
        NativeGxAppContext* originalContext = host != null
            ? host.Context : null;
        uint originalSelector = host?.Selector ?? 0u;
        int cases = 0;
        bool passed = true;
        passed &= Case(ref cases, host != null && host.Selector == 8u);

        NativeHostCallTable table = default;
        table.size = GxAbi.HostCallTableSize;
        table.version = GxAbi.HostAbiVersion;
        NativeGxAppContext context = MakeContext(&table, 8u);
        context.applicationRequestState = (void*)1;
        context.requestFileActivation = &RequestStub;
        bool hostCreated = GuideXosHost.TryCreate(&context, 8u,
            out GuideXosHost activationHost, out GuideXosResult createResult) &&
            createResult == GuideXosResult.Success && activationHost != null;
        passed &= Case(ref cases, hostCreated);

        if (hostCreated)
        {
            GuideXosNotesReturnSessionC155 staleSession =
                GuideXosNotesReturnSessionC155.Shared;
            staleSession.Clear();
            bool staleSessionArmed = staleSession.TryArm(
                "/system/apps/C155/alpha.txt", 4, 2, 0, out _);
            passed &= Case(ref cases, staleSessionArmed &&
                staleSession.IsPending);
        }

        if (hostCreated)
        {
            ReadOnlySpan<byte> selectedPath =
                "/system/apps/C151/alpha.txt"u8;
            s_stubCalls = 0;
            s_stubResult = 0;
            GuideXosFileActivationResult accepted =
                activationHost.TryRequestFileActivation(selectedPath);
            passed &= Case(ref cases, accepted ==
                GuideXosFileActivationResult.Accepted);
            passed &= Case(ref cases, s_stubCalls == 1 &&
                s_receivedLength == (uint)selectedPath.Length);
            passed &= Case(ref cases, s_receivedPath.AsSpan(0,
                selectedPath.Length).SequenceEqual(selectedPath));

            s_stubResult = 1;
            passed &= Case(ref cases, activationHost.TryRequestFileActivation(
                selectedPath) == GuideXosFileActivationResult.Unsupported);
            s_stubResult = -3;
            passed &= Case(ref cases, activationHost.TryRequestFileActivation(
                selectedPath) == GuideXosFileActivationResult.Directory);
            s_stubResult = -4;
            passed &= Case(ref cases, activationHost.TryRequestFileActivation(
                selectedPath) == GuideXosFileActivationResult.NotRegularFile);
            s_stubResult = -5;
            passed &= Case(ref cases, activationHost.TryRequestFileActivation(
                selectedPath) == GuideXosFileActivationResult.NotFound);
            s_stubResult = -6;
            passed &= Case(ref cases, activationHost.TryRequestFileActivation(
                selectedPath) == GuideXosFileActivationResult.IoFailure);
            s_stubResult = 99;
            passed &= Case(ref cases, activationHost.TryRequestFileActivation(
                selectedPath) == GuideXosFileActivationResult.NotSupported);

            int beforeInvalid = s_stubCalls;
            Span<byte> tooLong = stackalloc byte[97];
            tooLong.Fill((byte)'a');
            passed &= Case(ref cases, activationHost.TryRequestFileActivation(
                tooLong) == GuideXosFileActivationResult.PathTooLong);
            passed &= Case(ref cases, s_stubCalls == beforeInvalid);
            passed &= Case(ref cases, activationHost.TryRequestFileActivation(
                ReadOnlySpan<byte>.Empty) ==
                    GuideXosFileActivationResult.InvalidPath);
            Span<byte> embeddedNul = stackalloc byte[] { (byte)'/', 0, (byte)'x' };
            passed &= Case(ref cases, activationHost.TryRequestFileActivation(
                embeddedNul) == GuideXosFileActivationResult.InvalidPath);
            passed &= Case(ref cases, s_stubCalls == beforeInvalid);
        }

        NativeGxAppContext shortContext = MakeContext(&table, 8u);
        shortContext.size = GxAbi.AppContextLaunchPrefixSize;
        shortContext.applicationRequestState = (void*)1;
        shortContext.requestFileActivation = &RequestStub;
        passed &= Case(ref cases, GuideXosHost.TryCreate(&shortContext, 8u,
                out GuideXosHost shortHost, out _) &&
            shortHost.TryRequestFileActivation("/a.txt"u8) ==
                GuideXosFileActivationResult.NotSupported);

        NativeGxAppContext missingState = MakeContext(&table, 8u);
        missingState.requestFileActivation = &RequestStub;
        passed &= Case(ref cases, GuideXosHost.TryCreate(&missingState, 8u,
                out GuideXosHost missingStateHost, out _) &&
            missingStateHost.TryRequestFileActivation("/a.txt"u8) ==
                GuideXosFileActivationResult.NotSupported);

        NativeGxAppContext missingCallback = MakeContext(&table, 8u);
        missingCallback.applicationRequestState = (void*)1;
        passed &= Case(ref cases, GuideXosHost.TryCreate(&missingCallback, 8u,
                out GuideXosHost missingCallbackHost, out _) &&
            missingCallbackHost.TryRequestFileActivation("/a.txt"u8) ==
                GuideXosFileActivationResult.NotSupported);

        NativeGxAppContext wrongSelector = MakeContext(&table, 4u);
        wrongSelector.applicationRequestState = (void*)1;
        wrongSelector.requestFileActivation = &RequestStub;
        passed &= Case(ref cases, GuideXosHost.TryCreate(&wrongSelector, 4u,
                out GuideXosHost wrongSelectorHost, out _) &&
            wrongSelectorHost.TryRequestFileActivation("/a.txt"u8) ==
                GuideXosFileActivationResult.InvalidPath);

        Span<byte> launchBytes = stackalloc byte[96];
        ReadOnlySpan<byte> documentPath = "/system/apps/a.txt"u8;
        documentPath.CopyTo(launchBytes);
        NativeGxAppContext documentContext = MakeContext(&table, 4u);
        fixed (byte* launchPointer = launchBytes)
        {
            documentContext.activationKind = GxAbi.ActivationKindDocument;
            documentContext.launchContext = launchPointer;
            documentContext.launchContextLength = (uint)documentPath.Length;
            GuideXosLaunchContext copied = new();
            passed &= Case(ref cases, copied.TryCopy(&documentContext, 4u,
                out GuideXosLaunchContext documentActivation) &&
                documentActivation.IsDocumentActivation &&
                documentActivation.DocumentPath == "/system/apps/a.txt");

            documentContext.activationKind = GxAbi.ActivationKindNone;
            copied = new GuideXosLaunchContext();
            passed &= Case(ref cases, copied.TryCopy(&documentContext, 4u,
                out GuideXosLaunchContext ordinaryLaunch) &&
                !ordinaryLaunch.IsDocumentActivation &&
                ordinaryLaunch.DocumentPath.Length == 0);

            documentContext.activationKind = GxAbi.ActivationKindDocument;
            documentContext.launchContextLength = 0u;
            passed &= Case(ref cases, !new GuideXosLaunchContext().TryCopy(
                &documentContext, 4u, out _));

            documentContext.launchContextLength = (uint)documentPath.Length;
            documentContext.activationKind = 99u;
            passed &= Case(ref cases, !new GuideXosLaunchContext().TryCopy(
                &documentContext, 4u, out _));

            documentContext.activationKind = GxAbi.ActivationKindDocument;
            documentContext.launchContextLength = GxAbi.MaxLaunchContextBytes;
            launchBytes.Fill((byte)'x');
            passed &= Case(ref cases, new GuideXosLaunchContext().TryCopy(
                &documentContext, 4u,
                out GuideXosLaunchContext maximumActivation) &&
                maximumActivation.Length == 96 &&
                maximumActivation.DocumentPath.Length == 96);

            documentContext.launchContextLength = 97u;
            passed &= Case(ref cases, !new GuideXosLaunchContext().TryCopy(
                &documentContext, 4u, out _));

            documentContext.launchContextLength = 3u;
            launchBytes[0] = (byte)'/';
            launchBytes[1] = 0;
            launchBytes[2] = (byte)'x';
            passed &= Case(ref cases, !new GuideXosLaunchContext().TryCopy(
                &documentContext, 4u, out _));

            documentContext.size = GxAbi.AppContextLaunchPrefixSize;
            documentContext.launchContextLength = (uint)documentPath.Length;
            documentPath.CopyTo(launchBytes);
            passed &= Case(ref cases, new GuideXosLaunchContext().TryCopy(
                &documentContext, 4u,
                out GuideXosLaunchContext legacyContext) &&
                !legacyContext.IsDocumentActivation);
        }

        bool restoredHost = host == null ||
            (originalContext != null && GuideXosHost.TryCreate(
                originalContext, originalSelector,
                out GuideXosHost _, out GuideXosResult restoreResult) &&
                restoreResult == GuideXosResult.Success);
        passed &= Case(ref cases, restoredHost);
        if (host != null)
        {
            Span<byte> line = stackalloc byte[80];
            int position = 0;
            GuideXosText.Append(line, ref position,
                "C164-ACTIVATION-CONTEXT-TESTS cases="u8);
            GuideXosText.AppendUnsigned(line, ref position, (uint)cases);
            GuideXosText.Append(line, ref position,
                passed && cases >= 20 ? " result=PASS"u8 : " result=FAIL"u8);
            host.TryLog(line[..position]);
        }
        return passed && cases >= 20;
    }

    private static NativeGxAppContext MakeContext(
        NativeHostCallTable* table, uint selector)
    {
        NativeGxAppContext context = default;
        context.size = (uint)sizeof(NativeGxAppContext);
        context.apiVersion = GxAbi.ApiVersion;
        context.host = table;
        context.userData = (void*)(nuint)selector;
        return context;
    }

    private static bool Case(ref int cases, bool result)
    {
        cases++;
        return result;
    }

    [UnmanagedCallersOnly]
    private static int RequestStub(
        NativeGxAppContext* context, byte* path, uint pathLength)
    {
        s_stubCalls++;
        s_receivedLength = pathLength;
        uint count = Math.Min(pathLength, (uint)s_receivedPath.Length);
        for (uint index = 0u; index < count; index++)
            s_receivedPath[index] = path[index];
        return s_stubResult;
    }
}
#endif
