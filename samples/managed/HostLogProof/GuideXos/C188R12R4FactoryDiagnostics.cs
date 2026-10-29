#if HOSTLOGPROOF_C188R12R4_FACTORY
using System;
using System.Runtime.InteropServices;
using HostLogProof.Applications;

namespace HostLogProof;

/// <summary>
/// Allocation-free guest trace points around the selector-5 Settings factory.
/// Hosted captures are compiled only into the focused regression build.
/// </summary>
internal static class C188R12R4FactoryDiagnostics
{
#if HOSTLOGPROOF_C188R12R4_FACTORY_TESTS
    private const int TestLineCapacity = 64;
    private static readonly byte[][] s_testLines = new byte[TestLineCapacity][];
    private static int s_testLineCount;
    private static bool s_testCaptureEnabled;

    internal static int TestLineCount => s_testLineCount;
    internal static ReadOnlySpan<byte> TestLine(int index) =>
        index >= 0 && index < s_testLineCount ? s_testLines[index] : default;

    internal static void BeginTestCapture(uint invocationId)
    {
        Array.Clear(s_testLines);
        s_testLineCount = 0;
        s_testCaptureEnabled = true;
        s_invocationId = 0u;
        s_selector = 0u;
        s_operation = 0u;
        s_host = null;
        s_active = false;
    }

    internal static void EndTestCapture()
    {
        s_testCaptureEnabled = false;
        s_active = false;
        s_host = null;
    }
#endif

#if !HOSTLOGPROOF_C188R12R4_FACTORY_TESTS
    [DllImport("__Internal", EntryPoint = "guideXosC188R12R4BeginFactory")]
    private static extern int NativeBeginFactory(uint invocationId,
        uint selector);

    [DllImport("__Internal", EntryPoint = "guideXosC188R12R4SetOperation")]
    private static extern int NativeSetOperation(uint operation);

    [DllImport("__Internal", EntryPoint = "guideXosC188R12R4EndFactory")]
    private static extern int NativeEndFactory(uint success);
#endif

    private static GuideXosHost s_host;
    private static uint s_invocationId;
    private static uint s_selector;
    private static uint s_operation;
    private static bool s_active;

    internal static void BeginFactory(GuideXosHost host,
        uint invocationId, uint selector)
    {
        s_host = host;
        s_invocationId = invocationId;
        s_selector = selector;
        s_operation = 0u;
        s_active = true;
#if !HOSTLOGPROOF_C188R12R4_FACTORY_TESTS
        NativeBeginFactory(invocationId, selector);
#endif
    }

    internal static ManagedSettingsCenter CreateSettingsCenter()
    {
        Emit("type-init-enter"u8, 1u, "begin"u8, 0);
        ManagedSettingsCenter.EnsureFactoryTypeInitializedC188R12R4();
        Emit("type-init-return"u8, 2u, "complete"u8, 0);

        Emit("object-allocation-request"u8, 2u, "new-expression"u8, 0);
        ManagedSettingsCenter settings = new();
        Emit("object-construction-result"u8, 2u, "complete"u8, 0);
        return settings;
    }

    internal static void EmitBaseConstructorEnter() =>
        Emit("base-constructor-enter"u8, 2u, "entered"u8, 0);

    internal static void FactoryReturned(bool created) =>
        Emit("factory-return"u8, 11u,
            created ? "created"u8 : "null"u8, created ? 0 : 1);

    internal static void FactoryFailed() =>
        Emit("factory-failure"u8, s_operation, "exception"u8, 1);

    internal static void EndFactory(bool success)
    {
#if !HOSTLOGPROOF_C188R12R4_FACTORY_TESTS
        NativeEndFactory(success ? 1u : 0u);
#endif
        s_active = false;
        s_host = null;
    }

    internal static void Emit(ReadOnlySpan<byte> stage, uint operation,
        ReadOnlySpan<byte> result, int resultCode)
    {
        if (!s_active) return;
        s_operation = operation;
#if !HOSTLOGPROOF_C188R12R4_FACTORY_TESTS
        NativeSetOperation(operation);
#endif
        if (s_host == null && !CaptureEnabled) return;

        Span<byte> line = stackalloc byte[192];
        int position = 0;
        if (!GuideXosText.Append(line, ref position,
                "C188R12R4_FACTORY invocation="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position,
                s_invocationId) ||
            !GuideXosText.Append(line, ref position, " selector="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position, s_selector) ||
            !GuideXosText.Append(line, ref position, " operation="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position, operation) ||
            !GuideXosText.Append(line, ref position, " stage="u8) ||
            !GuideXosText.Append(line, ref position, stage) ||
            !GuideXosText.Append(line, ref position, " code="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position,
                unchecked((uint)resultCode)) ||
            !GuideXosText.Append(line, ref position, " result="u8) ||
            !GuideXosText.Append(line, ref position, result))
        {
            return;
        }

#if HOSTLOGPROOF_C188R12R4_FACTORY_TESTS
        if (s_testCaptureEnabled && s_testLineCount < TestLineCapacity)
            s_testLines[s_testLineCount++] = line[..position].ToArray();
#endif
        s_host?.TryLog(line[..position]);
    }

    private static bool CaptureEnabled
    {
        get
        {
#if HOSTLOGPROOF_C188R12R4_FACTORY_TESTS
            return s_testCaptureEnabled;
#else
            return false;
#endif
        }
    }
}
#endif
