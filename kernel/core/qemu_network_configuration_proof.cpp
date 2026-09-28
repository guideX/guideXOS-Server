#include "include/kernel/qemu_network_configuration_proof.h"

#include "include/kernel/dns.h"
#include "include/kernel/ipv4.h"
#include "include/kernel/network_settings_provider.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace qemu_network_configuration_proof {

void run()
{
#if defined(GXOS_QEMU_NETWORK_CONFIGURATION_TRANSACTION_PROOF)
    using namespace gxos::network_settings;
    NetworkSnapshot initial{};
    if (network_settings_provider::readSnapshot(&initial) != Result::Ok ||
        initial.state != SnapshotState::AdaptersAvailable || initial.adapterCount != 1u) {
        serial::puts("[NETTX-PROOF] result=FAIL stage=initial-snapshot\n");
        return;
    }

    const NetworkInterfaceInfo& adapter = initial.adapters[0];
    if (!adapter.ipv4Address.available || !adapter.subnetMask.available ||
        !adapter.gateway.available) {
        serial::puts("[NETTX-PROOF] result=FAIL stage=restore-input\n");
        return;
    }
    const uint32_t restoreDns = adapter.dns.available ? adapter.dns.value : dns::get_server();
    if (!validUnicastIPv4(restoreDns)) {
        serial::puts("[NETTX-PROOF] result=FAIL stage=restore-dns\n");
        return;
    }

    NetworkConfigurationCandidate testCandidate{};
    testCandidate.expectedGeneration = initial.generation;
    testCandidate.interfaceId = adapter.interfaceId;
    for (size_t i = 0; i < kInterfaceIdBytes; ++i)
        testCandidate.stableId[i] = adapter.stableId[i];
    testCandidate.mode = NetworkMode::Static;
    testCandidate.dnsMode = DnsMode::Manual;
    testCandidate.staticIPv4 = IPv4Configuration{
        0x0A172A0Au, 0xFFFFFF00u, 0x0A172A01u, 0x0A172A35u
    };
    const ConfigurationTransactionResult applied =
        network_settings_provider::applyCandidateLocally(testCandidate);
    NetworkSnapshot configured{};
    if (applied.outcome != TransactionOutcome::Success ||
        network_settings_provider::readSnapshot(&configured) != Result::Ok ||
        configured.generation <= initial.generation || configured.adapterCount != 1u ||
        configured.adapters[0].configurationMode != ConfigurationMode::Static ||
        !configured.adapters[0].ipv4Address.available ||
        configured.adapters[0].ipv4Address.value != testCandidate.staticIPv4.address ||
        !configured.adapters[0].subnetMask.available ||
        configured.adapters[0].subnetMask.value != testCandidate.staticIPv4.subnetMask ||
        !configured.adapters[0].gateway.available ||
        configured.adapters[0].gateway.value != testCandidate.staticIPv4.gateway ||
        !configured.adapters[0].dns.available ||
        configured.adapters[0].dns.value != testCandidate.staticIPv4.dns) {
        serial::puts("[NETTX-PROOF] result=FAIL stage=static-apply-verify\n");
        return;
    }

    NetworkConfigurationCandidate restoreCandidate{};
    restoreCandidate.expectedGeneration = configured.generation;
    restoreCandidate.interfaceId = adapter.interfaceId;
    for (size_t i = 0; i < kInterfaceIdBytes; ++i)
        restoreCandidate.stableId[i] = adapter.stableId[i];
    restoreCandidate.mode = NetworkMode::Static;
    restoreCandidate.dnsMode = DnsMode::Manual;
    restoreCandidate.staticIPv4 = IPv4Configuration{
        adapter.ipv4Address.value, adapter.subnetMask.value,
        adapter.gateway.value, restoreDns
    };
    const ConfigurationTransactionResult restored =
        network_settings_provider::applyCandidateLocally(restoreCandidate);
    NetworkSnapshot finalSnapshot{};
    if (restored.outcome != TransactionOutcome::Success ||
        network_settings_provider::readSnapshot(&finalSnapshot) != Result::Ok ||
        finalSnapshot.adapterCount != 1u || finalSnapshot.generation <= configured.generation ||
        finalSnapshot.adapters[0].ipv4Address.value != restoreCandidate.staticIPv4.address ||
        finalSnapshot.adapters[0].subnetMask.value != restoreCandidate.staticIPv4.subnetMask ||
        finalSnapshot.adapters[0].gateway.value != restoreCandidate.staticIPv4.gateway ||
        finalSnapshot.adapters[0].dns.value != restoreCandidate.staticIPv4.dns) {
        serial::puts("[NETTX-PROOF] result=FAIL stage=restore-verify\n");
        return;
    }

    NetworkSnapshot beforeRollback{};
    network_settings_provider::readSnapshot(&beforeRollback);
    NetworkConfigurationCandidate faultCandidate = testCandidate;
    faultCandidate.expectedGeneration = beforeRollback.generation;
    faultCandidate.staticIPv4.address = 0x0A182A0Au;
    faultCandidate.staticIPv4.gateway = 0x0A182A01u;
    faultCandidate.staticIPv4.dns = 0x0A182A35u;
    ipv4::qemu_fail_next_configuration_replace();
    const ConfigurationTransactionResult rolledBack =
        network_settings_provider::applyCandidateLocally(faultCandidate);
    NetworkSnapshot afterRollback{};
    network_settings_provider::readSnapshot(&afterRollback);
    if (rolledBack.outcome != TransactionOutcome::ApplyFailedRolledBack ||
        !sameSnapshot(beforeRollback, afterRollback)) {
        serial::puts("[NETTX-PROOF] result=FAIL stage=rollback-verify\n");
        return;
    }

    testCandidate.expectedGeneration = initial.generation;
    NetworkSnapshot beforeRejected{};
    network_settings_provider::readSnapshot(&beforeRejected);
    const ConfigurationTransactionResult rejected =
        network_settings_provider::applyCandidateLocally(testCandidate);
    NetworkSnapshot afterRejected{};
    network_settings_provider::readSnapshot(&afterRejected);
    if (rejected.outcome != TransactionOutcome::StaleInterface ||
        !sameSnapshot(beforeRejected, afterRejected)) {
        serial::puts("[NETTX-PROOF] result=FAIL stage=stale-rejection\n");
        return;
    }

    serial::puts("[NETTX-PROOF] result=PASS initialGeneration=");
    serial::put_hex64(initial.generation);
    serial::puts(" appliedGeneration=");
    serial::put_hex64(configured.generation);
    serial::puts(" restoredGeneration=");
    serial::put_hex64(finalSnapshot.generation);
    serial::puts(" rollback=verified rejected=stale unchanged=yes\n");
#endif
}

} // namespace qemu_network_configuration_proof
} // namespace kernel
