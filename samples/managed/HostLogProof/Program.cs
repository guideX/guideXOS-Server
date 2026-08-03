using System;
using System.Runtime.InteropServices;

namespace HostLogProof;

public static unsafe class Program
{
#if HOSTLOGPROOF_REPEATED_ALLOCATION
    [DllImport("__Internal", EntryPoint = "guideXosManagedAllocationCanFit")]
    private static extern int GuideXosManagedAllocationCanFit(uint length);

    [DllImport("__Internal", EntryPoint = "guideXosManagedAllocationValidateObject")]
    private static extern int GuideXosManagedAllocationValidateObject(
        nint arrayObject,
        uint length,
        uint sequence,
        uint zeroInitialized,
        uint patternValid);

    [DllImport("__Internal", EntryPoint = "guideXosManagedAllocationRecordFailure")]
    private static extern int GuideXosManagedAllocationRecordFailure(uint reason);

    [DllImport("__Internal", EntryPoint = "guideXosManagedAllocationReport")]
    private static extern int GuideXosManagedAllocationReport(NativeGxAppContext* context, uint status);
#endif

#if HOSTLOGPROOF_FIRST_REFILL_ALLOCATION || HOSTLOGPROOF_SEGMENT_BOUNDARY_ALLOCATION || HOSTLOGPROOF_SEGMENT_TRANSITION_ALLOCATION || HOSTLOGPROOF_FIRST_COLLECTION_BOUNDARY_ALLOCATION
    [DllImport("__Internal", EntryPoint = "guideXosManagedAllocationValidateObject")]
    private static extern int GuideXosManagedAllocationValidateObject(
        nint arrayObject,
        uint length,
        uint sequence,
        uint zeroByteCount,
        uint patternValid);

#if !HOSTLOGPROOF_FIRST_COLLECTION_BOUNDARY_ALLOCATION
    [DllImport("__Internal", EntryPoint = "guideXosManagedAllocationGetLoopStatus")]
    private static extern int GuideXosManagedAllocationGetLoopStatus();
#endif

    [DllImport("__Internal", EntryPoint = "guideXosManagedAllocationGetHardLimit")]
    private static extern uint GuideXosManagedAllocationGetHardLimit();

#if HOSTLOGPROOF_FIRST_COLLECTION_BOUNDARY_ALLOCATION
    [DllImport("__Internal", EntryPoint = "guideXosManagedAllocationRecordSentinelValidation")]
    private static extern int GuideXosManagedAllocationRecordSentinelValidation(
        uint checkedCount, uint failureCount);
#endif
#endif

#if !HOSTLOGPROOF_FIRST_REAL_ALLOCATION && !HOSTLOGPROOF_FIRST_REFILL_ALLOCATION && !HOSTLOGPROOF_SEGMENT_BOUNDARY_ALLOCATION && !HOSTLOGPROOF_SEGMENT_TRANSITION_ALLOCATION && !HOSTLOGPROOF_FIRST_COLLECTION_BOUNDARY_ALLOCATION
    [DllImport("__Internal", EntryPoint = "guideXosManagedArrayHostLog")]
    private static extern int GuideXosManagedArrayHostLog(NativeGxAppContext* context, nint arrayObject);
#endif

    public static void Main()
    {
    }

