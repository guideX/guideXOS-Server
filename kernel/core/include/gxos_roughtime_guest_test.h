#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Deliberately invoked from the guideXOS diagnostic shell command. It never
// runs automatically during boot and never changes the operating-system clock.
int gxos_roughtime_guest_selftest();

#ifdef __cplusplus
}
#endif
