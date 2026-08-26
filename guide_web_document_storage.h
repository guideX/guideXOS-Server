#pragma once

// Bounded on-demand decoded-document ownership shared by the hosted test
// surface and the freestanding Navigator adapter.  The storage deliberately
// has no STL dependency so it can be used by kernel code.  It owns only the
// segments that have actually been touched and has an explicit aggregate cap.

#include <stdint.h>

#include "guide_web_http_shared.h"

namespace gxos {
namespace web {

struct BoundedDocumentStorage {
    static const int kSegmentBytes = kHttpSharedDecodedDocumentSegmentBytes;
    static const int kMaxSegments = kHttpSharedMaxDecodedDocumentSegments;
    static const int kMaxBytes = kHttpSharedMaxDecodedDocumentBytes;

    uint8_t* segments[kMaxSegments];
    uint8_t* decoderHistory;
    int segmentCount;
    int byteCount;
    bool capHit;
    bool allocationFailed;
    bool flattenAllocationFailed;
    int segmentAllocationAttempts;
    int flattenAllocationAttempts;
    int failSegmentAllocationAt;
    int failFlattenAllocationAt;

    BoundedDocumentStorage()
        : decoderHistory(nullptr), segmentCount(0), byteCount(0),
          capHit(false), allocationFailed(false), flattenAllocationFailed(false),
          segmentAllocationAttempts(0), flattenAllocationAttempts(0),
          failSegmentAllocationAt(0), failFlattenAllocationAt(0)
    {
        for (int i = 0; i < kMaxSegments; ++i) segments[i] = nullptr;
    }

    ~BoundedDocumentStorage() { release(); }

    BoundedDocumentStorage(const BoundedDocumentStorage&) = delete;
    BoundedDocumentStorage& operator=(const BoundedDocumentStorage&) = delete;

    void reset()
    {
        release();
        capHit = false;
        allocationFailed = false;
        flattenAllocationFailed = false;
        segmentAllocationAttempts = 0;
        flattenAllocationAttempts = 0;
    }

    void release()
    {
        for (int i = 0; i < segmentCount; ++i) {
            delete[] segments[i];
            segments[i] = nullptr;
        }
        delete[] decoderHistory;
        decoderHistory = nullptr;
        segmentCount = 0;
        byteCount = 0;
    }

    int size() const { return byteCount; }
    int segmentsUsed() const { return segmentCount; }
    int capacityBytes() const { return kMaxBytes; }
    int allocatedBytes() const { return segmentCount * kSegmentBytes; }
    int historyBytes() const { return decoderHistory ? 32768 : 0; }

    // Test-only controls are instance-local so a failed transaction cannot
    // affect another document or another translation unit.  A value of zero
    // disables the corresponding injection; nonzero values are one-based
    // allocation ordinals.
    void setAllocationFaultInjectionForTest(int segmentOrdinal, int flattenOrdinal)
    {
        failSegmentAllocationAt = segmentOrdinal > 0 ? segmentOrdinal : 0;
        failFlattenAllocationAt = flattenOrdinal > 0 ? flattenOrdinal : 0;
        segmentAllocationAttempts = 0;
        flattenAllocationAttempts = 0;
    }

    void clearAllocationFaultInjectionForTest()
    {
        setAllocationFaultInjectionForTest(0, 0);
    }

    int segmentAllocationsAttempted() const { return segmentAllocationAttempts; }
    int flattenAllocationsAttempted() const { return flattenAllocationAttempts; }

    bool appendByte(uint8_t value)
    {
        if (byteCount >= kMaxBytes) {
            capHit = true;
            return false;
        }
        const int segmentIndex = byteCount / kSegmentBytes;
        const int segmentOffset = byteCount % kSegmentBytes;
        if (segmentIndex >= kMaxSegments) {
            capHit = true;
            return false;
        }
        if (!segments[segmentIndex]) {
            ++segmentAllocationAttempts;
            if (failSegmentAllocationAt > 0 &&
                segmentAllocationAttempts == failSegmentAllocationAt) {
                allocationFailed = true;
                return false;
            }
            segments[segmentIndex] = new uint8_t[kSegmentBytes];
            if (!segments[segmentIndex]) {
                allocationFailed = true;
                return false;
            }
            ++segmentCount;
        }
        segments[segmentIndex][segmentOffset] = value;
        ++byteCount;
        return true;
    }

    bool append(const uint8_t* bytes, int length)
    {
        if (length < 0 || (length > 0 && !bytes)) return false;
        for (int i = 0; i < length; ++i) {
            if (!appendByte(bytes[i])) return false;
        }
        return true;
    }

    bool flatten(uint8_t* output, int outputCapacity) const
    {
        if (!output || outputCapacity < byteCount + 1) return false;
        int copied = 0;
        for (int i = 0; i < segmentCount; ++i) {
            const int remaining = byteCount - copied;
            const int count = remaining > kSegmentBytes ? kSegmentBytes : remaining;
            if (count <= 0) break;
            for (int j = 0; j < count; ++j) output[copied + j] = segments[i][j];
            copied += count;
        }
        output[copied] = '\0';
        return copied == byteCount;
    }

    // The parser still requires one contiguous input.  Centralizing this
    // bounded handoff keeps its allocation policy visible and gives the
    // deterministic tests a way to exercise the failure path without
    // replacing the kernel-wide allocator.
    char* allocateFlattenedCopy()
    {
        ++flattenAllocationAttempts;
        if (failFlattenAllocationAt > 0 &&
            flattenAllocationAttempts == failFlattenAllocationAt) {
            flattenAllocationFailed = true;
            allocationFailed = true;
            return nullptr;
        }
        char* output = new char[byteCount + 1];
        if (!output || !flatten(reinterpret_cast<uint8_t*>(output), byteCount + 1)) {
            delete[] output;
            flattenAllocationFailed = true;
            allocationFailed = true;
            return nullptr;
        }
        return output;
    }

    static bool writeDecoderByte(void* context, uint8_t value)
    {
        return context && static_cast<BoundedDocumentStorage*>(context)->appendByte(value);
    }

    static bool prepareDecoderHistory(void* context, uint8_t** history, int* historyCapacity)
    {
        if (history) *history = nullptr;
        if (historyCapacity) *historyCapacity = 0;
        if (!context || !history || !historyCapacity) return false;
        BoundedDocumentStorage* storage = static_cast<BoundedDocumentStorage*>(context);
        if (!storage->decoderHistory) {
            storage->decoderHistory = new uint8_t[32768];
            if (!storage->decoderHistory) {
                storage->allocationFailed = true;
                return false;
            }
        }
        *history = storage->decoderHistory;
        *historyCapacity = 32768;
        return true;
    }

    HttpContentDecoderSink decoderSink()
    {
        HttpContentDecoderSink sink{};
        sink.context = this;
        sink.writeByte = &BoundedDocumentStorage::writeDecoderByte;
        sink.prepareHistory = &BoundedDocumentStorage::prepareDecoderHistory;
        sink.capacityBytes = kMaxBytes;
        sink.bytesWritten = byteCount;
        sink.history = nullptr;
        sink.historyCapacity = 0;
        sink.capacityExceeded = false;
        sink.allocationFailed = false;
        return sink;
    }
};

} // namespace web
} // namespace gxos