    [UnmanagedCallersOnly(EntryPoint = "ManagedMain")]
    public static int ManagedMain(NativeGxAppContext* ctx)
    {
        if (ctx == null)
        {
            return GxAbi.ErrorInvalidArgument;
        }

        if (ctx->size < (uint)sizeof(NativeGxAppContext))
        {
            return GxAbi.ErrorInvalidArgument;
        }

        if (ctx->apiVersion != GxAbi.ApiVersion)
        {
            return GxAbi.ErrorUnsupported;
        }

#if !HOSTLOGPROOF_FIRST_REAL_ALLOCATION && !HOSTLOGPROOF_FIRST_REFILL_ALLOCATION && !HOSTLOGPROOF_SEGMENT_BOUNDARY_ALLOCATION && !HOSTLOGPROOF_SEGMENT_TRANSITION_ALLOCATION && !HOSTLOGPROOF_FIRST_COLLECTION_BOUNDARY_ALLOCATION
        if (ctx->host == null || ctx->host->log == null)
        {
            return GxAbi.ErrorInvalidArgument;
        }
#endif

#if HOSTLOGPROOF_FIRST_REAL_ALLOCATION
        // This is the complete managed body for the first real-GC experiment.
        // It intentionally contains one object allocation and no managed
        // diagnostics, strings, delegates, exceptions, or additional objects.
        byte[] value = new byte[24];
        int zeroByteCount = 0;
        for (int i = 0; i < value.Length; i++)
        {
            if (value[i] == 0) zeroByteCount++;
        }

        bool patternVerified = true;
        for (int i = 0; i < value.Length; i++)
        {
            value[i] = (byte)(0x40 + i);
        }
        for (int i = 0; i < value.Length; i++)
        {
            if (value[i] != (byte)(0x40 + i)) patternVerified = false;
        }

        GC.KeepAlive(value);
        return (zeroByteCount << 8) | (patternVerified ? 1 : 0);
#elif HOSTLOGPROOF_FIRST_REFILL_ALLOCATION
        // The first allocation establishes the context.  Subsequent calls are
        // bounded by the native context geometry and stop immediately after
        // the first later refill returns.  No managed collection, retained
        // array set, diagnostic object, string, delegate, or exception is
        // used by this body.
        const int arrayLength = 256;
        byte[] current = new byte[arrayLength];
        uint iteration = 0u;
        uint zeroByteCount = CountZeroBytes(current);
        WriteIdentifyingPattern(current, iteration);
        bool patternValid = HasIdentifyingPattern(current, iteration);
        nint objectReference = System.Runtime.CompilerServices.Unsafe.As<byte[], nint>(ref current);
        int validationResult = GuideXosManagedAllocationValidateObject(
            objectReference, arrayLength, iteration, zeroByteCount,
            patternValid ? 1u : 0u);
        int loopStatus = GuideXosManagedAllocationGetLoopStatus();
        GC.KeepAlive(current);
        if (validationResult != 0 || loopStatus != 1)
        {
            return GxAbi.ErrorInvalidArgument;
        }

        uint hardLimit = GuideXosManagedAllocationGetHardLimit();
        if (hardLimit < 2u || hardLimit > 1024u)
        {
            return GxAbi.ErrorInvalidArgument;
        }

        for (iteration = 1u; iteration < hardLimit; iteration++)
        {
            current = new byte[arrayLength];
            zeroByteCount = CountZeroBytes(current);
            WriteIdentifyingPattern(current, iteration);
            patternValid = HasIdentifyingPattern(current, iteration);
            objectReference = System.Runtime.CompilerServices.Unsafe.As<byte[], nint>(ref current);
            validationResult = GuideXosManagedAllocationValidateObject(
                objectReference, arrayLength, iteration, zeroByteCount,
                patternValid ? 1u : 0u);
            loopStatus = GuideXosManagedAllocationGetLoopStatus();
            GC.KeepAlive(current);
            if (validationResult != 0)
            {
                return GxAbi.ErrorInvalidArgument;
            }
            if (loopStatus == 2)
            {
                return 0;
            }
            if (loopStatus != 1)
            {
                return GxAbi.ErrorInvalidArgument;
            }
        }

        return GxAbi.ErrorInvalidArgument;
#elif HOSTLOGPROOF_FIRST_COLLECTION_BOUNDARY_ALLOCATION
        // Keep four early objects live and validate their deterministic
        // contents before each next allocation. The native safe stop is
        // reached by the next allocation request; this body never returns
        // from an allocation that the GC could not complete.
        const int arrayLength = 4096;
        byte[] sentinel0 = null;
        byte[] sentinel1 = null;
        byte[] sentinel2 = null;
        byte[] sentinel3 = null;
        byte[] current = null;
        uint hardLimit = GuideXosManagedAllocationGetHardLimit();
        if (hardLimit < 8u || hardLimit > 1024u)
        {
            return GxAbi.ErrorInvalidArgument;
        }

        for (uint iteration = 0u; iteration < hardLimit; iteration++)
        {
            bool sentinelsValid = ValidateSample(sentinel0, 0u) &&
                ValidateSample(sentinel1, 1u) &&
                ValidateSample(sentinel2, 2u) &&
                ValidateSample(sentinel3, 3u);
            GuideXosManagedAllocationRecordSentinelValidation(
                iteration == 0u ? 0u : 4u, sentinelsValid ? 0u : 1u);
            if (!sentinelsValid)
            {
                return GxAbi.ErrorInvalidArgument;
            }

            current = new byte[arrayLength];
            uint zeroByteCount = CountZeroBytes(current);
            WriteIdentifyingPattern(current, iteration);
            bool patternValid = HasIdentifyingPattern(current, iteration);
            nint objectReference = System.Runtime.CompilerServices.Unsafe.As<byte[], nint>(ref current);
            int validationResult = GuideXosManagedAllocationValidateObject(
                objectReference, arrayLength, iteration, zeroByteCount,
                patternValid ? 1u : 0u);
            if (validationResult != 0)
            {
                return GxAbi.ErrorInvalidArgument;
            }

            if (iteration == 0u) sentinel0 = current;
            else if (iteration == 1u) sentinel1 = current;
            else if (iteration == 2u) sentinel2 = current;
            else if (iteration == 3u) sentinel3 = current;
            GC.KeepAlive(sentinel0);
            GC.KeepAlive(sentinel1);
            GC.KeepAlive(sentinel2);
            GC.KeepAlive(sentinel3);
            GC.KeepAlive(current);
        }

        return GxAbi.ErrorInvalidArgument;
#elif HOSTLOGPROOF_SEGMENT_BOUNDARY_ALLOCATION || HOSTLOGPROOF_SEGMENT_TRANSITION_ALLOCATION
        // Allocate one fixed-size primitive array at a time.  The native
        // diagnostic hook stops the loop at the first measured boundary or
        // the source-backed safe hard limit; no managed
        // collection, retained array set, string, delegate, exception, or
        // second allocation is introduced after that boundary.
        const int arrayLength = 4096;
        byte[] current = new byte[arrayLength];
        uint iteration = 0u;
        uint zeroByteCount = CountZeroBytes(current);
        WriteIdentifyingPattern(current, iteration);
        bool patternValid = HasIdentifyingPattern(current, iteration);
        nint objectReference = System.Runtime.CompilerServices.Unsafe.As<byte[], nint>(ref current);
        int validationResult = GuideXosManagedAllocationValidateObject(
            objectReference, arrayLength, iteration, zeroByteCount,
            patternValid ? 1u : 0u);
        int loopStatus = GuideXosManagedAllocationGetLoopStatus();
        GC.KeepAlive(current);
        if (validationResult != 0 || loopStatus < 0)
        {
            return GxAbi.ErrorInvalidArgument;
        }
        if (loopStatus == 2)
        {
            return 0;
        }

        uint hardLimit = GuideXosManagedAllocationGetHardLimit();
        if (hardLimit < 2u || hardLimit > 2048u)
        {
            return GxAbi.ErrorInvalidArgument;
        }

        for (iteration = 1u; iteration < hardLimit; iteration++)
        {
            current = new byte[arrayLength];
            zeroByteCount = CountZeroBytes(current);
            WriteIdentifyingPattern(current, iteration);
            patternValid = HasIdentifyingPattern(current, iteration);
            objectReference = System.Runtime.CompilerServices.Unsafe.As<byte[], nint>(ref current);
            validationResult = GuideXosManagedAllocationValidateObject(
                objectReference, arrayLength, iteration, zeroByteCount,
                patternValid ? 1u : 0u);
            loopStatus = GuideXosManagedAllocationGetLoopStatus();
            GC.KeepAlive(current);
            if (validationResult != 0 || loopStatus < 0)
            {
                return GxAbi.ErrorInvalidArgument;
            }
            if (loopStatus == 2)
            {
                return 0;
            }
            if (loopStatus == 3)
            {
                return GxAbi.ErrorUnsupported;
            }
            if (loopStatus != 1)
            {
                return GxAbi.ErrorInvalidArgument;
            }
        }

        return GxAbi.ErrorUnsupported;
#elif HOSTLOGPROOF_ALLOCATING
#if HOSTLOGPROOF_REPEATED_ALLOCATION
        const int arrayLength = 256;
        const int maximumBoundedAllocations = 512;
        byte[] sample0 = null;
        byte[] sample1 = null;
        byte[] sample2 = null;
        byte[] sample3 = null;
        uint completed = 0;

        while (completed < maximumBoundedAllocations)
        {
            int fit = GuideXosManagedAllocationCanFit((uint)arrayLength);
            if (fit == 0)
            {
                bool samplesValid = ValidateSample(sample0, 0) &&
                    ValidateSample(sample1, 1) &&
                    ValidateSample(sample2, 2) &&
                    ValidateSample(sample3, 3);
                if (!samplesValid)
                {
                    GuideXosManagedAllocationRecordFailure(1);
                    int failedResult = GuideXosManagedAllocationReport(ctx, 1);
                    GC.KeepAlive(sample0);
                    GC.KeepAlive(sample1);
                    GC.KeepAlive(sample2);
                    GC.KeepAlive(sample3);
                    return failedResult == 0 ? GxAbi.ErrorInvalidArgument : failedResult;
                }

                int oomResult = GuideXosManagedAllocationReport(ctx, 0);
                GC.KeepAlive(sample0);
                GC.KeepAlive(sample1);
                GC.KeepAlive(sample2);
                GC.KeepAlive(sample3);
                return oomResult;
            }

            if (fit < 0)
            {
                int failedResult = GuideXosManagedAllocationReport(ctx, 1);
                return failedResult == 0 ? GxAbi.ErrorInvalidArgument : failedResult;
            }

            byte[] current = new byte[arrayLength];
            bool zeroInitialized = IsZeroInitialized(current);
            WriteIdentifyingPattern(current, completed);
            bool patternValid = HasIdentifyingPattern(current, completed);
            nint objectReference = System.Runtime.CompilerServices.Unsafe.As<byte[], nint>(ref current);
            int objectResult = GuideXosManagedAllocationValidateObject(
                objectReference,
                (uint)arrayLength,
                completed,
                zeroInitialized ? 1u : 0u,
                patternValid ? 1u : 0u);
            bool samplesBeforeNextAllocation = ValidateSample(sample0, 0) &&
                ValidateSample(sample1, 1) &&
                ValidateSample(sample2, 2) &&
                ValidateSample(sample3, 3);
            if (objectResult != 0 || !samplesBeforeNextAllocation)
            {
                GuideXosManagedAllocationRecordFailure(2);
                int failedResult = GuideXosManagedAllocationReport(ctx, 1);
                GC.KeepAlive(current);
                GC.KeepAlive(sample0);
                GC.KeepAlive(sample1);
                GC.KeepAlive(sample2);
                GC.KeepAlive(sample3);
                return failedResult == 0 ? GxAbi.ErrorInvalidArgument : failedResult;
            }

            if (completed == 0) sample0 = current;
            else if (completed == 1) sample1 = current;
            else if (completed == 2) sample2 = current;
            else if (completed == 3) sample3 = current;
            completed++;
        }

        GuideXosManagedAllocationRecordFailure(3);
        int boundedResult = GuideXosManagedAllocationReport(ctx, 1);
        GC.KeepAlive(sample0);
        GC.KeepAlive(sample1);
        GC.KeepAlive(sample2);
        GC.KeepAlive(sample3);
        return boundedResult == 0 ? GxAbi.ErrorInvalidArgument : boundedResult;
#else
        // The existing guideXOS host ABI accepts a NUL-terminated byte pointer,
        // so the managed array contains 23 UTF-8 message bytes and one managed
        // terminator. The host callback is synchronous and does not retain it.
        byte[] messageBuffer = new byte[]
        {
            (byte)'H', (byte)'e', (byte)'l', (byte)'l', (byte)'o', (byte)' ',
            (byte)'f', (byte)'r', (byte)'o', (byte)'m', (byte)' ',
            (byte)'m', (byte)'a', (byte)'n', (byte)'a', (byte)'g', (byte)'e',
            (byte)'d', (byte)' ', (byte)'h', (byte)'e', (byte)'a', (byte)'p',
            0
        };

        // The guideXOS runtime-pack helper keeps object-layout knowledge inside
        // the runtime layer and calls the existing synchronous host ABI with
        // array data. The raw object reference is valid because this experiment
        // disables collection and the local reference remains live through the
        // helper call.
        nint arrayObject = System.Runtime.CompilerServices.Unsafe.As<byte[], nint>(ref messageBuffer);
        int result = GuideXosManagedArrayHostLog(ctx, arrayObject);
        GC.KeepAlive(messageBuffer);
        return result;
#endif
#else
        ReadOnlySpan<byte> messageUtf8 = "Hello from managed guideXOS code"u8;
        Span<byte> messageBuffer = stackalloc byte[messageUtf8.Length + 1];
        messageUtf8.CopyTo(messageBuffer);
        messageBuffer[messageUtf8.Length] = 0;

        fixed (byte* message = messageBuffer)
        {
            return ctx->host->log(ctx, message);
        }
#endif
    }

#if HOSTLOGPROOF_FIRST_REFILL_ALLOCATION || HOSTLOGPROOF_SEGMENT_BOUNDARY_ALLOCATION || HOSTLOGPROOF_SEGMENT_TRANSITION_ALLOCATION || HOSTLOGPROOF_FIRST_COLLECTION_BOUNDARY_ALLOCATION || HOSTLOGPROOF_REPEATED_ALLOCATION
    private static uint CountZeroBytes(byte[] buffer)
    {
        uint count = 0u;
        for (int i = 0; i < buffer.Length; i++)
        {
            if (buffer[i] == 0) count++;
        }
        return count;
    }

