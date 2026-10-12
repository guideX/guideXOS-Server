#ifndef KERNEL_I219_LAN_INIT_DIAGNOSTICS_H
#define KERNEL_I219_LAN_INIT_DIAGNOSTICS_H

#include "kernel/nic.h"

namespace kernel {
namespace nic {
namespace lan_init_diagnostics {

inline bool lan_init_done(uint32_t status)
{
    return (status & E1000_STATUS_LAN_INIT_DONE) != 0u;
}

// The reader supplies values for only the STATUS and CTRL offsets. No writer
// is accepted by this interface. The requested count is clamped to the fixed
// sample limit, and delay runs only between observations.
template <typename ReadRegister, typename DelayMilliseconds>
inline uint8_t capture_samples(I219LanInitSample* samples,
                              uint32_t requestedCount,
                              uint32_t* nextSequence,
                              ReadRegister readRegister,
                              DelayMilliseconds delayMilliseconds)
{
#if GXOS_I219_LAN_INIT_DIAGNOSTICS
    if (!samples) return 0u;
    const uint32_t count = requestedCount < I219_LAN_INIT_SAMPLE_LIMIT
        ? requestedCount : I219_LAN_INIT_SAMPLE_LIMIT;
    uint32_t sequence = nextSequence ? *nextSequence : 0u;
    for (uint32_t i = 0; i < count; ++i) {
        I219LanInitSample sample = {};
        sample.sequence = ++sequence;
        sample.status = readRegister(E1000_STATUS);
        sample.statusValid = sample.status != 0xFFFFFFFFu;
        sample.ctrl = readRegister(E1000_CTRL);
        sample.ctrlValid = sample.ctrl != 0xFFFFFFFFu;
        samples[i] = sample;
        if (i + 1u < count) {
            delayMilliseconds(I219_LAN_INIT_SAMPLE_INTERVAL_MS);
        }
    }
    if (nextSequence) *nextSequence = sequence;
    return static_cast<uint8_t>(count);
#else
    (void)samples;
    (void)requestedCount;
    (void)nextSequence;
    (void)readRegister;
    (void)delayMilliseconds;
    return 0u;
#endif
}

} // namespace lan_init_diagnostics
} // namespace nic
} // namespace kernel

#endif // KERNEL_I219_LAN_INIT_DIAGNOSTICS_H
