#include <cassert>
#include <cstdint>

#include "kernel/i219_lan_init_diagnostics.h"
#include "kernel/shell.h"

namespace nic = kernel::nic;
namespace diag = kernel::nic::lan_init_diagnostics;

struct SimulatedMmio {
    uint32_t statuses[8];
    uint32_t ctrls[8];
    uint32_t statusReads;
    uint32_t ctrlReads;
    uint32_t delayCalls;
    uint32_t delayedMilliseconds;
    uint32_t statusWrites;

    SimulatedMmio()
        : statuses{}, ctrls{}, statusReads(0), ctrlReads(0), delayCalls(0),
          delayedMilliseconds(0), statusWrites(0)
    {
    }

    uint32_t read(uint32_t reg)
    {
        if (reg == nic::E1000_STATUS) {
            const uint32_t index = statusReads++;
            return statuses[index < 8u ? index : 7u];
        }
        assert(reg == nic::E1000_CTRL);
        const uint32_t index = ctrlReads++;
        return ctrls[index < 8u ? index : 7u];
    }

    void delay(uint32_t milliseconds)
    {
        ++delayCalls;
        delayedMilliseconds += milliseconds;
    }
};

static void test_parser()
{
    assert(kernel::shell::nicinfo_mode_from_arg("laninit") ==
           kernel::shell::NICINFO_MODE_LANINIT);
    assert(kernel::shell::nicinfo_mode_from_arg("laninitx") ==
           kernel::shell::NICINFO_MODE_INVALID);
}

int main()
{
    test_parser();

#if GXOS_I219_LAN_INIT_DIAGNOSTICS
    {
        SimulatedMmio mmio;
        mmio.statuses[0] = nic::E1000_STATUS_LAN_INIT_DONE | nic::E1000_STATUS_LU;
        mmio.ctrls[0] = 0u;
        nic::I219LanInitSample sample = {};
        uint32_t sequence = 0u;
        const uint8_t count = diag::capture_samples(
            &sample, 1u, &sequence,
            [&mmio](uint32_t reg) { return mmio.read(reg); },
            [&mmio](uint32_t ms) { mmio.delay(ms); });
        assert(count == 1u);
        assert(sequence == 1u && sample.sequence == 1u);
        assert(sample.statusValid && sample.ctrlValid);
        assert(diag::lan_init_done(sample.status));
        assert((sample.status & nic::E1000_STATUS_LU) != 0u);
        assert(mmio.statusReads == 1u && mmio.ctrlReads == 1u);
        assert(mmio.delayCalls == 0u && mmio.statusWrites == 0u);
    }

    {
        SimulatedMmio mmio;
        mmio.statuses[0] = nic::E1000_STATUS_LU;
        mmio.ctrls[0] = nic::E1000_CTRL_RST;
        nic::I219LanInitSample sample = {};
        uint32_t sequence = 4u;
        const uint8_t count = diag::capture_samples(
            &sample, 1u, &sequence,
            [&mmio](uint32_t reg) { return mmio.read(reg); },
            [&mmio](uint32_t ms) { mmio.delay(ms); });
        assert(count == 1u);
        assert(sequence == 5u && sample.sequence == 5u);
        assert(sample.statusValid && !diag::lan_init_done(sample.status));
        assert(sample.ctrlValid && (sample.ctrl & nic::E1000_CTRL_RST) != 0u);
        assert(mmio.statusWrites == 0u);
    }

    {
        SimulatedMmio mmio;
        mmio.statuses[0] = 0u;
        mmio.statuses[1] = nic::E1000_STATUS_LAN_INIT_DONE;
        mmio.statuses[2] = nic::E1000_STATUS_LAN_INIT_DONE | nic::E1000_STATUS_PHYRA;
        mmio.ctrls[0] = 0u;
        mmio.ctrls[1] = 0u;
        mmio.ctrls[2] = 0u;
        nic::I219LanInitSample samples[nic::I219_LAN_INIT_SAMPLE_LIMIT] = {};
        uint32_t sequence = 0u;
        const uint8_t count = diag::capture_samples(
            samples, 3u, &sequence,
            [&mmio](uint32_t reg) { return mmio.read(reg); },
            [&mmio](uint32_t ms) { mmio.delay(ms); });
        assert(count == 3u);
        assert(!diag::lan_init_done(samples[0].status));
        assert(diag::lan_init_done(samples[1].status));
        assert(diag::lan_init_done(samples[2].status));
        assert((samples[2].status & nic::E1000_STATUS_PHYRA) != 0u);
        assert(samples[0].sequence == 1u && samples[1].sequence == 2u &&
               samples[2].sequence == 3u);
        assert(mmio.statusReads == 3u && mmio.ctrlReads == 3u);
        assert(mmio.delayCalls == 2u && mmio.delayedMilliseconds == 2u);
        assert(mmio.statusWrites == 0u);
    }

    {
        SimulatedMmio mmio;
        for (uint32_t i = 0; i < 8u; ++i) {
            mmio.statuses[i] = (i & 1u)
                ? nic::E1000_STATUS_LAN_INIT_DONE : 0u;
            mmio.ctrls[i] = 0u;
        }
        nic::I219LanInitSample samples[nic::I219_LAN_INIT_SAMPLE_LIMIT] = {};
        uint32_t sequence = 0u;
        const uint8_t count = diag::capture_samples(
            samples, 99u, &sequence,
            [&mmio](uint32_t reg) { return mmio.read(reg); },
            [&mmio](uint32_t ms) { mmio.delay(ms); });
        assert(count == nic::I219_LAN_INIT_SAMPLE_LIMIT);
        assert(mmio.statusReads == nic::I219_LAN_INIT_SAMPLE_LIMIT);
        assert(mmio.ctrlReads == nic::I219_LAN_INIT_SAMPLE_LIMIT);
        assert(mmio.delayCalls == nic::I219_LAN_INIT_SAMPLE_LIMIT - 1u);
        assert(sequence == nic::I219_LAN_INIT_SAMPLE_LIMIT);
        assert(mmio.statusWrites == 0u);
    }

    {
        SimulatedMmio mmio;
        mmio.statuses[0] = 0xFFFFFFFFu;
        mmio.ctrls[0] = 0xFFFFFFFFu;
        nic::I219LanInitSample sample = {};
        const uint8_t count = diag::capture_samples(
            &sample, 1u, nullptr,
            [&mmio](uint32_t reg) { return mmio.read(reg); },
            [&mmio](uint32_t ms) { mmio.delay(ms); });
        assert(count == 1u && !sample.statusValid && !sample.ctrlValid);
        assert(mmio.statusWrites == 0u);
    }
#else
    {
        SimulatedMmio mmio;
        nic::I219LanInitSample sample = {};
        sample.sequence = 77u;
        uint32_t sequence = 9u;
        const uint8_t count = diag::capture_samples(
            &sample, 3u, &sequence,
            [&mmio](uint32_t reg) { return mmio.read(reg); },
            [&mmio](uint32_t ms) { mmio.delay(ms); });
        assert(count == 0u);
        assert(sequence == 9u && sample.sequence == 77u);
        assert(mmio.statusReads == 0u && mmio.ctrlReads == 0u);
        assert(mmio.delayCalls == 0u && mmio.statusWrites == 0u);
    }
#endif

    return 0;
}
