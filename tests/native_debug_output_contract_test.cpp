#include <cassert>
#include <cstdint>
#include <cstdio>

#include "guidexos/development_debug.h"
#include "kernel/core/native_elf/native_elf_debug_output.h"

using kernel::native_elf::NativeDebugOutputQueue;

static gx_development_debug_output_record make_record(
    uint64_t id, uint64_t hit, const char* path, const char* text)
{
    gx_development_debug_output_record record = {};
    record.breakpointId = id;
    record.sessionGeneration = 7;
    record.rawHitCount = hit;
    record.sourceLine = id == 1 ? 4 : 5;
    record.errorCategory = 0;
    uint32_t i = 0;
    while (i + 1 < sizeof(record.sourcePath) && path[i] != '\0') {
        record.sourcePath[i] = path[i];
        ++i;
    }
    record.sourcePath[i] = '\0';
    i = 0;
    while (i + 1 < sizeof(record.text) && text[i] != '\0') {
        record.text[i] = text[i];
        ++i;
    }
    record.text[i] = '\0';
    return record;
}

int main()
{
    NativeDebugOutputQueue queue = {};
    queue.clear();

    const gx_development_debug_output_record first =
        make_record(1, 2, "src/helper.cpp", "input=2 doubled=4");
    const gx_development_debug_output_record second =
        make_record(2, 4, "src/main.cpp", "sum=9");
    assert(queue.enqueue(first));
    assert(queue.enqueue(second));
    assert(queue.count == 2);

    gx_development_debug_output_record output[GX_DEVELOPMENT_DEBUG_MAX_OUTPUT_RECORDS] = {};
    assert(queue.drain(output, GX_DEVELOPMENT_DEBUG_MAX_OUTPUT_RECORDS) == 2);
    assert(output[0].breakpointId == 1 && output[0].rawHitCount == 2);
    assert(output[1].breakpointId == 2 && output[1].rawHitCount == 4);
    assert(output[0].sourcePath[4] == 'h' && output[1].sourcePath[4] == 'm');
    assert(output[0].text[0] == 'i' && output[1].text[0] == 's');
    assert(queue.count == 0 && queue.dropped == 0);

    for (uint64_t id = 1; id <= GX_DEVELOPMENT_DEBUG_MAX_OUTPUT_RECORDS; ++id)
        assert(queue.enqueue(make_record(id, id, "src/loop.cpp", "ordered")));
    assert(!queue.enqueue(make_record(99, 99, "src/drop.cpp", "dropped")));
    assert(queue.count == GX_DEVELOPMENT_DEBUG_MAX_OUTPUT_RECORDS);
    assert(queue.dropped == 1);
    assert(queue.drain(output, GX_DEVELOPMENT_DEBUG_MAX_OUTPUT_RECORDS) ==
           GX_DEVELOPMENT_DEBUG_MAX_OUTPUT_RECORDS);
    for (uint32_t i = 0; i < GX_DEVELOPMENT_DEBUG_MAX_OUTPUT_RECORDS; ++i)
        assert(output[i].breakpointId == i + 1 && output[i].rawHitCount == i + 1);
    assert(queue.dropped == 1);
    assert(queue.drain(output, GX_DEVELOPMENT_DEBUG_MAX_OUTPUT_RECORDS) == 0);

    queue.clear();
    assert(queue.count == 0 && queue.dropped == 0);
    std::printf("Native debug output contract test PASS\n");
    return 0;
}