    private static bool IsZeroInitialized(byte[] buffer)
    {
        for (int i = 0; i < buffer.Length; i++)
        {
            if (buffer[i] != 0) return false;
        }
        return true;
    }

    private static void WriteIdentifyingPattern(byte[] buffer, uint sequence)
    {
        buffer[0] = (byte)sequence;
        buffer[1] = (byte)(sequence >> 8);
        buffer[2] = (byte)(sequence >> 16);
        buffer[3] = (byte)(sequence >> 24);
        for (int i = 4; i < buffer.Length; i++)
        {
            buffer[i] = (byte)(((uint)i * 17u + sequence * 31u) & 0xFFu);
        }
    }

    private static bool HasIdentifyingPattern(byte[] buffer, uint sequence)
    {
        if (buffer.Length < 4) return false;
        if (buffer[0] != (byte)sequence ||
            buffer[1] != (byte)(sequence >> 8) ||
            buffer[2] != (byte)(sequence >> 16) ||
            buffer[3] != (byte)(sequence >> 24)) return false;
        for (int i = 4; i < buffer.Length; i++)
        {
            if (buffer[i] != (byte)(((uint)i * 17u + sequence * 31u) & 0xFFu)) return false;
        }
        return true;
    }

    private static bool ValidateSample(byte[] buffer, uint sequence)
    {
        return buffer == null || HasIdentifyingPattern(buffer, sequence);
    }
#endif
}
