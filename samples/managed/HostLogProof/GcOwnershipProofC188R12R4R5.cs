using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

#if HOSTLOGPROOF_C188R12R4R5_GC_OWNERSHIP_PROOF || HOSTLOGPROOF_C188R12R4R5_GC_OWNERSHIP_TESTS
namespace HostLogProof;

internal static unsafe class GcOwnershipProofC188R12R4R5
{
    private const uint RootObject = 1u;
    private const uint ChildObject = 2u;
    private const uint ReferenceArray = 3u;
    private const uint PrimitiveArray = 4u;
    private const uint PayloadArray = 5u;
    private const uint EphemeralObject = 6u;

    private static GCHandle s_rootHandle;
    private static GCHandle s_ephemeralWeakHandle;
    private static uint s_managedEntryCount;
    private static uint s_startCollectionCount;
    private static ulong s_startHeapAllocatedBytes;
    private static ulong s_startThreadAllocatedBytes;
    private static nint s_previousRootAddress;
    private static nint s_previousChildAddress;
    private static uint s_testObjectObservations;
    private static uint s_testCounterReads;

#if HOSTLOGPROOF_C188R12R4R5_GC_OWNERSHIP_PROOF
    [DllImport("__Internal", EntryPoint = "guideXosNativeAotC188R12R4R5ObserveObject")]
    private static extern int NativeObserveObject(
        uint callbackOrdinal, uint role, nint objectAddress,
        uint isArray, uint expectedLength);

    [DllImport("__Internal", EntryPoint = "guideXosNativeAotC188R12R4R5ReadCounters")]
    private static extern int NativeReadCounters(
        out uint collectionCount, out ulong heapAllocatedBytes,
        out ulong threadAllocatedBytes);
#endif

    private sealed class Node
    {
        public int Marker;
        public Node Next;
        public Node[] Links;
        public int[] Values;
        public byte[] Payload;
    }

    public static int Execute(NativeGxAppContext* context)
    {
        return RunEntry(context);
    }

    private static int RunEntry(NativeGxAppContext* context)
    {
        uint callbackOrdinal = ++s_managedEntryCount;
        if (callbackOrdinal == 1u)
        {
            return CreateAndRecordGraph(context, callbackOrdinal);
        }

        if (callbackOrdinal > 3u || !s_rootHandle.IsAllocated)
        {
            Emit(context, "invalid-entry"u8, callbackOrdinal, 0u, 0u,
                0u, default, default, default, default, default, default,
                0u, 0u, 0u, 0u, 0u);
            return GxAbi.ErrorInvalidArgument;
        }

        return CollectAndVerify(context, callbackOrdinal);
    }

