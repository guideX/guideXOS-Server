#include "include/kernel/system_service_bridge.h"

#include "include/kernel/arch.h"
#include "include/kernel/interrupts.h"
#include "include/kernel/network_settings_provider.h"
#include "include/kernel/pit.h"
#include "include/kernel/serial_debug.h"
#include "system_service_dispatcher.h"

namespace kernel {
namespace system_service_bridge {
namespace {

using namespace gxos::system_service;
using namespace gxos::network_settings;

constexpr uint16_t kCom2 = 0x2F8;
constexpr uint8_t kCom2Irq = 3;
constexpr uint16_t kRxCapacity = 2048;
constexpr uint64_t kPartialRequestTimeoutTicks = 100;

volatile uint8_t s_rxBytes[kRxCapacity]{};
volatile uint16_t s_rxHead = 0;
volatile uint16_t s_rxTail = 0;
volatile bool s_rxOverflow = false;
uint8_t s_request[kMaxRequestBytes]{};
size_t s_requestUsed = 0;
uint64_t s_lastRequestByteTick = 0;
bool s_initialized = false;

void serialIrqHandler()
{
    for (uint32_t count = 0; count < kRxCapacity; ++count) {
        if ((arch::inb(kCom2 + 5) & 0x01u) == 0u) break;
        const uint8_t value = arch::inb(kCom2);
        const uint16_t next = static_cast<uint16_t>((s_rxHead + 1u) % kRxCapacity);
        if (next == s_rxTail) {
            s_rxOverflow = true;
        } else {
            s_rxBytes[s_rxHead] = value;
            s_rxHead = next;
        }
    }
    interrupts::eoi(kCom2Irq);
}

bool tryReadByte(uint8_t* output)
{
    if (!output || s_rxTail == s_rxHead) return false;
    *output = s_rxBytes[s_rxTail];
    s_rxTail = static_cast<uint16_t>((s_rxTail + 1u) % kRxCapacity);
    return true;
}

bool writeBytes(const uint8_t* bytes, size_t length)
{
    if (!bytes || length > kMaxResponseBytes) return false;
    for (size_t i = 0; i < length; ++i) {
        uint32_t waitCount = 0;
        while ((arch::inb(kCom2 + 5) & 0x20u) == 0u) {
            if (++waitCount >= 100000u) return false;
        }
        arch::outb(kCom2, bytes[i]);
    }
    return true;
}

void logRequest(uint16_t type, uint32_t requestId)
{
    serial::puts("[SYSBRIDGE] request received type=");
    serial::put_hex16(type);
    serial::puts(" id=");
    serial::put_hex32(requestId);
    serial::putc('\n');
}

void logAddress(const char* label, const IPv4Value& address)
{
    serial::puts(label);
    if (!address.available) {
        serial::puts("unavailable");
        return;
    }
    char text[16]{};
    if (formatIPv4(address.value, text)) serial::puts(text);
    else serial::puts("unavailable");
}

void serveRequest()
{
    RequestHeader header{};
    const bool headerValid = decodeRequest(s_request, sizeof(s_request), &header);
    const uint16_t type = headerValid ? header.type : 0u;
    const uint32_t requestId = headerValid ? header.requestId : 0u;
    logRequest(type, requestId);

    uint8_t response[kMaxResponseBytes]{};
    size_t responseBytes = 0;
    const Provider provider =
        network_settings_provider::appModelProvider();
    const bool framed = dispatch(s_request, sizeof(s_request), provider,
        DispatchTrust::TrustedSystemServicePeer, response, sizeof(response),
        &responseBytes);
    if (!framed || responseBytes < kResponseHeaderBytes) {
        serial::puts("[SYSBRIDGE] response failed reason=encode\n");
        return;
    }

    ResponseHeader responseHeader{};
    const bool responseHeaderValid = decodeResponseHeader(
        response, responseBytes, &responseHeader);
    if (responseHeaderValid && responseHeader.payloadBytes == kSnapshotWireBytes) {
        NetworkSnapshot snapshot{};
        if (decodeSnapshot(response + kResponseHeaderBytes,
                responseHeader.payloadBytes, &snapshot)) {
            serial::puts("[NETSNAP] generation=");
            serial::put_hex64(snapshot.generation);
            serial::puts(" adapters=");
            serial::put_hex32(snapshot.adapterCount);
            serial::puts(" state=");
            serial::put_hex8(static_cast<uint8_t>(snapshot.state));
            if (snapshot.adapterCount != 0u) {
                const NetworkInterfaceInfo& adapter = snapshot.adapters[0];
                serial::puts(" name=");
                serial::puts(adapter.name);
                serial::puts(" driver=");
                serial::puts(adapter.driver);
                serial::puts(" link=");
                serial::put_hex8(static_cast<uint8_t>(adapter.linkState));
                serial::puts(" mode=");
                serial::put_hex8(static_cast<uint8_t>(adapter.configurationMode));
                serial::puts(" dhcp=");
                serial::put_hex8(static_cast<uint8_t>(adapter.dhcpState));
                serial::puts(" dnsSource=");
                serial::put_hex8(static_cast<uint8_t>(adapter.dnsSource));
                logAddress(" ipv4=", adapter.ipv4Address);
                logAddress(" mask=", adapter.subnetMask);
                logAddress(" gateway=", adapter.gateway);
                logAddress(" dns=", adapter.dns);
            }
            serial::putc('\n');
        }
    }
    if (responseHeaderValid && (responseHeader.status == ResponseStatus::Ok ||
        responseHeader.status == ResponseStatus::Unavailable)) {
        serial::puts("[SYSBRIDGE] authorized service=network.read_snapshot\n");
    } else {
        serial::puts("[SYSBRIDGE] rejected status=");
        serial::put_hex16(responseHeaderValid
            ? static_cast<uint16_t>(responseHeader.status)
            : static_cast<uint16_t>(ResponseStatus::InternalError));
        serial::putc('\n');
    }
    const bool sent = writeBytes(response, responseBytes);
    serial::puts("[SYSBRIDGE] response sent id=");
    serial::put_hex32(requestId);
    serial::puts(" status=");
    serial::put_hex16(responseHeaderValid
        ? static_cast<uint16_t>(responseHeader.status)
        : static_cast<uint16_t>(ResponseStatus::InternalError));
    serial::puts(" result=");
    serial::puts(sent ? "ok\n" : "uart-failed\n");
}

bool requestPrefixHasMagic()
{
    return s_requestUsed >= 4 && readU32(s_request) == kWireMagic;
}

void consumeByte(uint8_t value, uint64_t now)
{
    if (s_requestUsed < sizeof(s_request)) {
        s_request[s_requestUsed++] = value;
        s_lastRequestByteTick = now;
    }
    if (s_requestUsed < sizeof(s_request)) return;

    if (requestPrefixHasMagic()) {
        serveRequest();
        s_requestUsed = 0;
        return;
    }

    // Resynchronize on the fixed magic instead of retaining an unbounded
    // stream buffer after noise or a truncated/disconnected sender.
    for (size_t i = 1; i < sizeof(s_request); ++i) s_request[i - 1] = s_request[i];
    s_requestUsed = sizeof(s_request) - 1;
}

} // namespace

void init()
{
#if ARCH_HAS_PIC_8259
    if (s_initialized) return;
    arch::outb(kCom2 + 1, 0x00); // Disable UART interrupts while configuring.
    arch::outb(kCom2 + 3, 0x80); // Divisor latch access.
    arch::outb(kCom2 + 0, 0x01); // 115200 baud.
    arch::outb(kCom2 + 1, 0x00);
    arch::outb(kCom2 + 3, 0x03); // 8 data bits, no parity, one stop bit.
    arch::outb(kCom2 + 2, 0xC7); // Enable and clear the FIFOs.
    arch::outb(kCom2 + 4, 0x0B); // DTR, RTS, and OUT2.
    s_rxHead = 0;
    s_rxTail = 0;
    s_rxOverflow = false;
    interrupts::register_irq(kCom2Irq, serialIrqHandler);
    arch::outb(kCom2 + 1, 0x01); // Receive-data-available interrupt.
    s_initialized = true;
    serial::puts("[SYSBRIDGE] initialized transport=uart-com2 protocol=1 max-response=");
    serial::put_hex16(static_cast<uint16_t>(kMaxResponseBytes));
    serial::putc('\n');
#endif
}

void poll()
{
    if (!s_initialized) return;
    const uint64_t now = pit::ticks();
    if (s_rxOverflow) {
        s_rxOverflow = false;
        s_rxTail = s_rxHead;
        s_requestUsed = 0;
        serial::puts("[SYSBRIDGE] receive reset reason=bounded-buffer-overflow\n");
    }
    if (s_requestUsed != 0 && now >= s_lastRequestByteTick &&
        now - s_lastRequestByteTick > kPartialRequestTimeoutTicks) {
        s_requestUsed = 0;
        serial::puts("[SYSBRIDGE] receive reset reason=partial-request-timeout\n");
    }
    uint8_t value = 0;
    uint32_t processed = 0;
    while (processed < kRxCapacity && tryReadByte(&value)) {
        consumeByte(value, now);
        ++processed;
    }
}

} // namespace system_service_bridge
} // namespace kernel
