#include <assert.h>
#include <stdint.h>

#include "kernel/nic.h"

using namespace kernel;

static void test_tarc0_policy()
{
    const uint32_t current = 0xA5A55A5Au | nic::E1000_TARC0_CB_MULTIQ_3_REQ;
    const uint32_t configured = nic::i219_pch_tarc0_configuration(current);
    const uint32_t changed = nic::E1000_TARC0_I219_INIT_BITS |
                             nic::E1000_TARC0_CB_MULTIQ_3_REQ;

    assert((configured & nic::E1000_TARC0_I219_INIT_BITS) ==
           nic::E1000_TARC0_I219_INIT_BITS);
    assert((configured & nic::E1000_TARC0_CB_MULTIQ_3_REQ) ==
           nic::E1000_TARC0_CB_MULTIQ_2_REQ);
    assert(((configured ^ current) & ~changed) == 0u);
    assert(nic::i219_pch_tarc0_configuration(configured) == configured);
}

static void test_tarc1_policy()
{
    const uint32_t current = 0xA5A55A5Au;
    const uint32_t changed = nic::E1000_TARC1_I219_INIT_BITS |
                             nic::E1000_TARC1_I219_MULR_POLICY;

    const uint32_t mulrClear = nic::i219_pch_tarc1_configuration(
        current, 0u);
    assert((mulrClear & nic::E1000_TARC1_I219_INIT_BITS) ==
           nic::E1000_TARC1_I219_INIT_BITS);
    assert((mulrClear & nic::E1000_TARC1_I219_MULR_POLICY) != 0u);
    assert(((mulrClear ^ current) & ~changed) == 0u);

    const uint32_t mulrSet = nic::i219_pch_tarc1_configuration(
        current | nic::E1000_TARC1_I219_MULR_POLICY,
        nic::E1000_TCTL_MULR);
    assert((mulrSet & nic::E1000_TARC1_I219_INIT_BITS) ==
           nic::E1000_TARC1_I219_INIT_BITS);
    assert((mulrSet & nic::E1000_TARC1_I219_MULR_POLICY) == 0u);
    assert(((mulrSet ^ (current | nic::E1000_TARC1_I219_MULR_POLICY)) &
            ~changed) == 0u);
}

static void test_rfctl_policy()
{
    const uint32_t current = 0xA5A55A0Au;
    const uint32_t configured = nic::i219_pch_rfctl_configuration(current);
    const uint32_t required = nic::E1000_RFCTL_NFSW_DIS |
                              nic::E1000_RFCTL_NFSR_DIS;

    assert((configured & required) == required);
    assert(((configured ^ current) & ~required) == 0u);
    assert(nic::i219_pch_rfctl_configuration(configured) == configured);
}

int main()
{
    test_tarc0_policy();
    test_tarc1_policy();
    test_rfctl_policy();
    return 0;
}
