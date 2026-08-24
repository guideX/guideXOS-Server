using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace HostLogProof;

public static unsafe class Program
{
#if HOSTLOGPROOF_THREAD_STATIC_PRIMITIVE || HOSTLOGPROOF_THREAD_STATIC_COMBINED
    [ThreadStatic]
    private static int s_threadStaticInt;
#endif

#if HOSTLOGPROOF_THREAD_STATIC_REFERENCE || HOSTLOGPROOF_THREAD_STATIC_COMBINED
    [ThreadStatic]
    private static byte[]? s_threadStaticRef;
#endif

#if HOSTLOGPROOF_THREAD_STATIC_PRIMITIVE || HOSTLOGPROOF_THREAD_STATIC_REFERENCE || HOSTLOGPROOF_THREAD_STATIC_COMBINED
    [DllImport("__Internal", EntryPoint = "guideXosManagedThreadStaticProofRecord")]
    private static extern int GuideXosManagedThreadStaticProofRecord(
        uint marker,
        uint kind,
        nint assigned,
        nint readback,
        uint expected,
        uint actual,
        uint identityMatch,
        uint objectValid);
#endif

#if HOSTLOGPROOF_FIRST_NON_NULL_ROOT
    [ThreadStatic]
    private static byte[]? s_gcProofThreadRoot;
#endif

#if HOSTLOGPROOF_SHORT_WEAK_LIFETIME
    [DllImport("__Internal", EntryPoint = "guideXosNativeAotC011EC33WeakHandleAllocated")]
    private static extern int GuideXosNativeAotC011EC33WeakHandleAllocated(
        nint target,
        nint targetType,
        nint weakHandleSlot,
        uint handleType);

    [DllImport("__Internal", EntryPoint = "guideXosNativeAotC011EC33GetCompletedCollections")]
    private static extern int GuideXosNativeAotC011EC33GetCompletedCollections();

    [DllImport("__Internal", EntryPoint = "guideXosNativeAotC011EC33LifetimeBoundaryReturned")]
    private static extern int GuideXosNativeAotC011EC33LifetimeBoundaryReturned(
        nint target,
        nint targetType,
        nint weakHandleSlot);

#if HOSTLOGPROOF_C011EC37
    [DllImport("__Internal", EntryPoint = "guideXosNativeAotC011EC37ManagedCheckpoint")]
    private static extern int GuideXosNativeAotC011EC37ManagedCheckpoint(
        uint checkpoint,
        nint weakHandleSlot);

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int RunC011EC37ManagedCheckpoint()
    {
        return GuideXosNativeAotC011EC37ManagedCheckpoint(1u, 0);
    }
#endif

#if HOSTLOGPROOF_C011EC39
    [DllImport("__Internal", EntryPoint = "guideXosNativeAotC011EC39Finish")]
    private static extern int GuideXosNativeAotC011EC39Finish();

#if HOSTLOGPROOF_C011EC39_C38_VARIANT
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int RunC011EC39C38VariantWorkload()
    {
        // C38's eight ordinary post-Collection-2 byte[64] allocations are
        // retained as a workload delta only.  C39 does not call the C38
        // allocator observer and does not gate or force any allocation path.
        for (int ordinal = 0; ordinal < 8; ordinal++)
        {
            byte[] value = new byte[64];
            value[0] = (byte)ordinal;
            GC.KeepAlive(value);
        }
        return 0;
    }
#endif
#endif

#if HOSTLOGPROOF_C011EC38
    [DllImport("__Internal", EntryPoint = "guideXosNativeAotC011EC38BeforeAllocation")]
    private static extern int GuideXosNativeAotC011EC38BeforeAllocation(
        uint ordinal,
        uint payloadSize);

    [DllImport("__Internal", EntryPoint = "guideXosNativeAotC011EC38AfterAllocation")]
    private static extern int GuideXosNativeAotC011EC38AfterAllocation(
        nint objectAddress);

