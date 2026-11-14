#include "include/gxos_roughtime_acquisition.h"
#include "include/gxos_roughtime_agreement_internal.h"

#include "../../gxos_clock_assurance.h"
#include "include/kernel/ipv4.h"
#include "include/kernel/nic.h"
#include "include/kernel/pit.h"
#include "include/kernel/secure_random.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/socket.h"
#include "include/kernel/udp.h"

namespace kernel {
namespace roughtime {
namespace {

static const uint64_t kPitTickNanoseconds = 10000000ULL;
static const uint64_t kRequestDeadlineTicks = 200ULL;
static const uint32_t kMaximumReceivePolls = 50000U;
static const uint32_t kMaximumUdpSendAttempts = 2U;

struct AcquisitionWorkspace {
    uint8_t nonce[32];
    uint8_t request[GXOS_ROUGHTIME_V1_UDP_REQUEST_BYTES];
    uint8_t response[GXOS_ROUGHTIME_V1_MAX_UDP_RESPONSE_BYTES];
    gxos_roughtime_v1_workspace verifier;
};

static void clear_bytes(void* memory, size_t length)
{
    uint8_t* bytes = static_cast<uint8_t*>(memory);
    for (size_t i = 0; i < length; ++i) bytes[i] = 0;
}

static bool pit_ticks_to_ns(uint64_t ticks, uint64_t* nanoseconds)
{
    if (!nanoseconds || ticks > (~static_cast<uint64_t>(0) / kPitTickNanoseconds)) {
        return false;
    }
    *nanoseconds = ticks * kPitTickNanoseconds;
    return true;
}

static int secure_random_fill(void*, uint8_t* output, size_t output_length)
{
    return secure_random::fill(output, output_length) ? 1 : 0;
}

class SocketLease {
public:
    SocketLease() : m_fd(socket::INVALID_SOCKET), m_port(0) {}
    ~SocketLease() { release(); }

    bool open()
    {
        m_fd = socket::udp_socket();
        if (m_fd < 0) return false;
        m_port = udp::alloc_ephemeral_port();
        if (m_port == 0) {
            release();
            return false;
        }
        if (socket::udp_bind(m_fd, m_port) != socket::SOCK_OK) {
            release();
            return false;
        }
        return true;
    }

    int fd() const { return m_fd; }

private:
    void release()
    {
        if (m_fd >= 0) {
            socket::udp_close(m_fd);
            m_fd = socket::INVALID_SOCKET;
        }
        if (m_port != 0) {
            // udp_close unbinds a successfully bound socket; this call also
            // releases the allocator slot after a bind failure.
            udp::free_ephemeral_port(m_port);
            m_port = 0;
        }
    }

