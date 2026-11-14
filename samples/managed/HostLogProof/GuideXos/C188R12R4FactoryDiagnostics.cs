#if HOSTLOGPROOF_C188R12R4_FACTORY
using System;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;
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

#if !HOSTLOGPROOF_C188R12R4_FACTORY_TESTS && !HOSTLOGPROOF_C188R21_SETTINGS_FACTORY
    [DllImport("__Internal", EntryPoint = "guideXosC188R12R4BeginFactory")]
    private static extern int NativeBeginFactory(uint invocationId,
        uint selector);

    [DllImport("__Internal", EntryPoint = "guideXosC188R12R4SetOperation")]
    private static extern int NativeSetOperation(uint operation);

    [DllImport("__Internal", EntryPoint = "guideXosC188R12R4EndFactory")]
    private static extern int NativeEndFactory(uint success);
#endif

#if HOSTLOGPROOF_C188R21_SETTINGS_FACTORY
    [DllImport("__Internal", EntryPoint = "PalGetMaximumStackBounds",
        CallingConvention = CallingConvention.StdCall)]
    private static extern byte NativeGetMaximumStackBounds(
        out nint stackLow, out nint stackHigh);
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
#if !HOSTLOGPROOF_C188R21_SETTINGS_FACTORY
        NativeBeginFactory(invocationId, selector);
#endif
#endif
#if HOSTLOGPROOF_C188R21_SETTINGS_FACTORY
        EmitR21("factory-entry"u8, invocationId, selector, "reached"u8, 0);
#endif
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    internal static unsafe ManagedSettingsCenter CreateSettingsCenter()
    {
#if HOSTLOGPROOF_C188R21_SETTINGS_FACTORY
        Span<byte> stackMarker = stackalloc byte[1];
        fixed (byte* currentStackPointer = stackMarker)
        {
            LogR21StackBudget((nint)currentStackPointer);
        }
        EmitR21("factory-body"u8, s_invocationId, s_selector,
            "initializing"u8, 0);
#endif
        Emit("type-init-enter"u8, 1u, "begin"u8, 0);
        ManagedSettingsCenter.EnsureFactoryTypeInitializedC188R12R4();
        Emit("type-init-return"u8, 2u, "complete"u8, 0);

        Emit("object-allocation-request"u8, 2u, "new-expression"u8, 0);
        ManagedSettingsCenter settings = new();
        Emit("object-construction-result"u8, 2u, "complete"u8, 0);
#if HOSTLOGPROOF_C188R21_SETTINGS_FACTORY
        EmitR21("factory-result"u8, s_invocationId, s_selector,
            "constructed"u8, 0);
#endif
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
#if !HOSTLOGPROOF_C188R21_SETTINGS_FACTORY
        NativeEndFactory(success ? 1u : 0u);
#endif
#endif
#if HOSTLOGPROOF_C188R21_SETTINGS_FACTORY
        EmitR21("factory-exit"u8, s_invocationId, s_selector,
            success ? "created"u8 : "failed"u8, success ? 0 : 1);
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
#if !HOSTLOGPROOF_C188R21_SETTINGS_FACTORY
        NativeSetOperation(operation);
#endif
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

#if HOSTLOGPROOF_C188R21_SETTINGS_FACTORY
    private static void LogR21StackBudget(nint currentStackPointer)
    {
        if (s_host == null) return;
        byte valid = NativeGetMaximumStackBounds(out nint stackLow,
            out nint stackHigh);
        if (valid == 0 || currentStackPointer < stackLow ||
            currentStackPointer > stackHigh || stackHigh <= stackLow)
        {
            EmitR21("stack-budget"u8, s_invocationId, s_selector,
                "invalid-bounds"u8, 1);
            return;
        }
        nuint remaining = (nuint)(currentStackPointer - stackLow);
        nuint total = (nuint)(stackHigh - stackLow);
        Span<byte> line = stackalloc byte[112];
        int position = 0;
        if (GuideXosText.Append(line, ref position,
                "C188R21-STACK stage=settings-factory-entry remaining="u8) &&
            GuideXosText.AppendUnsigned(line, ref position, (uint)remaining) &&
            GuideXosText.Append(line, ref position, " total="u8) &&
            GuideXosText.AppendUnsigned(line, ref position, (uint)total) &&
            GuideXosText.Append(line, ref position, " valid=1"u8))
        {
            s_host.TryLog(line[..position]);
        }
    }

    private static void EmitR21(ReadOnlySpan<byte> stage, uint invocationId,
        uint selector, ReadOnlySpan<byte> result, int resultCode)
    {
        if (s_host == null) return;
        Span<byte> line = stackalloc byte[144];
        int position = 0;
        if (GuideXosText.Append(line, ref position,
                "C188R21-FACTORY invocation="u8) &&
            GuideXosText.AppendUnsigned(line, ref position, invocationId) &&
            GuideXosText.Append(line, ref position, " selector="u8) &&
            GuideXosText.AppendUnsigned(line, ref position, selector) &&
            GuideXosText.Append(line, ref position, " stage="u8) &&
            GuideXosText.Append(line, ref position, stage) &&
            GuideXosText.Append(line, ref position, " result="u8) &&
            GuideXosText.Append(line, ref position, result) &&
            GuideXosText.Append(line, ref position, " code="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                unchecked((uint)resultCode)))
        {
            s_host.TryLog(line[..position]);
        }
    }
#endif

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