    private static int CreateAndRecordGraph(
        NativeGxAppContext* context, uint callbackOrdinal)
    {
        if (s_rootHandle.IsAllocated || ReadCounters(
                out s_startCollectionCount, out s_startHeapAllocatedBytes,
                out s_startThreadAllocatedBytes) != 0)
        {
            return GxAbi.ErrorInvalidArgument;
        }

        if (CreateGraph() != 0 || !TryGetRoot(out Node root) ||
            !VerifyGraph(root) || RecordGraph(callbackOrdinal, root) != 0 ||
            ReadCounters(out uint collectionCount, out ulong heapAllocatedBytes,
                out ulong threadAllocatedBytes) != 0)
        {
            return GxAbi.ErrorInvalidArgument;
        }

        s_previousRootAddress = ObjectAddress(root);
        s_previousChildAddress = ObjectAddress(root.Next);
        bool accountingValid = heapAllocatedBytes > s_startHeapAllocatedBytes ||
            threadAllocatedBytes > s_startThreadAllocatedBytes;
        bool noCollection = collectionCount == s_startCollectionCount;
        bool passed = accountingValid && noCollection;
        bool emitted = Emit(context, "created"u8, callbackOrdinal,
            collectionCount, heapAllocatedBytes, threadAllocatedBytes,
            root, root.Next, root.Links,
            root.Values, root.Payload, GCHandle.ToIntPtr(s_rootHandle),
            noCollection ? 1u : 0u, 0u, 0u, 0u, passed ? 1u : 0u);
        return passed && emitted ? 0 : GxAbi.ErrorInvalidArgument;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int CreateGraph()
    {
        Node root = new();
        Node child = new();
        if (root.Marker != 0 || root.Next != null || root.Links != null ||
            root.Values != null || root.Payload != null ||
            child.Marker != 0 || child.Next != null || child.Links != null ||
            child.Values != null || child.Payload != null)
        {
            return -1;
        }

        Node[] links = new Node[2];
        int[] values = new int[6];
        byte[] payload = new byte[16];
        if (links[0] != null || links[1] != null ||
            !AllZero(values) || !AllZero(payload))
        {
            return -1;
        }

        root.Marker = unchecked((int)0xC1881244);
        child.Marker = unchecked((int)0xC1881245);
        root.Next = child;
        child.Next = root;
        root.Links = links;
        root.Values = values;
        root.Payload = payload;
        links[0] = root;
        links[1] = child;
        for (int index = 0; index < values.Length; index++)
        {
            values[index] = 0x5100 + index * 17;
        }
        for (int index = 0; index < payload.Length; index++)
        {
            payload[index] = (byte)(0xA0 + index * 3);
        }

        s_rootHandle = GCHandle.Alloc(root, GCHandleType.Normal);
        return s_rootHandle.IsAllocated ? 0 : -1;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int CollectAndVerify(
        NativeGxAppContext* context, uint callbackOrdinal)
    {
        if (s_ephemeralWeakHandle.IsAllocated)
        {
            s_ephemeralWeakHandle.Free();
        }

        uint beforeCollection = (uint)GC.CollectionCount(2);
        CreateEphemeralWeakTarget();
        nint ephemeralAddress = NativeEphemeralAddress;
        if (ephemeralAddress == 0 || s_ephemeralWeakHandle.Target == null)
        {
            return GxAbi.ErrorInvalidArgument;
        }

        GC.Collect(2, GCCollectionMode.Forced, blocking: true, compacting: true);
        uint afterCollection = (uint)GC.CollectionCount(2);
        bool weakCleared = s_ephemeralWeakHandle.Target == null;
        if (ReadCounters(out uint nativeCollectionCount,
                out ulong heapAllocatedBytes,
                out ulong threadAllocatedBytes) != 0)
        {
            return GxAbi.ErrorInvalidArgument;
        }
        bool collectionValid = IsCollectionQualified(beforeCollection,
            afterCollection, nativeCollectionCount, weakCleared);
        if (!TryGetRoot(out Node root) || !VerifyGraph(root) ||
            RecordGraph(callbackOrdinal, root) != 0 ||
            root == null)
        {
            return GxAbi.ErrorInvalidArgument;
        }

        nint rootAddress = ObjectAddress(root);
        bool rootHandleUpdated = rootAddress != 0 &&
            s_rootHandle.Target is Node handleTarget &&
            ObjectAddress(handleTarget) == rootAddress;
        bool moved = rootAddress != s_previousRootAddress ||
            ObjectAddress(root.Next) != s_previousChildAddress;
        // Relocation is useful evidence but is not required for safe collection.
        // The registered handle must resolve to the retained root either way.
        bool proofPassed = collectionValid && rootHandleUpdated;
        bool emitted = Emit(context, callbackOrdinal == 2u
                ? "collection-1"u8 : "collection-2"u8,
            callbackOrdinal, nativeCollectionCount, heapAllocatedBytes,
            threadAllocatedBytes,
            root, root.Next, root.Links, root.Values, root.Payload,
            GCHandle.ToIntPtr(s_rootHandle), collectionValid ? 1u : 0u,
            weakCleared ? 1u : 0u, rootHandleUpdated ? 1u : 0u,
            moved ? 1u : 0u,
            proofPassed ? 1u : 0u);

        s_ephemeralWeakHandle.Free();
        if (!proofPassed || !emitted)
        {
            return GxAbi.ErrorInvalidArgument;
        }

        s_previousRootAddress = rootAddress;
        s_previousChildAddress = ObjectAddress(root.Next);

        return 0;
    }

    private static nint NativeEphemeralAddress;

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void CreateEphemeralWeakTarget()
    {
        Node ephemeral = new() { Marker = unchecked((int)0xC188E5E1) };
        NativeEphemeralAddress = ObjectAddress(ephemeral);
        s_ephemeralWeakHandle = GCHandle.Alloc(ephemeral, GCHandleType.Weak);
        if (ObserveObject(s_managedEntryCount, EphemeralObject,
                NativeEphemeralAddress, 0u, 0u) != 0)
        {
            NativeEphemeralAddress = 0;
        }
        GC.KeepAlive(ephemeral);
    }

    private static int RecordGraph(uint callbackOrdinal, Node root)
    {
        if (root == null || root.Next == null || root.Links == null ||
            root.Values == null || root.Payload == null)
        {
            return -1;
        }

        return ObserveObject(callbackOrdinal, RootObject,
                   ObjectAddress(root), 0u, 0u) == 0 &&
               ObserveObject(callbackOrdinal, ChildObject,
                   ObjectAddress(root.Next), 0u, 0u) == 0 &&
               ObserveObject(callbackOrdinal, ReferenceArray,
                   ObjectAddress(root.Links), 1u, (uint)root.Links.Length) == 0 &&
               ObserveObject(callbackOrdinal, PrimitiveArray,
                   ObjectAddress(root.Values), 1u, (uint)root.Values.Length) == 0 &&
               ObserveObject(callbackOrdinal, PayloadArray,
                   ObjectAddress(root.Payload), 1u, (uint)root.Payload.Length) == 0
            ? 0 : -1;
    }

    private static bool TryGetRoot(out Node root)
    {
        root = s_rootHandle.IsAllocated ? s_rootHandle.Target as Node : null;
        return root != null;
    }

    private static bool VerifyGraph(Node root)
    {
        if (root == null || root.Marker != unchecked((int)0xC1881244) ||
            root.Next == null || root.Next.Marker != unchecked((int)0xC1881245) ||
            !ReferenceEquals(root.Next.Next, root) || root.Links == null ||
            root.Links.Length != 2 || !ReferenceEquals(root.Links[0], root) ||
            !ReferenceEquals(root.Links[1], root.Next) ||
            root.Values == null || root.Values.Length != 6 ||
            root.Payload == null || root.Payload.Length != 16)
        {
            return false;
        }

        for (int index = 0; index < root.Values.Length; index++)
        {
            if (root.Values[index] != 0x5100 + index * 17)
            {
                return false;
            }
        }
        for (int index = 0; index < root.Payload.Length; index++)
        {
            if (root.Payload[index] != (byte)(0xA0 + index * 3))
            {
                return false;
            }
        }
        return true;
    }

    private static bool AllZero(int[] values)
    {
        for (int index = 0; index < values.Length; index++)
        {
            if (values[index] != 0) return false;
        }
        return true;
    }

    private static bool AllZero(byte[] values)
    {
        for (int index = 0; index < values.Length; index++)
        {
            if (values[index] != 0) return false;
        }
        return true;
    }

    private static nint ObjectAddress<T>(T value) where T : class =>
        Unsafe.As<T, nint>(ref value);

    private static int ObserveObject(
        uint callbackOrdinal, uint role, nint objectAddress,
        uint isArray, uint expectedLength)
    {
#if HOSTLOGPROOF_C188R12R4R5_GC_OWNERSHIP_TESTS
        s_testObjectObservations++;
        return objectAddress != 0 &&
            (isArray == 0u || expectedLength != 0u) ? 0 : -1;
#else
        return NativeObserveObject(callbackOrdinal, role, objectAddress,
            isArray, expectedLength);
#endif
    }

    private static int ReadCounters(
        out uint collectionCount, out ulong heapAllocatedBytes,
        out ulong threadAllocatedBytes)
    {
#if HOSTLOGPROOF_C188R12R4R5_GC_OWNERSHIP_TESTS
        s_testCounterReads++;
        collectionCount = (uint)GC.CollectionCount(2);
        heapAllocatedBytes = unchecked((ulong)GC.GetTotalAllocatedBytes(precise: true)) +
            (ulong)s_testCounterReads * 256u;
        threadAllocatedBytes = 0u;
        return 0;
#else
        return NativeReadCounters(out collectionCount, out heapAllocatedBytes,
            out threadAllocatedBytes);
#endif
    }

    private static bool IsCollectionQualified(
        uint before, uint after, uint nativeAfter, bool weakCleared) =>
        after > before && nativeAfter == after && weakCleared;

    private static bool Emit(
        NativeGxAppContext* context,
        ReadOnlySpan<byte> phase,
        uint callbackOrdinal,
        uint collectionCount,
        ulong heapAllocatedBytes,
        ulong threadAllocatedBytes,
        Node root,
        Node child,
        Node[] references,
        int[] values,
        byte[] payload,
        nint rootHandle,
        uint collectionValid,
        uint weakCleared,
        uint rootHandleUpdated,
        uint moved,
        uint proofPassed)
    {
        Span<byte> buffer = stackalloc byte[512];
        int position = 0;
        Append(buffer, ref position, "marker=C188R12R4R5-GC-OWNERSHIP phase="u8);
        Append(buffer, ref position, phase);
        Append(buffer, ref position, " callback="u8);
        AppendDecimal(buffer, ref position, callbackOrdinal);
        Append(buffer, ref position, " collectionCount="u8);
        AppendDecimal(buffer, ref position, collectionCount);
        Append(buffer, ref position, " heapAllocatedBytes="u8);
        AppendDecimal(buffer, ref position, heapAllocatedBytes);
        Append(buffer, ref position, " threadAllocatedBytes="u8);
        AppendDecimal(buffer, ref position, threadAllocatedBytes);
        Append(buffer, ref position, " root="u8);
        AppendHex(buffer, ref position, ObjectAddress(root));
        Append(buffer, ref position, " child="u8);
        AppendHex(buffer, ref position, ObjectAddress(child));
        Append(buffer, ref position, " references="u8);
        AppendHex(buffer, ref position, ObjectAddress(references));
        Append(buffer, ref position, " primitive="u8);
        AppendHex(buffer, ref position, ObjectAddress(values));
        Append(buffer, ref position, " payload="u8);
        AppendHex(buffer, ref position, ObjectAddress(payload));
        Append(buffer, ref position, " strongHandle="u8);
        AppendHex(buffer, ref position, rootHandle);
        Append(buffer, ref position, " ownership=pass graph=pass collectionQualified="u8);
        AppendDecimal(buffer, ref position, collectionValid);
        Append(buffer, ref position, " weakCleared="u8);
        AppendDecimal(buffer, ref position, weakCleared);
        if (phase.SequenceEqual("created"u8))
        {
            Append(buffer, ref position,
                " generation=0 blocking=0 compacting=0 rootHandleUpdated=0"u8);
        }
        else
        {
            Append(buffer, ref position,
                " generation=2 blocking=1 compacting=1 rootHandleUpdated="u8);
            AppendDecimal(buffer, ref position, rootHandleUpdated);
        }
        Append(buffer, ref position, " moved="u8);
        AppendDecimal(buffer, ref position, moved);
        Append(buffer, ref position, proofPassed == 1u
            ? " outcome=PASS"u8 : " outcome=FAIL"u8);

#if HOSTLOGPROOF_C188R12R4R5_GC_OWNERSHIP_TESTS
        Console.WriteLine(System.Text.Encoding.ASCII.GetString(buffer[..position]));
        return true;
#else
        if (context == null || context->host == null || context->host->log == null)
        {
            return false;
        }
        buffer[position] = 0;
        fixed (byte* message = buffer)
        {
            return context->host->log(context, message) == 0;
        }
#endif
    }

    private static void Append(
        Span<byte> destination, ref int position, ReadOnlySpan<byte> value)
    {
        value.CopyTo(destination[position..]);
        position += value.Length;
    }

    private static void AppendDecimal(
        Span<byte> destination, ref int position, ulong value)
    {
        Span<byte> reverse = stackalloc byte[20];
        int digits = 0;
        do
        {
            reverse[digits++] = (byte)('0' + (value % 10u));
            value /= 10u;
        } while (value != 0u);
        while (digits != 0)
        {
            destination[position++] = reverse[--digits];
        }
    }

    private static void AppendHex(
        Span<byte> destination, ref int position, nint value)
    {
        const string Hex = "0123456789ABCDEF";
        ulong bits = unchecked((ulong)(nuint)value);
        for (int shift = 60; shift >= 0; shift -= 4)
        {
            destination[position++] = (byte)Hex[(int)((bits >> shift) & 0xFu)];
        }
    }

    private static void AppendHex(
        Span<byte> destination, ref int position, ulong value)
    {
        AppendHex(destination, ref position, unchecked((nint)value));
    }

    public static int RunHostedTests()
    {
        s_rootHandle = default;
        s_ephemeralWeakHandle = default;
        s_managedEntryCount = 0u;
        s_testObjectObservations = 0u;
        s_testCounterReads = 0u;
        int first = RunEntry(null);
        int second = RunEntry(null);
        int third = RunEntry(null);
        bool negativeCasesRejected =
            !IsCollectionQualified(4u, 4u, 4u, true) &&
            !IsCollectionQualified(4u, 5u, 4u, true) &&
            !IsCollectionQualified(4u, 5u, 5u, false);
        bool passed = first == 0 && second == 0 && third == 0 &&
            s_testObjectObservations == 17u && s_testCounterReads >= 3u &&
            s_managedEntryCount == 3u && negativeCasesRejected;
        if (s_rootHandle.IsAllocated) s_rootHandle.Free();
        if (s_ephemeralWeakHandle.IsAllocated) s_ephemeralWeakHandle.Free();
        Console.WriteLine(passed
            ? "[C188R12R4R5-GC-OWNERSHIP-HOSTED] outcome=PASS callbacks=3 observations=17 collections=2 negativeCollectionGuards=3"
            : $"[C188R12R4R5-GC-OWNERSHIP-HOSTED] outcome=FAIL first={first} second={second} third={third} observations={s_testObjectObservations} counterReads={s_testCounterReads}");
        return passed ? 0 : 1;
    }
}
#endif
