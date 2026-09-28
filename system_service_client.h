#pragma once

#include "system_service_protocol.h"

namespace gxos {
namespace system_service {

class SystemServiceClient {
public:
    explicit SystemServiceClient(const Transport& transport,
                                 uint32_t timeoutMs = kDefaultRequestTimeoutMs)
        : m_transport(transport), m_timeoutMs(timeoutMs)
    {
    }

    ClientResult getNetworkSnapshot(network_settings::NetworkSnapshot* output)
    {
        using namespace network_settings;
        if (!output) return ClientResult::InvalidArgument;
        *output = NetworkSnapshot{};
        output->backend = Backend::Unavailable;
        output->state = SnapshotState::Unavailable;
        if (!m_transport.transact) return ClientResult::Unavailable;

        uint8_t request[kMaxRequestBytes]{};
        size_t requestBytes = 0;
        uint32_t requestId = m_nextRequestId++;
        if (requestId == 0u) {
            requestId = m_nextRequestId++;
            if (requestId == 0u) requestId = 1u;
        }
        if (!encodeRequest(kProtocolVersion,
                static_cast<uint16_t>(RequestType::GetNetworkSnapshot),
                requestId, 0u, request, sizeof(request), &requestBytes))
            return ClientResult::Failed;

        uint8_t response[kMaxResponseBytes]{};
        size_t responseBytes = 0;
        const TransportResult transportResult = m_transport.transact(
            m_transport.context, request, requestBytes, response,
            sizeof(response), &responseBytes, m_timeoutMs);
        if (transportResult == TransportResult::Timeout) return ClientResult::Timeout;
        if (transportResult == TransportResult::Disconnected) return ClientResult::Disconnected;
        if (transportResult != TransportResult::Ok) return ClientResult::Failed;

        ResponseHeader header{};
        if (!decodeResponseHeader(response, responseBytes, &header) ||
            header.type != 0x8001u || header.requestId != requestId)
            return ClientResult::ProtocolError;
        if (header.payloadBytes == kSnapshotWireBytes) {
            NetworkSnapshot snapshot{};
            if (!decodeSnapshot(response + kResponseHeaderBytes,
                    header.payloadBytes, &snapshot)) return ClientResult::ProtocolError;
            *output = snapshot;
        }

        switch (header.status) {
        case ResponseStatus::Ok: return ClientResult::Ok;
        case ResponseStatus::Unavailable: return ClientResult::Unavailable;
        case ResponseStatus::Unauthorized: return ClientResult::Unauthorized;
        case ResponseStatus::Unsupported: return ClientResult::Unsupported;
        case ResponseStatus::InvalidRequest: return ClientResult::InvalidArgument;
        default: return ClientResult::ProtocolError;
        }
    }

    ClientResult setNetworkConfiguration(
        const network_settings::NetworkConfigurationCandidate& candidate,
        network_settings::ConfigurationTransactionResult* transactionOutput = nullptr)
    {
        using namespace network_settings;
        if (transactionOutput) *transactionOutput = ConfigurationTransactionResult{};
        if (validateCandidate(candidate) != ConfigurationField::None)
            return ClientResult::InvalidArgument;
        if (!m_transport.transact) return ClientResult::Unavailable;

        uint8_t request[kMaxRequestBytes]{};
        size_t requestBytes = 0;
        uint32_t requestId = m_nextRequestId++;
        if (requestId == 0u) {
            requestId = m_nextRequestId++;
            if (requestId == 0u) requestId = 1u;
        }
        if (!encodeConfigurationRequest(candidate, requestId, request,
                sizeof(request), &requestBytes)) return ClientResult::InvalidArgument;

        uint8_t response[kMaxResponseBytes]{};
        size_t responseBytes = 0;
        const TransportResult transportResult = m_transport.transact(
            m_transport.context, request, requestBytes, response,
            sizeof(response), &responseBytes, m_timeoutMs);
        if (transportResult == TransportResult::Timeout) return ClientResult::Timeout;
        if (transportResult == TransportResult::Disconnected) return ClientResult::Disconnected;
        if (transportResult != TransportResult::Ok) return ClientResult::Failed;

        ResponseHeader header{};
        if (!decodeResponseHeader(response, responseBytes, &header) ||
            header.type != 0x8002u || header.requestId != requestId)
            return ClientResult::ProtocolError;
        if (header.status == ResponseStatus::Ok) {
            ConfigurationTransactionResult transaction{};
            if (!decodeConfigurationResult(response + kResponseHeaderBytes,
                    header.payloadBytes, &transaction)) return ClientResult::ProtocolError;
            if (transactionOutput) *transactionOutput = transaction;
        }
        switch (header.status) {
        case ResponseStatus::Ok: return ClientResult::Ok;
        case ResponseStatus::Unavailable: return ClientResult::Unavailable;
        case ResponseStatus::Unauthorized: return ClientResult::Unauthorized;
        case ResponseStatus::Unsupported: return ClientResult::Unsupported;
        case ResponseStatus::InvalidRequest: return ClientResult::InvalidArgument;
        default: return ClientResult::ProtocolError;
        }
    }

private:
    Transport m_transport{};
    uint32_t m_timeoutMs{kDefaultRequestTimeoutMs};
    uint32_t m_nextRequestId{1};
};

} // namespace system_service
} // namespace gxos
