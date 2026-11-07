#!/usr/bin/env python3
"""Verify one guest DNS transaction in a QEMU filter-dump PCAP."""

import argparse
import ipaddress
import struct
import sys


def fail(message):
    raise ValueError(message)


def decode_name(message, offset):
    labels = []
    cursor = offset
    consumed = None
    visited = set()
    while True:
        if cursor >= len(message):
            fail("DNS name exceeds packet bounds")
        length = message[cursor]
        if length & 0xC0 == 0xC0:
            if cursor + 1 >= len(message):
                fail("DNS compression pointer is truncated")
            pointer = ((length & 0x3F) << 8) | message[cursor + 1]
            if pointer in visited or pointer >= len(message):
                fail("DNS compression pointer is invalid or cyclic")
            visited.add(pointer)
            if consumed is None:
                consumed = cursor + 2
            cursor = pointer
            continue
        if length & 0xC0:
            fail("DNS label uses an unsupported label encoding")
        cursor += 1
        if length == 0:
            return ".".join(labels).lower(), (consumed if consumed is not None else cursor)
        if length > 63 or cursor + length > len(message):
            fail("DNS label exceeds packet bounds")
        labels.append(message[cursor:cursor + length].decode("ascii", "replace"))
        cursor += length


def parse_dns(payload):
    if len(payload) < 12:
        fail("DNS message is shorter than its header")
    ident, flags, qdcount, ancount, _, _ = struct.unpack_from("!HHHHHH", payload)
    offset = 12
    questions = []
    for _ in range(qdcount):
        name, offset = decode_name(payload, offset)
        if offset + 4 > len(payload):
            fail("DNS question is truncated")
        qtype, qclass = struct.unpack_from("!HH", payload, offset)
        offset += 4
        questions.append((name, qtype, qclass))
    answers = []
    for _ in range(ancount):
        name, offset = decode_name(payload, offset)
        if offset + 10 > len(payload):
            fail("DNS answer header is truncated")
        rtype, rclass, ttl, rdlength = struct.unpack_from("!HHIH", payload, offset)
        offset += 10
        if offset + rdlength > len(payload):
            fail("DNS answer data is truncated")
        rdata = payload[offset:offset + rdlength]
        offset += rdlength
        if rtype == 1 and rclass == 1 and rdlength == 4:
            answers.append((name, str(ipaddress.IPv4Address(rdata)), ttl))
    return ident, flags, questions, answers


def parse_frame(frame, linktype):
    offset = 0
    if linktype == 1:  # Ethernet
        if len(frame) < 14:
            return None
        ethertype = struct.unpack_from("!H", frame, 12)[0]
        offset = 14
        while ethertype in (0x8100, 0x88A8):
            if len(frame) < offset + 4:
                return None
            ethertype = struct.unpack_from("!H", frame, offset + 2)[0]
            offset += 4
        if ethertype != 0x0800:
            return None
    elif linktype not in (101, 228):  # Raw IPv4 or LINKTYPE_IPV4
        fail(f"unsupported PCAP link type {linktype}")

    packet = frame[offset:]
    if len(packet) < 20 or packet[0] >> 4 != 4:
        return None
    ip_header_len = (packet[0] & 0x0F) * 4
    total_len = struct.unpack_from("!H", packet, 2)[0]
    fragment = struct.unpack_from("!H", packet, 6)[0]
    if ip_header_len < 20 or total_len < ip_header_len or len(packet) < total_len:
        return None
    if packet[9] != 17 or fragment & 0x3FFF:
        return None
    src = str(ipaddress.IPv4Address(packet[12:16]))
    dst = str(ipaddress.IPv4Address(packet[16:20]))
    udp = packet[ip_header_len:total_len]
    if len(udp) < 8:
        return None
    src_port, dst_port, udp_len, _ = struct.unpack_from("!HHHH", udp)
    if udp_len < 8 or udp_len > len(udp):
        return None
    payload = udp[8:udp_len]
    try:
        ident, flags, questions, answers = parse_dns(payload)
    except ValueError:
        return None
    return src, dst, src_port, dst_port, ident, flags, questions, answers


def load_packets(path):
    data = open(path, "rb").read()
    if len(data) < 24:
        fail("PCAP is shorter than its global header")
    magic = data[:4]
    if magic in (b"\xd4\xc3\xb2\xa1", b"\x4d\x3c\xb2\xa1"):
        order = "<"
    elif magic in (b"\xa1\xb2\xc3\xd4", b"\xa1\xb2\x3c\x4d"):
        order = ">"
    else:
        fail("file does not have a supported classic PCAP header")
    linktype = struct.unpack_from(order + "I", data, 20)[0]
    packets = []
    offset = 24
    while offset + 16 <= len(data):
        _, _, captured_len, _ = struct.unpack_from(order + "IIII", data, offset)
        offset += 16
        if captured_len > len(data) - offset:
            fail("PCAP packet record is truncated")
        parsed = parse_frame(data[offset:offset + captured_len], linktype)
        offset += captured_len
        if parsed is not None:
            packets.append(parsed)
    return packets


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--pcap", required=True)
    parser.add_argument("--hostname", required=True)
    parser.add_argument("--address", required=True)
    parser.add_argument("--query-id", required=True, type=int)
    parser.add_argument("--guest-ip", default="10.0.2.15")
    parser.add_argument("--resolver", default="10.0.2.3")
    args = parser.parse_args()
    hostname = args.hostname.rstrip(".").lower()
    expected_address = str(ipaddress.IPv4Address(args.address))
    queries = []
    for packet in load_packets(args.pcap):
        src, dst, sport, dport, ident, flags, questions, answers = packet
        if (src == args.guest_ip and dst == args.resolver and dport == 53 and
                ident == args.query_id and not flags & 0x8000 and
                questions == [(hostname, 1, 1)]):
            queries.append((sport, ident))
    if not queries:
        fail(f"no guest UDP/53 A query for {hostname} with transaction id {args.query_id}")

    for source_port, ident in queries:
        for packet in load_packets(args.pcap):
            src, dst, sport, dport, response_id, flags, questions, answers = packet
            if (src != args.resolver or dst != args.guest_ip or sport != 53 or
                    dport != source_port or response_id != ident or not flags & 0x8000 or
                    flags & 0x000F != 0 or questions != [(hostname, 1, 1)]):
                continue
            if any(owner == hostname and address == expected_address
                   for owner, address, _ in answers):
                print("[GUEST-DNS-PCAP] PASS "
                      f"guest={args.guest_ip}:{source_port} resolver={args.resolver}:53 "
                      f"id={ident} qname={hostname} qtype=A answer={expected_address}")
                return 0
    fail("matching response from the configured resolver did not contain the parsed A record")


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"[GUEST-DNS-PCAP] FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
