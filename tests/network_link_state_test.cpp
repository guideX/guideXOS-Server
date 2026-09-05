// Hosted deterministic checks for the Phase 12 link-source and refresh
// decision logic. These exercise register interpretation without fabricating
// a physical AIDA_LPT result or claiming that hosted code has carrier.

#include <assert.h>

#include "kernel/nic.h"

using namespace kernel;

static nic::I219LinkEvidence evidence(bool firstValid, uint16_t first,
                                      bool secondValid, uint16_t second,
                                      bool statusValid, uint16_t status)
{
    nic::I219LinkEvidence result = {};
    result.bmsrFirstValid = firstValid;
    result.bmsrFirst = first;
    result.bmsrSecondValid = secondValid;
    result.bmsrSecond = second;
    result.phyStatusValid = statusValid;
    result.phyStatus = status;
    return result;
}

int main()
{
    assert(nic::I219_PHY_BMSR_REG == 1);
    assert(nic::I219_PHY_BMSR_LINK == (1u << 2));
    assert(nic::I219_PHY_STATUS_REG == 26);
    assert(nic::I219_PHY_STATUS_LINK == (1u << 6));
    assert(nic::I219_LINK_REFRESH_POLL_LIMIT > 1u);
    assert(nic::I219_LINK_SETTLE_POLL_LIMIT >
           nic::I219_LINK_REFRESH_POLL_LIMIT);
    assert(!nic::link_refresh_poll_exhausted(0, 20));
    assert(!nic::link_refresh_poll_exhausted(19, 20));
    assert(nic::link_refresh_poll_exhausted(20, 20));
    assert(!nic::link_refresh_poll_exhausted(20, 0));

    // The second BMSR read is authoritative after the RO/LL first read.
    nic::LinkEvaluation latchClearedUp = nic::evaluate_i219_link(
        evidence(true, 0, true, nic::I219_PHY_BMSR_LINK,
                 false, 0));
    assert(latchClearedUp.state == nic::NIC_LINK_UP);
    assert(latchClearedUp.source == nic::LinkSource::Phy);
    assert(latchClearedUp.result == nic::LinkRefreshResult::Up);

    nic::LinkEvaluation latchClearedDown = nic::evaluate_i219_link(
        evidence(true, nic::I219_PHY_BMSR_LINK, true, 0,
                 false, 0));
    assert(latchClearedDown.state == nic::NIC_LINK_DOWN);
    assert(latchClearedDown.result == nic::LinkRefreshResult::Down);

    // PHY Status register 26 is useful corroborating I219 evidence.
    nic::LinkEvaluation combinedUp = nic::evaluate_i219_link(
        evidence(true, 0, true, nic::I219_PHY_BMSR_LINK,
                 true, nic::I219_PHY_STATUS_LINK));
    assert(combinedUp.state == nic::NIC_LINK_UP);
    assert(combinedUp.source == nic::LinkSource::Combined);
    assert(combinedUp.result == nic::LinkRefreshResult::Up);

    nic::LinkEvaluation combinedDown = nic::evaluate_i219_link(
        evidence(true, nic::I219_PHY_BMSR_LINK, true, 0,
                 true, 0));
    assert(combinedDown.state == nic::NIC_LINK_DOWN);
    assert(combinedDown.source == nic::LinkSource::Combined);

    // A disagreement is observable uncertainty, never a guessed DOWN.
    nic::LinkEvaluation conflict = nic::evaluate_i219_link(
        evidence(true, 0, true, 0, true, nic::I219_PHY_STATUS_LINK));
    assert(conflict.state == nic::NIC_LINK_UNKNOWN);
    assert(conflict.result == nic::LinkRefreshResult::Conflict);
    assert(conflict.source == nic::LinkSource::Combined);

    // If both PHY reads are unavailable, the caller must report read error;
    // a valid MAC register alone does not silently turn that into DOWN.
    nic::LinkEvaluation noPhy = nic::evaluate_i219_link(
        evidence(false, 0, false, 0, false, 0));
    assert(noPhy.state == nic::NIC_LINK_READ_ERROR);
    assert(noPhy.result == nic::LinkRefreshResult::ReadError);
    assert(noPhy.state != nic::NIC_LINK_DOWN);

    // The vendor PHY status is a bounded fallback when the BMSR transaction
    // is unavailable, while still identifying the source as PHY.
    nic::LinkEvaluation statusOnly = nic::evaluate_i219_link(
        evidence(false, 0, false, 0, true, nic::I219_PHY_STATUS_LINK));
    assert(statusOnly.state == nic::NIC_LINK_UP);
    assert(statusOnly.source == nic::LinkSource::Phy);

    assert(nic::link_state_from_mac_status(nic::E1000_STATUS_LU) ==
           nic::NIC_LINK_UP);
    assert(nic::link_state_from_mac_status(0) == nic::NIC_LINK_DOWN);
    assert(nic::link_state_from_mac_status(0xFFFFFFFFu) ==
           nic::NIC_LINK_READ_ERROR);

    assert(nic::link_refresh_result_name(nic::LinkRefreshResult::Conflict) != nullptr);
    assert(nic::link_refresh_result_name(nic::LinkRefreshResult::ReadError) != nullptr);
    assert(nic::link_source_name(nic::LinkSource::Combined) != nullptr);
    return 0;
}
