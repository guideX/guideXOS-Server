#include <guidexos/abi.h>

#include <cstdint>
#include <iostream>

static bool valid_handle(uint64_t handle, uint16_t generation, uint8_t kind, uint32_t id)
{
    const uint64_t magic = UINT64_C(0x4758000000000000);
    const uint64_t expected = magic | (static_cast<uint64_t>(generation) << 32) |
        (static_cast<uint64_t>(kind) << 28) | id;
    return handle != 0 && handle == expected;
}

static bool valid_string(const char* value)
{
    if (!value) return false;
    for (uint32_t i = 0; i < GX_ABI_MAX_STRING_BYTES; ++i) if (value[i] == '\0') return true;
    return false;
}

static bool valid_geometry(int32_t x, int32_t y, int32_t width, int32_t height)
{
    return x >= 0 && y >= 0 && width > 0 && height > 0 &&
        width <= 640 && height <= 640 && x <= 640 - width && y <= 576 - height;
}

static bool gui_abi_compatible(uint32_t size, uint32_t version)
{
    return size >= GX_GUI_HOST_CALLS_SIZE && version == GX_GUI_ABI_VERSION;
}

int main()
{
    const uint64_t window = UINT64_C(0x4758000110000007);
    const uint64_t widget = UINT64_C(0x4758000120000002);
    const uint64_t otherOwnerWindow = UINT64_C(0x4758000210000007);
    if (!valid_handle(window, 1, 1, 7) || valid_handle(widget, 1, 1, 2) ||
        valid_handle(UINT64_C(0x4758000200000007), 1, 1, 7) ||
        valid_handle(UINT64_C(0x4758000110000007), 2, 1, 7) ||
        valid_handle(otherOwnerWindow, 1, 1, 7)) return 1;
    if (valid_string(nullptr) || !valid_string("arm64")) return 1;
    char oversized[GX_ABI_MAX_STRING_BYTES + 1];
    for (uint32_t i = 0; i < GX_ABI_MAX_STRING_BYTES + 1; ++i) oversized[i] = 'x';
    if (valid_string(oversized)) return 1;
    if (!valid_geometry(20, 20, 320, 32) || valid_geometry(-1, 0, 20, 20) ||
        valid_geometry(0, 0, 641, 20) || valid_geometry(640, 0, 1, 1)) return 1;
    bool destroyed = false;
    const bool firstDestroyAccepted = !destroyed;
    destroyed = true;
    const bool duplicateDestroyRejected = destroyed;
    const bool eventAfterExitRejected = destroyed;
    const bool crossOwnerRejected = window != otherOwnerWindow;
    const bool invalidControlRejected = !valid_handle(window, 1, 2, 7);
    const bool versionMismatchRejected = !gui_abi_compatible(GX_GUI_HOST_CALLS_SIZE, GX_GUI_ABI_VERSION + 1u);
    if (!firstDestroyAccepted || !duplicateDestroyRejected || !eventAfterExitRejected ||
        !crossOwnerRejected || !invalidControlRejected || !versionMismatchRejected) return 1;
    std::cout << "AARCH64_PHASE8_HOST_CONTROLS_PASS (handles, ownership, strings, geometry, duplicate destroy, ABI mismatch)\n";
    return 0;
}
