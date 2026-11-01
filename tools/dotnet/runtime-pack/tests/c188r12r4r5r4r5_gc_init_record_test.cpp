#include "../src/platform/guidexos_nativeaot_gcinit_record.h"

#include <stdint.h>

namespace {
int g_failures = 0;

void check(bool condition, const char* label) {
    if (!condition) {
        ++g_failures;
        (void)label;
    }
}
}

int main() {
    const uint32_t eFail = 0x80004005u;
    const uint32_t outOfMemory = 0x8007000Eu;

    GuideXosNativeAotGcInitBeginHeap();
    check(GuideXosNativeAotGcInitGetField(GX_GCINIT_FIELD_HEAP_STATE) == GX_GCINIT_ENTERED,
          "heap enters");
    GuideXosNativeAotGcInitBeginSemiShared();
    GuideXosNativeAotGcInitCompleteSemiShared(1);
    GuideXosNativeAotGcInitRecordInitializeGcResult(0u);
    GuideXosNativeAotGcInitBeginWaitForGCEvent();
    GuideXosNativeAotGcInitCompleteWaitEventAllocation(true);
    GuideXosNativeAotGcInitBeginWaitEventCreate();
    GuideXosNativeAotGcInitCompleteWaitEventCreate(true);
    check(static_cast<uint32_t>(GuideXosNativeAotGcInitRecordHeapResult(0)) == 0u,
          "success HRESULT is unchanged");
    check(GuideXosNativeAotGcInitGetField(GX_GCINIT_FIELD_HEAP_STATE) == GX_GCINIT_RETURNED_SUCCESS,
          "heap success is recorded");
    check(GuideXosNativeAotGcInitGetField(GX_GCINIT_FIELD_FIRST_FAILURE_SITE) == GX_GCINIT_FAILURE_NONE,
          "success has no failure site");
    check(GuideXosNativeAotGcInitGetField(GX_GCINIT_FIELD_NATIVE_STATUS) == GX_GCINIT_NATIVE_STATUS_UNAVAILABLE,
          "native status remains explicitly unavailable");

    GuideXosNativeAotGcInitBeginHeap();
    GuideXosNativeAotGcInitBeginSemiShared();
    GuideXosNativeAotGcInitCompleteSemiShared(0);
    GuideXosNativeAotGcInitRecordInitializeGcResult(eFail);
    check(static_cast<uint32_t>(GuideXosNativeAotGcInitRecordHeapResult(static_cast<int32_t>(eFail))) == eFail,
          "semi-shared failure HRESULT is unchanged");
    check(GuideXosNativeAotGcInitGetField(GX_GCINIT_FIELD_FIRST_FAILURE_SITE) == GX_GCINIT_FAILURE_INIT_SEMI_SHARED,
          "first semi-shared failure is retained");
    check(GuideXosNativeAotGcInitGetField(GX_GCINIT_FIELD_WAIT_EVENT_ALLOCATION_STATE) == GX_GCINIT_NOT_ENTERED,
          "wait event remains unentered after semi-shared failure");

    GuideXosNativeAotGcInitBeginHeap();
    GuideXosNativeAotGcInitBeginSemiShared();
    GuideXosNativeAotGcInitCompleteSemiShared(1);
    GuideXosNativeAotGcInitRecordInitializeGcResult(0u);
    GuideXosNativeAotGcInitBeginWaitForGCEvent();
    GuideXosNativeAotGcInitCompleteWaitEventAllocation(true);
    GuideXosNativeAotGcInitBeginWaitEventCreate();
    GuideXosNativeAotGcInitCompleteWaitEventCreate(false);
    (void)GuideXosNativeAotGcInitRecordHeapResult(static_cast<int32_t>(eFail));
    check(GuideXosNativeAotGcInitGetField(GX_GCINIT_FIELD_FIRST_FAILURE_SITE) == GX_GCINIT_FAILURE_WAIT_EVENT_CREATE,
          "wait-event failure site is retained");
    check(GuideXosNativeAotGcInitGetField(GX_GCINIT_FIELD_FINAL_HEAP_HRESULT) == eFail,
          "wait-event E_FAIL is recorded");

    GuideXosNativeAotGcInitBeginHeap();
    GuideXosNativeAotGcInitBeginSemiShared();
    GuideXosNativeAotGcInitCompleteSemiShared(1);
    GuideXosNativeAotGcInitRecordInitializeGcResult(0u);
    GuideXosNativeAotGcInitBeginWaitForGCEvent();
    GuideXosNativeAotGcInitCompleteWaitEventAllocation(false);
    (void)GuideXosNativeAotGcInitRecordHeapResult(static_cast<int32_t>(outOfMemory));
    check(GuideXosNativeAotGcInitGetField(GX_GCINIT_FIELD_FIRST_FAILURE_SITE) == GX_GCINIT_FAILURE_WAIT_EVENT_ALLOCATION,
          "WaitForGCEvent wrapper allocation failure is identified");
    check(GuideXosNativeAotGcInitGetField(GX_GCINIT_FIELD_WAIT_EVENT_CREATE_STATE) == GX_GCINIT_NOT_ENTERED,
          "event creation does not follow failed wrapper allocation");

    GuideXosNativeAotGcInitBeginHeap();
    GuideXosNativeAotGcInitBeginSemiShared();
    check(GuideXosNativeAotGcInitGetField(GX_GCINIT_FIELD_SEMI_SHARED_STATE) == GX_GCINIT_ENTERED,
          "begin without completion stays entered");
    check(GuideXosNativeAotGcInitGetField(GX_GCINIT_FIELD_SEMI_SHARED_RESULT) == GX_GCINIT_VALUE_UNAVAILABLE,
          "begin without completion has no invented result");
    GuideXosNativeAotGcInitFinalizeForRead();
    check(GuideXosNativeAotGcInitGetField(GX_GCINIT_FIELD_HEAP_STATE) == GX_GCINIT_DID_NOT_RETURN,
          "in-flight heap is marked did-not-return after the outer return");
    check(GuideXosNativeAotGcInitGetField(GX_GCINIT_FIELD_SEMI_SHARED_STATE) == GX_GCINIT_DID_NOT_RETURN,
          "in-flight operation is marked did-not-return before the snapshot");
    check(GuideXosNativeAotGcInitGetField(GX_GCINIT_FIELD_SEMI_SHARED_RESULT) == GX_GCINIT_VALUE_UNAVAILABLE,
          "did-not-return preserves unavailable result");
    check(GuideXosNativeAotGcInitGetField(GX_GCINIT_FIELD_INVOCATION) == 5u,
          "invocation identity increments");
    check(GuideXosNativeAotGcInitGetField(GX_GCINIT_FIELD_COUNT) == GX_GCINIT_VALUE_UNAVAILABLE,
          "out-of-range accessor is bounded");

    return g_failures == 0 ? 0 : 1;
}
