#include "gxos_time_estimate.h"
#include "gxos_tls_time_policy.h"

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
    if (store) store->initialize_for_test();
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

extern "C" int gxos_time_estimate_test_tls_request(
    uint32_t index, uint64_t now_monotonic_ns,
    gxos::clockassurance::TlsTimeEstimateLease* output)
{
    gxos::clockassurance::TimeEstimateStore* store = store_for(index);
    if (!store || !output) return -1;
    return static_cast<int>(store->request_tls_lease(now_monotonic_ns, output));
}

extern "C" int gxos_time_estimate_test_tls_refresh(
    uint32_t index, const gxos::clockassurance::TlsTimeEstimateLease* lease,
    uint64_t now_monotonic_ns, gxos::clockassurance::TlsTimeEstimateLease* output)
{
    gxos::clockassurance::TimeEstimateStore* store = store_for(index);
    if (!store || !lease || !output) return -1;
    return static_cast<int>(store->refresh_tls_lease(*lease, now_monotonic_ns, output));
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

extern "C" int gxos_tls_time_policy_check_interval(
    uint64_t earliest, uint64_t latest, int64_t not_before, int64_t not_after)
{
    return static_cast<int>(gxos::tls_time::check_certificate_interval(
        earliest, latest, not_before, not_after));
}

extern "C" int gxos_tls_time_policy_utc_to_unix(
    int year, int month, int day, int hour, int minute, int second,
    int64_t* output_unix_seconds)
{
    const gxos::tls_time::UtcCalendarTime calendar = {
        year, month, day, hour, minute, second
    };
    return gxos::tls_time::certificate_utc_to_unix_seconds(
        calendar, output_unix_seconds) ? 1 : 0;
}
