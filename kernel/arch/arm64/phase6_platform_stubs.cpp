// Phase-6 platform boundary.
//
// The common desktop renderer has optional hooks for input, networking, and
// audio status widgets.  AArch64-6 intentionally brings up display output
// before those device services, so these bounded no-device providers keep the
// production renderer linkable without pretending that input is available.

#include "kernel/input_manager.h"
#include "kernel/ps2keyboard.h"
#include "kernel/shell.h"
#include "kernel/pit.h"
#include "kernel/nic.h"
#include "kernel/usb_net.h"
#include "kernel/ipv4.h"
#include "kernel/pci_audio.h"
#include "kernel/usb_audio.h"
#include "kernel/app_launch_target_resolver.h"

namespace kernel {
namespace input {

#if !defined(GXOS_AARCH64_PHASE7)
void poll() {}
int32_t mouse_x() { return 0; }
int32_t mouse_y() { return 0; }
uint8_t mouse_buttons() { return ButtonNone; }
bool mouse_dirty() { return false; }
void mouse_clear_dirty() {}
#endif

} // namespace input

namespace ps2keyboard {

bool has_key() { return false; }
uint32_t get_key() { return 0; }
bool is_ctrl_down() { return false; }
bool is_shift_down() { return false; }
bool is_alt_down() { return false; }
bool is_f4_down() { return false; }
bool was_alt_f4_shortcut_candidate() { return false; }
bool last_key_alt_left_down() { return false; }
bool last_key_alt_right_down() { return false; }

} // namespace ps2keyboard

namespace shell {

bool is_open() { return false; }
void open() {}
void close() {}
void toggle() {}
void toggle_fullscreen() {}
void process_key(uint32_t) {}
ShellState get_state() { return ShellState::Closed; }
void draw(uint32_t, uint32_t, uint32_t, uint32_t) {}

} // namespace shell

namespace pit {

uint64_t ticks() { return 0; }

} // namespace pit

namespace nic {

bool is_active() { return false; }
const NICDevice* get_device() { return nullptr; }
LinkState get_link_state() { return NIC_LINK_DOWN; }
const NetStats* get_stats() { return nullptr; }

} // namespace nic

namespace usb_net {

uint8_t device_count() { return 0; }
const NetDevice* get_device(uint8_t) { return nullptr; }

} // namespace usb_net

namespace ipv4 {

bool is_configured() { return false; }

} // namespace ipv4

namespace pci_audio {

uint8_t controller_count() { return 0; }
bool get_mute(uint8_t) { return false; }
bool set_mute(uint8_t, bool) { return false; }

} // namespace pci_audio

namespace usb_audio {

uint8_t device_count() { return 0; }
bool get_mute(uint8_t) { return false; }
usb::TransferStatus set_mute(uint8_t, bool) { return usb::XFER_NOT_SUPPORTED; }

} // namespace usb_audio

namespace appmodel {

gxos::apps::LaunchTarget resolveLaunchTarget(const char*) { return gxos::apps::LaunchTarget{}; }

} // namespace appmodel
} // namespace kernel

namespace gxos {
namespace apps {

bool TypedDispatchRuntimeEnabled() { return false; }

} // namespace apps
} // namespace gxos