    int m_fd;
    uint16_t m_port;
};

static void log_result(const gxos_roughtime_v1_acquisition_result* result)
{
    serial::puts("[ROUGHTIME] result=");
    serial::puts(gxos_roughtime_acquisition_status_name(result->status));
    if (result->status == GXOS_ROUGHTIME_ACQUISITION_SUCCESS) {
        serial::puts(" response_bytes=");
        serial::put_hex32(result->response_bytes);
        serial::puts(" server_key_id=");
        for (size_t i = 0; i < sizeof(result->evidence.server_key_identity); ++i) {
            serial::put_hex8(result->evidence.server_key_identity[i]);
        }
        serial::puts(" midpoint=");
        serial::put_hex64(result->evidence.server_midpoint_unix_seconds);
        serial::puts(" radius=");
        serial::put_hex32(result->evidence.radius_seconds);
        serial::puts(" earliest=");
        serial::put_hex64(result->evidence.earliest_unix_seconds);
        serial::puts(" latest=");
        serial::put_hex64(result->evidence.latest_unix_seconds);
        serial::puts(" uncertainty=");
        serial::put_hex64(result->evidence.estimated_uncertainty_seconds);
        serial::puts(" rtt_ns=");
        serial::put_hex64(result->evidence.measured_transport_delay_ns);
        serial::puts(" trusted_time=not-accepted clock_unchanged=yes\n");
    } else {
        serial::putc('\n');
    }
}

} // namespace

gxos_roughtime_acquisition_status acquire_ipv4(
    uint32_t server_ipv4,
    uint16_t server_port,
    const uint8_t server_public_key[32],
    gxos_roughtime_v1_acquisition_result* result)
{
    if (!result) return GXOS_ROUGHTIME_ACQUISITION_INVALID_ARGUMENT;
    clear_bytes(result, sizeof(*result));
    result->status = GXOS_ROUGHTIME_ACQUISITION_INVALID_ARGUMENT;

    if (!server_public_key || server_ipv4 == 0 || server_port == 0) {
        log_result(result);
        return result->status;
    }
    if (!ipv4::is_configured() || nic::get_link_state() != nic::NIC_LINK_UP) {
        result->status = GXOS_ROUGHTIME_ACQUISITION_NETWORK_UNAVAILABLE;
        log_result(result);
        return result->status;
    }

    AcquisitionWorkspace workspace;
    clear_bytes(&workspace, sizeof(workspace));
    size_t request_length = 0;
    const gxos_roughtime_acquisition_status request_status =
        gxos_roughtime_v1_build_request_with_random(
            secure_random_fill, nullptr, server_public_key,
            workspace.nonce, workspace.request, sizeof(workspace.request),
            &request_length);
    if (request_status != GXOS_ROUGHTIME_ACQUISITION_SUCCESS) {
        result->status = request_status;
        clear_bytes(&workspace, sizeof(workspace));
        log_result(result);
        return result->status;
    }

    SocketLease socket_lease;
    if (!socket_lease.open()) {
        result->status = GXOS_ROUGHTIME_ACQUISITION_SOCKET_FAILURE;
        clear_bytes(&workspace, sizeof(workspace));
        log_result(result);
        return result->status;
    }

    const uint64_t start_ticks = pit::ticks();
    uint64_t sent_ns = 0;
    if (!pit_ticks_to_ns(start_ticks, &sent_ns)) {
        result->status = GXOS_ROUGHTIME_ACQUISITION_INVALID_ARGUMENT;
        clear_bytes(&workspace, sizeof(workspace));
        log_result(result);
        return result->status;
    }

    const socket::SockAddr server = socket::make_sockaddr(server_ipv4, server_port);
    char server_ip_text[16];
    ipv4::ip_to_string(server_ipv4, server_ip_text);
    serial::puts("[ROUGHTIME] request-start destination=");
    serial::puts(server_ip_text);
    serial::putc(':');
    serial::put_hex32(server_port);
    serial::puts(" request_bytes=");
    serial::put_hex32(static_cast<uint32_t>(request_length));
    serial::putc('\n');

    int sent = socket::udp_sendto(socket_lease.fd(), workspace.request,
        static_cast<uint16_t>(request_length), &server);
    for (uint32_t attempt = 1U;
         sent != static_cast<int>(request_length) && attempt < kMaximumUdpSendAttempts;
         ++attempt) {
        const ipv4::SendDiagnostics* previous_send = ipv4::get_last_send_diagnostics();
        const bool neighbor_timeout = previous_send && previous_send->statusValid &&
            previous_send->status == ipv4::IP_ERR_NO_ROUTE &&
            previous_send->stage == ipv4::SEND_STAGE_NEIGHBOR_RESOLUTION &&
            (previous_send->neighborOutcome == ipv4::NEIGHBOR_OUTCOME_ARP_REPLY_TIMEOUT ||
             previous_send->neighborOutcome == ipv4::NEIGHBOR_OUTCOME_GATEWAY_ARP_REPLY_TIMEOUT);
        const uint64_t retry_ticks = pit::ticks();
        if (!neighbor_timeout || retry_ticks < start_ticks ||
            retry_ticks - start_ticks >= kRequestDeadlineTicks) {
            break;
        }
        serial::puts("[ROUGHTIME] send-retry=neighbor-resolution attempt=");
        serial::put_hex32(attempt + 1U);
        serial::putc('\n');
        ipv4::poll_network();
        sent = socket::udp_sendto(socket_lease.fd(), workspace.request,
            static_cast<uint16_t>(request_length), &server);
    }
    if (sent != static_cast<int>(request_length)) {
        const ipv4::SendDiagnostics* send_diagnostics = ipv4::get_last_send_diagnostics();
        serial::puts("[ROUGHTIME] send-failure returned=");
        serial::put_hex32(static_cast<uint32_t>(sent));
        if (send_diagnostics && send_diagnostics->statusValid) {
            serial::puts(" ip_status=");
            serial::puts(ipv4::status_name(send_diagnostics->status));
            serial::puts(" stage=");
            serial::puts(ipv4::send_stage_name(send_diagnostics->stage));
            serial::puts(" neighbor=");
            serial::puts(ipv4::neighbor_resolution_outcome_name(
                send_diagnostics->neighborOutcome));
            if (send_diagnostics->nicStatusValid) {
                serial::puts(" nic_status=");
                serial::put_hex32(send_diagnostics->nicStatus);
            }
        }
        serial::putc('\n');
        result->status = GXOS_ROUGHTIME_ACQUISITION_SEND_FAILURE;
        clear_bytes(&workspace, sizeof(workspace));
        log_result(result);
        return result->status;
    }

    for (uint32_t poll = 0; poll < kMaximumReceivePolls; ++poll) {
        ipv4::poll_network();
        const int pending = socket::udp_pending(socket_lease.fd());
        if (pending < 0) {
            result->status = GXOS_ROUGHTIME_ACQUISITION_SOCKET_FAILURE;
            clear_bytes(&workspace, sizeof(workspace));
            log_result(result);
            return result->status;
        }
        if (pending != 0) {
            socket::SockAddr source = { 0, 0 };
            const int received = socket::udp_recvfrom(socket_lease.fd(), workspace.response,
                GXOS_ROUGHTIME_V1_MAX_UDP_RESPONSE_BYTES, &source);
            if (received == socket::SOCK_ERR_MSGSIZE) {
                result->status = GXOS_ROUGHTIME_ACQUISITION_RESPONSE_TOO_LARGE;
                clear_bytes(&workspace, sizeof(workspace));
                log_result(result);
                return result->status;
            }
            if (received == socket::SOCK_ERR_WOULDBLOCK) continue;
            if (received < 0) {
                result->status = GXOS_ROUGHTIME_ACQUISITION_SOCKET_FAILURE;
                clear_bytes(&workspace, sizeof(workspace));
                log_result(result);
                return result->status;
            }

            const uint64_t received_ticks = pit::ticks();
            uint64_t received_ns = 0;
            if (!pit_ticks_to_ns(received_ticks, &received_ns)) {
                result->status = GXOS_ROUGHTIME_ACQUISITION_INVALID_ARGUMENT;
                clear_bytes(&workspace, sizeof(workspace));
                log_result(result);
                return result->status;
            }
            const gxos_roughtime_acquisition_status response_status =
                gxos_roughtime_v1_dispatch_response(
                    workspace.response, static_cast<size_t>(received),
                    source.addr, source.port, server_ipv4, server_port,
                    workspace.request, request_length, workspace.nonce,
                    server_public_key, sent_ns, received_ns,
                    &workspace.verifier, &result->evidence);
            if (response_status != GXOS_ROUGHTIME_ACQUISITION_SUCCESS) {
                result->status = response_status;
                clear_bytes(&workspace, sizeof(workspace));
                log_result(result);
                return result->status;
            }

            gxos::clockassurance::AuthenticatedEvidence clock_evidence = {
                true,
                true,
                result->evidence.signature_verified != 0,
                result->evidence.nonce_matched != 0,
                static_cast<int64_t>(result->evidence.earliest_unix_seconds),
                static_cast<int64_t>(result->evidence.latest_unix_seconds),
                received_ns / 1000000000ULL,
                GXOS_ROUGHTIME_V1_EVIDENCE_MAX_AGE_SECONDS
            };
            result->clock_evidence_contract_valid =
                gxos::clockassurance::assess_authenticated_evidence(
                    clock_evidence, received_ns / 1000000000ULL) ==
                gxos::clockassurance::EvidenceResult::Accepted ? 1U : 0U;
            if (!result->clock_evidence_contract_valid ||
                received_ns > (~static_cast<uint64_t>(0) -
                    static_cast<uint64_t>(GXOS_ROUGHTIME_V1_EVIDENCE_MAX_AGE_SECONDS) *
                    1000000000ULL)) {
                result->status = GXOS_ROUGHTIME_ACQUISITION_CLOCK_EVIDENCE_REJECTED;
                clear_bytes(&workspace, sizeof(workspace));
                log_result(result);
                return result->status;
            }

            if (!agreement_internal::seal_acquisition_evidence(
                    result->evidence, result->evidence_expires_monotonic_ns,
                    &result->agreement_evidence)) {
                result->status = GXOS_ROUGHTIME_ACQUISITION_CLOCK_EVIDENCE_REJECTED;
                clear_bytes(&workspace, sizeof(workspace));
                log_result(result);
                return result->status;
            }

            result->status = GXOS_ROUGHTIME_ACQUISITION_SUCCESS;
            result->server_ipv4 = server_ipv4;
            result->server_port = server_port;
            result->request_bytes = static_cast<uint16_t>(request_length);
            result->response_bytes = static_cast<uint16_t>(received);
            result->evidence_expires_monotonic_ns = received_ns +
                static_cast<uint64_t>(GXOS_ROUGHTIME_V1_EVIDENCE_MAX_AGE_SECONDS) *
                1000000000ULL;
            clear_bytes(&workspace, sizeof(workspace));
            log_result(result);
            return result->status;
        }

        const uint64_t current_ticks = pit::ticks();
        if (current_ticks < start_ticks || current_ticks - start_ticks >= kRequestDeadlineTicks) {
            break;
        }
    }

    result->status = GXOS_ROUGHTIME_ACQUISITION_TIMEOUT;
    clear_bytes(&workspace, sizeof(workspace));
    log_result(result);
    return result->status;
}

} // namespace roughtime
} // namespace kernel