    [DllImport("__Internal", EntryPoint = "guideXosNativeAotC011EC38Finish")]
    private static extern int GuideXosNativeAotC011EC38Finish();

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int RunC011EC38OneAllocation(uint ordinal)
    {
        if (GuideXosNativeAotC011EC38BeforeAllocation(ordinal, 64u) != 0)
        {
            return -1;
        }

        byte[] value = new byte[64];
        nint objectAddress = Unsafe.As<byte[], nint>(ref value);
        int status = GuideXosNativeAotC011EC38AfterAllocation(objectAddress);
        GC.KeepAlive(value);
        return status;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int RunC011EC38AllocationReuse()
    {
        // Eight fixed calls are the complete bounded reuse window. Each call
        // uses the ordinary managed new-array path and retains no diagnostic
        // object or allocator handle.
        for (uint ordinal = 0u; ordinal < 8u; ordinal++)
        {
            if (RunC011EC38OneAllocation(ordinal) != 0)
            {
                return -1;
            }
        }
        return GuideXosNativeAotC011EC38Finish();
    }
#endif

#if HOSTLOGPROOF_C011EC40
    [DllImport("__Internal", EntryPoint = "guideXosNativeAotC011EC40BeforeAllocation")]
    private static extern int GuideXosNativeAotC011EC40BeforeAllocation(
        uint ordinal,
        uint payloadSize);

    [DllImport("__Internal", EntryPoint = "guideXosNativeAotC011EC40AfterAllocation")]
    private static extern int GuideXosNativeAotC011EC40AfterAllocation(
        nint objectAddress);

    [DllImport("__Internal", EntryPoint = "guideXosNativeAotC011EC40Finish")]
    private static extern int GuideXosNativeAotC011EC40Finish();

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int RunC011EC40AllocationReuse()
    {
        for (uint ordinal = 0u; ordinal < 8u; ordinal++)
        {
            int before = GuideXosNativeAotC011EC40BeforeAllocation(ordinal, 64u);
            if (before < 0)
            {
                return -1;
            }
            if (before > 0)
            {
                break;
            }

            byte[] value = new byte[64];
            nint objectAddress = Unsafe.As<byte[], nint>(ref value);
            if (GuideXosNativeAotC011EC40AfterAllocation(objectAddress) != 0)
            {
                return -1;
            }
            value[0] = (byte)ordinal;
            GC.KeepAlive(value);
        }
        return GuideXosNativeAotC011EC40Finish();
    }
#endif

    private readonly struct ShortWeakLifetimeSetup
    {
        internal readonly nint Target;
        internal readonly nint TargetType;
        internal readonly nint WeakHandleSlot;
        internal readonly int Status;

        internal ShortWeakLifetimeSetup(
            nint target,
            nint targetType,
            nint weakHandleSlot,
            int status)
        {
            Target = target;
            TargetType = targetType;
            WeakHandleSlot = weakHandleSlot;
            Status = status;
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static ShortWeakLifetimeSetup CreateAndRunLiveCollection1()
    {
        byte[] target = new byte[64];
        for (int i = 0; i < target.Length; i++)
        {
            target[i] = (byte)(0xE0 + i);
        }

        nint targetAddress = Unsafe.As<byte[], nint>(ref target);
        nint targetType = typeof(byte[]).TypeHandle.Value;
        GCHandle weakHandle = GCHandle.Alloc(target, GCHandleType.Weak);
        nint weakHandleSlot = GCHandle.ToIntPtr(weakHandle);
        int status = GuideXosNativeAotC011EC33WeakHandleAllocated(
            targetAddress, targetType, weakHandleSlot,
            (uint)GCHandleType.Weak);

        // The production allocation loop is the authentic Collection-1
        // trigger in this locked bring-up. The validation return code ends
        // the loop on the allocation that resumes after Collection 1, so the
        // next managed action is the natural helper return.
        const int arrayLength = 4096;
        uint hardLimit = GuideXosManagedAllocationGetHardLimit();
        if (hardLimit < 8u || hardLimit > 1024u)
        {
            return new ShortWeakLifetimeSetup(
                targetAddress, targetType, weakHandleSlot, -1);
        }
        for (uint iteration = 0u; iteration < hardLimit; iteration++)
        {
            byte[] current = new byte[arrayLength];
            // This is a scalar native observer. Before Collection 1 it is a
            // no-op; on the first managed instruction after RestartEE it
            // records the C35 resume boundary. The following validator then
            // returns the C36 boundary code and this helper returns naturally.
            _ = GuideXosNativeAotC011EC33GetCompletedCollections();
            uint zeroByteCount = CountZeroBytes(current);
            WriteIdentifyingPattern(current, iteration);
            bool patternValid = HasIdentifyingPattern(current, iteration);
            nint objectReference = Unsafe.As<byte[], nint>(ref current);
            int validationStatus = GuideXosManagedAllocationValidateObject(
                objectReference, arrayLength, iteration, zeroByteCount,
                patternValid ? 1u : 0u);
            if (validationStatus < 0)
            {
                return new ShortWeakLifetimeSetup(
                    targetAddress, targetType, weakHandleSlot, -1);
            }
            GC.KeepAlive(target);
            GC.KeepAlive(current);
            if (validationStatus > 0)
            {
                break;
            }
        }

        return new ShortWeakLifetimeSetup(
            targetAddress, targetType, weakHandleSlot, status);
    }

#endif

#if HOSTLOGPROOF_SHORT_WEAK_LIVE
    [DllImport("__Internal", EntryPoint = "guideXosNativeAotC011EC31WeakHandleAllocated")]
    private static extern int GuideXosNativeAotC011EC31WeakHandleAllocated(
        nint target,
        nint strongRootSlot,
        nint strongRootValue,
        nint weakHandleSlot,
        uint handleType);

    [DllImport("__Internal", EntryPoint = "guideXosNativeAotC011EC31StrongRootRecorded")]
    private static extern int GuideXosNativeAotC011EC31StrongRootRecorded(
        nint strongRootSlot,
        nint strongRootValue);
#endif

#if HOSTLOGPROOF_SHORT_WEAK_DEAD
    [DllImport("__Internal", EntryPoint = "guideXosNativeAotC011EC32WeakHandleAllocated")]
    private static extern int GuideXosNativeAotC011EC32WeakHandleAllocated(
        nint target,
        nint targetType,
        nint weakHandleSlot,
        uint handleType);

    [DllImport("__Internal", EntryPoint = "guideXosNativeAotC011EC32HelperReturned")]
    private static extern int GuideXosNativeAotC011EC32HelperReturned(
        nint target,
        nint targetType,
        nint weakHandleSlot);

    private readonly struct ShortWeakDeadSetup
    {
        internal readonly nint Target;
        internal readonly nint TargetType;
        internal readonly nint WeakHandleSlot;
        internal readonly int Status;

        internal ShortWeakDeadSetup(
            nint target,
            nint targetType,
            nint weakHandleSlot,
            int status)
        {
            Target = target;
            TargetType = targetType;
            WeakHandleSlot = weakHandleSlot;
            Status = status;
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static ShortWeakDeadSetup CreateShortWeakDeadSetup()
    {
        byte[] target = new byte[64];
        for (int i = 0; i < target.Length; i++)
        {
            target[i] = (byte)(0xD0 + i);
        }

        nint targetAddress = Unsafe.As<byte[], nint>(ref target);
        nint targetType = typeof(byte[]).TypeHandle.Value;
        GCHandle weakHandle = GCHandle.Alloc(target, GCHandleType.Weak);
        nint weakHandleSlot = GCHandle.ToIntPtr(weakHandle);
        int status = GuideXosNativeAotC011EC32WeakHandleAllocated(
            targetAddress, targetType, weakHandleSlot,
            (uint)GCHandleType.Weak);

        // Keep the target alive through the production allocation and proof
        // callback. The NoInlining helper boundary is what removes this
        // managed reference from the active frames before collection.
        GC.KeepAlive(target);
        return new ShortWeakDeadSetup(
            targetAddress, targetType, weakHandleSlot, status);
    }
#endif

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

#if HOSTLOGPROOF_FIRST_NON_NULL_ROOT
    [DllImport("__Internal", EntryPoint = "guideXosManagedThreadStaticProofAssigned")]
    private static extern int GuideXosManagedThreadStaticProofAssigned(
        nint arrayObject, uint sentinelOrdinal, uint objectSize, uint patternValid);

    [DllImport("__Internal", EntryPoint = "guideXosManagedThreadStaticProofReadback")]
    private static extern int GuideXosManagedThreadStaticProofReadback(
        nint arrayObject, uint sentinelOrdinal, uint exactMatch);
#endif
#endif
#endif

#if !HOSTLOGPROOF_FIRST_REAL_ALLOCATION && !HOSTLOGPROOF_FIRST_REFILL_ALLOCATION && !HOSTLOGPROOF_SEGMENT_BOUNDARY_ALLOCATION && !HOSTLOGPROOF_SEGMENT_TRANSITION_ALLOCATION && !HOSTLOGPROOF_FIRST_COLLECTION_BOUNDARY_ALLOCATION && !HOSTLOGPROOF_THREAD_STATIC_PRIMITIVE && !HOSTLOGPROOF_THREAD_STATIC_REFERENCE && !HOSTLOGPROOF_THREAD_STATIC_COMBINED
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

#if !HOSTLOGPROOF_FIRST_REAL_ALLOCATION && !HOSTLOGPROOF_FIRST_REFILL_ALLOCATION && !HOSTLOGPROOF_SEGMENT_BOUNDARY_ALLOCATION && !HOSTLOGPROOF_SEGMENT_TRANSITION_ALLOCATION && !HOSTLOGPROOF_FIRST_COLLECTION_BOUNDARY_ALLOCATION && !HOSTLOGPROOF_THREAD_STATIC_PRIMITIVE && !HOSTLOGPROOF_THREAD_STATIC_REFERENCE && !HOSTLOGPROOF_THREAD_STATIC_COMBINED
        if (ctx->host == null || ctx->host->log == null)
        {
            return GxAbi.ErrorInvalidArgument;
        }
#endif

#if HOSTLOGPROOF_THREAD_STATIC_PRIMITIVE || HOSTLOGPROOF_THREAD_STATIC_REFERENCE || HOSTLOGPROOF_THREAD_STATIC_COMBINED
#if HOSTLOGPROOF_THREAD_STATIC_PRIMITIVE || HOSTLOGPROOF_THREAD_STATIC_COMBINED
        int primitiveInitial = s_threadStaticInt;
        if (GuideXosManagedThreadStaticProofRecord(
                0x7A510001u, 1u, 0, 0,
                unchecked((uint)primitiveInitial), 0u, 0u, 0u) != 0 ||
            primitiveInitial != 0)
        {
            return GxAbi.ErrorInvalidArgument;
        }

        const int primitiveAssigned = 0x13572468;
        s_threadStaticInt = primitiveAssigned;
        int primitiveReadback = s_threadStaticInt;
        if (GuideXosManagedThreadStaticProofRecord(
                0x7A510002u, 1u, 0,
                primitiveReadback,
                unchecked((uint)primitiveInitial),
                unchecked((uint)primitiveAssigned),
                primitiveReadback == primitiveAssigned ? 1u : 0u,
                1u) != 0 || primitiveReadback != primitiveAssigned)
        {
            return GxAbi.ErrorInvalidArgument;
        }
#endif

#if HOSTLOGPROOF_THREAD_STATIC_REFERENCE || HOSTLOGPROOF_THREAD_STATIC_COMBINED
        if (GuideXosManagedThreadStaticProofRecord(
                0x7A510003u, 2u, 0, 0, 0u, 0u, 0u, 0u) != 0)
        {
            return GxAbi.ErrorInvalidArgument;
        }

        byte[]? initialReference = s_threadStaticRef;
        if (initialReference != null)
        {
            return GxAbi.ErrorInvalidArgument;
        }

        byte[] assignedReference = new byte[16];
        for (int i = 0; i < assignedReference.Length; i++)
        {
            assignedReference[i] = (byte)(0xA0 + i);
        }
        s_threadStaticRef = assignedReference;
        byte[]? readbackReference = s_threadStaticRef;
        nint assignedAddress = System.Runtime.CompilerServices.Unsafe.As<byte[], nint>(
            ref assignedReference);
        nint readbackAddress = readbackReference == null
            ? 0
            : System.Runtime.CompilerServices.Unsafe.As<byte[], nint>(
                ref readbackReference);
        bool objectValid = readbackReference != null;
        if (objectValid)
        {
            for (int i = 0; i < readbackReference!.Length; i++)
            {
                if (readbackReference[i] != (byte)(0xA0 + i))
                {
                    objectValid = false;
                    break;
                }
            }
        }
        if (GuideXosManagedThreadStaticProofRecord(
                0x7A510004u, 2u, assignedAddress, readbackAddress,
                0u, 0u, assignedAddress == readbackAddress ? 1u : 0u,
                objectValid ? 1u : 0u) != 0 ||
            assignedAddress == 0 || assignedAddress != readbackAddress || !objectValid)
        {
            return GxAbi.ErrorInvalidArgument;
        }

        GC.KeepAlive(assignedReference);
        GC.KeepAlive(readbackReference);
        return 0x7A510004;
#else
        return 0x7A510002;
#endif
#elif HOSTLOGPROOF_FIRST_REAL_ALLOCATION
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
#elif HOSTLOGPROOF_FIRST_NON_NULL_ROOT && !HOSTLOGPROOF_SHORT_WEAK_LIVE
        // This is the non-null thread-static root proof. It deliberately uses
        // the same four retained sentinels and allocation boundary as the
        // established first-collection workload. The selected first sentinel
        // is assigned once through ordinary managed [ThreadStatic] semantics;
        // the native hooks record only the managed assignment and one managed
        // readback immediately before the next allocation triggers GC.
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

            if (iteration == 0u)
            {
                sentinel0 = current;
                s_gcProofThreadRoot = sentinel0;
                nint assigned = System.Runtime.CompilerServices.Unsafe.As<byte[], nint>(
                    ref s_gcProofThreadRoot);
                int assignmentResult = GuideXosManagedThreadStaticProofAssigned(
                    assigned, 0u, (uint)arrayLength, patternValid ? 1u : 0u);
                if (assignmentResult != 0)
                {
                    return GxAbi.ErrorInvalidArgument;
                }
                // Read the genuine field back immediately after the normal
                // managed assignment, before allocation pressure can move
                // the collection request earlier because of the runtime's
                // real thread-static storage allocation.
                nint readback = System.Runtime.CompilerServices.Unsafe.As<byte[], nint>(
                    ref s_gcProofThreadRoot);
                int readbackResult = GuideXosManagedThreadStaticProofReadback(
                    readback, 0u, readback ==
                        System.Runtime.CompilerServices.Unsafe.As<byte[], nint>(ref sentinel0)
                        ? 1u : 0u);
                if (readbackResult != 0)
                {
                    return GxAbi.ErrorInvalidArgument;
                }
            }
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
#elif HOSTLOGPROOF_SHORT_WEAK_LIFETIME
        // Collection 1 is triggered while the NoInlining helper owns the
        // only managed strong reference. Only scalar identity crosses its
        // return boundary; the same production weak handle remains allocated.
        ShortWeakLifetimeSetup setup = CreateAndRunLiveCollection1();
        bool setupValid = setup.Status == 0 && setup.Target != 0 &&
            setup.TargetType != 0 && setup.WeakHandleSlot != 0;
        int boundaryStatus = setupValid
            ? GuideXosNativeAotC011EC33LifetimeBoundaryReturned(
                setup.Target, setup.TargetType, setup.WeakHandleSlot)
            : -1;
        setup = default;
        if (!setupValid || boundaryStatus != 0)
        {
            return GxAbi.ErrorInvalidArgument;
        }

        // The helper's 4-KiB arrays establish Collection 1. After the
        // helper returns, use ordinary 64-KiB allocations to create enough
        // real allocation pressure for the target's promoted generation to
        // be condemned by a later collection. The four retained sentinels
        // are diagnostic workload roots, not references to the target.
        const int arrayLength = 65536;
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

        // The helper frame has returned. This outer frame carries only scalar
        // status and ordinary allocation sentinels.
        for (uint iteration = 0u; iteration < hardLimit; iteration++)
        {
#if HOSTLOGPROOF_C011EC37
            int completedCollections =
                GuideXosNativeAotC011EC33GetCompletedCollections();
            if (completedCollections >= 2)
            {
                // This scalar increment is the deterministic managed
                // continuation checkpoint. It does not allocate or retain a
                // new object before the native observer records success.
#if HOSTLOGPROOF_C011EC40
                int checkpointStatus = RunC011EC37ManagedCheckpoint();
                if (checkpointStatus != 0)
                {
                    return GxAbi.ErrorInvalidArgument;
                }
                return RunC011EC40AllocationReuse() == 0
                    ? 0 : GxAbi.ErrorInvalidArgument;
#elif HOSTLOGPROOF_C011EC39
                int checkpointStatus = RunC011EC37ManagedCheckpoint();
                if (checkpointStatus != 0)
                {
                    return GxAbi.ErrorInvalidArgument;
                }
#if HOSTLOGPROOF_C011EC39_C38_VARIANT
                if (RunC011EC39C38VariantWorkload() != 0)
                {
                    return GxAbi.ErrorInvalidArgument;
                }
#endif
                return GuideXosNativeAotC011EC39Finish() == 0
                    ? 0 : GxAbi.ErrorInvalidArgument;
#elif HOSTLOGPROOF_C011EC38
                int checkpointStatus = RunC011EC37ManagedCheckpoint();
                if (checkpointStatus != 0)
                {
                    return GxAbi.ErrorInvalidArgument;
                }
                return RunC011EC38AllocationReuse() == 0
                    ? 0 : GxAbi.ErrorInvalidArgument;
#else
                int checkpointStatus = RunC011EC37ManagedCheckpoint();
                return checkpointStatus == 0 ? 0 : GxAbi.ErrorInvalidArgument;
#endif
            }
#endif
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
            if (zeroByteCount != arrayLength || !patternValid)
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
#elif HOSTLOGPROOF_SHORT_WEAK_LIVE
        // Establish an independent production strong handle and the real
        // short-weak handle before allocation pressure can suspend the EE.
        byte[] shortWeakTarget = new byte[64];
        for (int i = 0; i < shortWeakTarget.Length; i++)
        {
            shortWeakTarget[i] = (byte)(0xC0 + i);
        }
        nint targetAddress = System.Runtime.CompilerServices.Unsafe.As<byte[], nint>(
            ref shortWeakTarget);
        GCHandle strongRootHandle = GCHandle.Alloc(shortWeakTarget, GCHandleType.Normal);
        nint strongRootSlot = GCHandle.ToIntPtr(strongRootHandle);
        nint strongRootValue = targetAddress;
        GCHandle shortWeakHandle = GCHandle.Alloc(shortWeakTarget, GCHandleType.Weak);
        nint weakHandleSlot = GCHandle.ToIntPtr(shortWeakHandle);
        if (GuideXosNativeAotC011EC31WeakHandleAllocated(
                targetAddress, strongRootSlot, strongRootValue,
                weakHandleSlot, (uint)GCHandleType.Weak) != 0)
        {
            return GxAbi.ErrorInvalidArgument;
        }
        if (GuideXosNativeAotC011EC31StrongRootRecorded(
                strongRootSlot, strongRootValue) != 0)
        {
            return GxAbi.ErrorInvalidArgument;
        }

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
            GC.KeepAlive(strongRootHandle);
            GC.KeepAlive(shortWeakHandle);
            GC.KeepAlive(sentinel0);
            GC.KeepAlive(sentinel1);
            GC.KeepAlive(sentinel2);
            GC.KeepAlive(sentinel3);
            GC.KeepAlive(current);
        }

        return GxAbi.ErrorInvalidArgument;
#elif HOSTLOGPROOF_SHORT_WEAK_DEAD
        // The target and its genuine weak handle are created in a dedicated
        // non-inlined helper. Only scalar identity metadata crosses the
        // helper boundary; no managed target reference or strong handle is
        // retained by the outer frame.
        ShortWeakDeadSetup setup = CreateShortWeakDeadSetup();
        if (setup.Status != 0 || setup.Target == 0 ||
            setup.TargetType == 0 || setup.WeakHandleSlot == 0 ||
            GuideXosNativeAotC011EC32HelperReturned(
                setup.Target, setup.TargetType, setup.WeakHandleSlot) != 0)
        {
            return GxAbi.ErrorInvalidArgument;
        }

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
            nint objectReference = Unsafe.As<byte[], nint>(ref current);
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
