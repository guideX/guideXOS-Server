#include "gxos_time_estimate.h"

namespace {

static const uint32_t kStoreCount = 16;
static gxos::clockassurance::TimeEstimateStore s_stores[kStoreCount];

static gxos::clockassurance::TimeEstimateStore* store_for(uint32_t index)
{
    return index < kStoreCount ? &s_stores[index] : 0;
}

} // namespace

extern "C" void gxos_time_estimate_test_reset(uint32_t index)
{
    gxos::clockassurance::TimeEstimateStore* store = store_for(index);
    if (store) store->initialize();
}

extern "C" int gxos_time_estimate_test_accept(
    uint32_t index,
    const gxos_roughtime_round_configuration* configuration,
    const gxos_roughtime_round_result* round,
    uint64_t now_monotonic_ns)
{
    gxos::clockassurance::TimeEstimateStore* store = store_for(index);
    if (!store || !configuration || !round) return -1;
    return static_cast<int>(store->accept_fixture_round(
        *configuration, *round, now_monotonic_ns));
}

extern "C" int gxos_time_estimate_test_query(
    uint32_t index,
    uint64_t now_monotonic_ns,
    gxos::clockassurance::TimeEstimateInfo* output)
{
    gxos::clockassurance::TimeEstimateStore* store = store_for(index);
    if (!store || !output) return -1;
    return static_cast<int>(store->query(now_monotonic_ns, output));
}

extern "C" void gxos_time_estimate_test_invalidate(uint32_t index, uint8_t reason)
{
    gxos::clockassurance::TimeEstimateStore* store = store_for(index);
    if (store) store->invalidate(static_cast<gxos::clockassurance::TimeEstimateReason>(reason));
}

extern "C" void gxos_time_estimate_test_discontinuity(uint32_t index)
{
    gxos::clockassurance::TimeEstimateStore* store = store_for(index);
    if (store) store->report_clock_discontinuity();
}
