#include <assert.h>

#include "kernel/nic.h"
#include "kernel/shell.h"

using namespace kernel;

int main()
{
    const uint32_t allBitsSet = 0xFFFFFFFFu;
    const uint32_t configured = nic::i219_spt_tx_snoop_configuration(allBitsSet);
    assert((configured & nic::E1000_GCR_TX_NO_SNOOP_MASK) == 0u);
    assert(((configured ^ allBitsSet) &
            ~nic::E1000_GCR_TX_NO_SNOOP_MASK) == 0u);
    assert(nic::i219_spt_tx_snoop_configuration_valid(configured));
    assert(!nic::i219_spt_tx_snoop_configuration_valid(allBitsSet));
    assert(nic::i219_spt_tx_snoop_change_required(allBitsSet));
    assert(!nic::i219_spt_tx_snoop_change_required(configured));

    const uint32_t rxAndOther = 0xA5A50007u;
    const uint32_t rxAndOtherAfter =
        nic::i219_spt_tx_snoop_configuration(rxAndOther);
    assert((rxAndOtherAfter & 0x7u) == (rxAndOther & 0x7u));
    assert(((rxAndOtherAfter ^ rxAndOther) &
            ~nic::E1000_GCR_TX_NO_SNOOP_MASK) == 0u);
    assert(nic::i219_spt_tx_snoop_configuration_valid(rxAndOtherAfter));

    assert(shell::nicinfo_mode_from_args("tx", "phase25", nullptr) ==
           shell::NICINFO_MODE_TX_PHASE25);
    assert(shell::nicinfo_mode_from_args("tx", "phase25", "again") ==
           shell::NICINFO_MODE_INVALID);
    assert(shell::NICINFO_TX_PHASE25_EXPECTED_LINES <=
           shell::NICINFO_TX_PHASE25_MAX_LINES);
    return 0;
}
