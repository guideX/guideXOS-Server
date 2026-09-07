#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

static bool checked_add(uint64_t a, uint64_t b, uint64_t* out) {
    if (!out || b > UINT64_MAX - a) return false;
    *out = a + b;
    return *out > a;
}

static bool validate_machine(const uint8_t* bytes, size_t size, uint16_t machine) {
    if (!bytes || size < 64 || bytes[0] != 0x7f || bytes[1] != 'E' || bytes[2] != 'L' || bytes[3] != 'F' ||
        bytes[4] != 2 || bytes[5] != 1) return false;
    const uint16_t actual = static_cast<uint16_t>(bytes[18] | (bytes[19] << 8));
    return actual == machine;
}

int main() {
    uint8_t header[64]{};
    header[0] = 0x7f; header[1] = 'E'; header[2] = 'L'; header[3] = 'F'; header[4] = 2; header[5] = 1;
    header[18] = 183 & 0xff; header[19] = 183 >> 8;
    if (!validate_machine(header, sizeof(header), 183)) return 1;
    header[18] = 62; header[19] = 0;
    if (validate_machine(header, sizeof(header), 183)) return 2;
    if (validate_machine(header, 19, 183)) return 3;
    uint64_t result = 0;
    if (checked_add(UINT64_MAX - 3, 4, &result)) return 4;
    if (!checked_add(1, 2, &result) || result != 3) return 5;
    std::cout << "Phase 5 host controls: PASS (wrong machine, truncation, overflow)\n";
    return 0;
}
