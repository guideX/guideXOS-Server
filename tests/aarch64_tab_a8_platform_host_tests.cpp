#include <cstdio>
#include <cstring>

#include "../aarch64/tab_a8/tab_a8_platform.h"

static bool expect(bool condition, const char* name)
{
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", name);
    return condition;
}

int main()
{
    bool ok = true;
    gxos_aarch64_tab_a8_platform_map map = {};
    map.size = sizeof(map);
    map.version = 1;
    map.evidence = GXOS_AARCH64_TAB_A8_EVIDENCE_STOCK_PROVEN;
    map.value_state = GXOS_AARCH64_TAB_A8_VALUE_PRESENT;
    map.uart_base = UINT64_C(0x70100000);
    map.uart_irq = 45;
    map.flags = GXOS_AARCH64_TAB_A8_MAP_FLAG_UART_VALID;
    ok &= expect(map.size == sizeof(gxos_aarch64_tab_a8_platform_map),
                 "platform map has self-described size");
    ok &= expect(gxos_aarch64_tab_a8_identity_matches("SM-X200", "gta8wifi"),
                 "expected SM-X200 identity accepted");
    ok &= expect(!gxos_aarch64_tab_a8_identity_matches("SM-X205", "gta8wifi"),
                 "SM-X205 is not silently treated as SM-X200");
    ok &= expect(!gxos_aarch64_tab_a8_identity_matches("SM-X200", "other"),
                 "other codename is rejected");
    if (!ok) return 1;
    std::puts("AARCH64 Tab A8 platform map host controls: PASS");
    return 0;
}
