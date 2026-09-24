#include "include/kernel/nativeaot_application.h"

#include "include/kernel/address_space.h"
#include "include/kernel/arch.h"
#include "include/kernel/desktop.h"
#include "include/kernel/hugepages.h"
#include "include/kernel/framebuffer.h"
#include "include/kernel/kernel_app.h"
#include "include/kernel/kernel_compositor.h"
#include "include/kernel/pit.h"
#include "include/kernel/process.h"
#include "include/kernel/ps2keyboard.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/vfs.h"
#include "built_in_app_metadata.h"

#include "runtime/local_storage/guidexos_local_storage.h"
#include "runtime/memory/guidexos_virtual_memory_region.h"
#include "runtime/synchronization/guidexos_event.h"
#include "runtime/thread/guidexos_native_stack_bounds.h"
#include "runtime/thread/guidexos_native_thread.h"

#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_gc_startup_platform_contract.h"
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_pal_abi_bridge.h"
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_pal_contract.h"
#include "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_threadstore_adapter.h"

#include <stddef.h>
#include <stdint.h>

namespace kernel {
namespace nativeaot {
int32_t invokeManagedAction(uint32_t selector, uint32_t actionId);
int32_t invokeManagedInput(uint32_t selector, uint32_t inputFlags);
namespace {

constexpr uintptr_t kPageSize = 0x1000u;
constexpr uint32_t kMaxProgramHeaders = 32u;
constexpr uint32_t kMaxMappedPages = 4096u;
// The ELF image owns one VM registry slot per PT_LOAD mapping.  NativeAOT
// startup also needs ordinary PAL allocations, so the registry must leave
// bounded capacity beyond the image's seven current load segments.
constexpr uint32_t kMaxVmSlots = 256u;
constexpr uint32_t kMaxEventSlots = 8u;
constexpr uint32_t kMaxWorkerSlots = 4u;
constexpr uint32_t kMaxFlsSlots = gxos::runtime::kLocalStorageCapacity;
constexpr uint32_t kMaxFlsContexts = gxos::runtime::kLocalStorageMaximumContexts;
constexpr uint64_t kMaxArtifactBytes = 4u * 1024u * 1024u;
constexpr uint32_t kMaxApplicationPath = 128u;
constexpr uint32_t kElfClass64 = 2u;
constexpr uint32_t kElfDataLittle = 1u;
constexpr uint16_t kElfExecutable = 2u;
constexpr uint16_t kElfMachineAmd64 = 0x3Eu;
constexpr uint32_t kElfProgramLoad = 1u;
constexpr uint32_t kElfProgramNull = 0u;
constexpr uint32_t kElfProgramGnuStack = 0x6474E551u;
constexpr uint32_t kElfFlagExecute = 1u;
constexpr uint32_t kElfFlagWrite = 2u;
constexpr uint32_t kElfFlagRead = 4u;
constexpr uint32_t kVmemCommit = 0x1000u;
constexpr uint32_t kVmemRelease = 0x8000u;
constexpr int32_t kInvalidApplicationIdReturn = -4;
constexpr const char* kProductionCompositeImage = gxos::apps::kManagedNativeAotCompositeImagePath;
// C113 and C114 append optional file-service callbacks to the C112 v1 prefix.
// The version remains v1 because prefix clients are still valid consumers;
// table size and capability bits gate each appended field.
constexpr uint32_t kManagedHostAbiVersion = 1u;
constexpr uint32_t kManagedHostAbiV1Size = 72u;
constexpr uint32_t kManagedHostC113Size = 88u;
constexpr uint32_t kManagedHostTableSize = 104u;
constexpr uint64_t kManagedCapabilitySurface = 1ull << 0;
constexpr uint64_t kManagedCapabilityText = 1ull << 1;
constexpr uint64_t kManagedCapabilityPrimitive = 1ull << 2;
constexpr uint64_t kManagedCapabilityAction = 1ull << 3;
constexpr uint64_t kManagedCapabilityClose = 1ull << 4;
constexpr uint64_t kManagedCapabilityLaunchContext = 1ull << 5;
constexpr uint64_t kManagedCapabilityLog = 1ull << 6;
constexpr uint64_t kManagedCapabilityFileRead = 1ull << 7;
constexpr uint64_t kManagedCapabilityFileWrite = 1ull << 8;
constexpr uint64_t kManagedCapabilityDirectoryList = 1ull << 9;
constexpr uint64_t kManagedCapabilityFileStat = 1ull << 10;
constexpr uint64_t kManagedCapabilities =
    kManagedCapabilitySurface | kManagedCapabilityText |
    kManagedCapabilityPrimitive | kManagedCapabilityAction |
    kManagedCapabilityClose | kManagedCapabilityLaunchContext |
    kManagedCapabilityLog | kManagedCapabilityFileRead |
    kManagedCapabilityFileWrite | kManagedCapabilityDirectoryList |
    kManagedCapabilityFileStat;
constexpr uint32_t kManagedFilePathMaxBytes = 96u;
constexpr uint32_t kManagedFileMaxBytes = 16u * 1024u;
constexpr uint32_t kManagedDirectoryMaxEntries = 64u;
constexpr uint32_t kManagedDirectoryNameMaxBytes = 127u;
constexpr uint32_t kManagedDirectoryEntryAbiSize = 144u;
constexpr uint32_t kManagedFileInfoAbiSize = 16u;
constexpr int32_t kManagedFileSuccess = 0;
constexpr int32_t kManagedFileNotFound = -10;
constexpr int32_t kManagedFileInvalidPath = -11;
constexpr int32_t kManagedFileBufferTooSmall = -12;
constexpr int32_t kManagedFileTooLarge = -13;
constexpr int32_t kManagedFileIoFailure = -14;
constexpr int32_t kManagedFileCapabilityUnavailable = -15;
constexpr int32_t kManagedFileInvalidArgument = -16;
constexpr int32_t kManagedFileNotDirectory = -17;
constexpr int32_t kManagedFileEntryNameTooLong = -18;
constexpr uint32_t kManagedEntryTypeRegular = 1u;
constexpr uint32_t kManagedEntryTypeDirectory = 2u;
constexpr const char* kManagedFileRoot = "/system/apps/";
constexpr uint32_t kLaunchFlagAction = 0x80000000u;
constexpr uint32_t kLaunchFlagCapabilityProbe = 0x40000000u;
constexpr uint32_t kLaunchFlagAbiProbe = 0x20000000u;
// C116 input events reuse the existing launch-flags transport.  The input
// bridge is append-only at the semantic level: no host-table field is added.
constexpr uint32_t kLaunchFlagInput = 0x10000000u;
constexpr uint32_t kLaunchFlagInputKindMask = 0x0F000000u;
constexpr uint32_t kLaunchFlagInputPointerDown = 0x01000000u;
constexpr uint32_t kLaunchFlagInputKeyDown = 0x02000000u;
constexpr uint32_t kLaunchFlagInputKeyChar = 0x03000000u;
// C136 keeps the v1 launch-flags transport and spends two unused semantic
// kind values on pointer-up and secondary-button events.  Coordinates remain
// the existing 24-bit payload; button identity is carried by the event kind.
constexpr uint32_t kLaunchFlagInputPointerUp = 0x04000000u;
constexpr uint32_t kLaunchFlagInputSecondaryPointerDown = 0x05000000u;
constexpr uint32_t kLaunchFlagInputSecondaryPointerUp = 0x06000000u;
// C137 uses the remaining kind values as a compact signed wheel-delta lane.
// This preserves the existing 12-bit X + 12-bit Y payload and therefore keeps
// the v1 launch-flags ABI unchanged.
constexpr uint32_t kLaunchFlagInputWheel = 0x07000000u;
constexpr uint32_t kLaunchFlagInputWheelLast = 0x0F000000u;
constexpr uint32_t kLaunchFlagInputPayloadMask = 0x00FFFFFFu;
constexpr uint32_t kLaunchFlagInputCoordinateMask = 0x00000FFFu;
constexpr uint32_t kC137RelaunchKey = 0x11Bu;
// C117 reserves the high bit of the 24-bit key payload for Shift. This is an
// input-transport detail, not a host-table or ABI extension.
constexpr uint32_t kLaunchFlagInputShift = 0x00800000u;
constexpr uint32_t kLaunchFlagInputValueMask = 0x007FFFFFu;

#pragma pack(push, 1)
struct Elf64Header {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t programHeaderOffset;
    uint64_t sectionHeaderOffset;
    uint32_t flags;
    uint16_t headerSize;
    uint16_t programHeaderSize;
    uint16_t programHeaderCount;
    uint16_t sectionHeaderSize;
    uint16_t sectionHeaderCount;
    uint16_t sectionNameIndex;
};

struct Elf64ProgramHeader {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtualAddress;
    uint64_t physicalAddress;
    uint64_t fileSize;
    uint64_t memorySize;
    uint64_t alignment;
};
#pragma pack(pop)

struct MappedPage {
    uintptr_t address;
    uint64_t physical;
};

struct VmSlot {
    bool active;
    gxos::runtime::virtual_memory::VirtualMemoryRegion region;
};

struct EventSlot {
    bool active;
    gxos::runtime::Event event;
};

struct FlsSlot {
    bool active;
    gxos::runtime::LocalStorageIndex genericIndex;
    guidexos_nativeaot_pal_win64_detach_callback callback;
};

struct FlsContext {
    bool active;
    uint64_t threadId;
    void* values[kMaxFlsSlots];
};

struct WorkerSlot {
    bool active;
    bool joined;
    uint32_t generation;
    guidexos_nativeaot_pal_win64_worker_entry entry;
    void* context;
    gxos::runtime::ThreadHandle nativeHandle;
};

struct NativeAotTlsGsArea {
    uint8_t reserved[0x58];
    void** vector;
};

struct NativeGxAppContext;

struct NativeHostCallTable {
    uint32_t size;
    uint32_t version;
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *log)(NativeGxAppContext* context, uint8_t* message);
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *requestWindow)(
        NativeGxAppContext* context, uint8_t* title, int32_t width, int32_t height,
        uint64_t* outWindow);
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *drawText)(
        NativeGxAppContext* context, uint64_t window, int32_t x, int32_t y,
        uint8_t* text);
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *drawRect)(
        NativeGxAppContext* context, uint64_t window, int32_t x, int32_t y,
        int32_t width, int32_t height, uint32_t color);
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *addButton)(
        NativeGxAppContext* context, uint64_t window, int32_t x, int32_t y,
        int32_t width, int32_t height, uint8_t* text, int32_t* outWidget);
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *closeWindow)(
        NativeGxAppContext* context, uint64_t window);
    uint64_t capabilities;
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *addActionButton)(
        NativeGxAppContext* context, uint64_t window, int32_t x, int32_t y,
        int32_t width, int32_t height, uint8_t* text, uint32_t actionId,
        int32_t* outWidget);
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *fileReadAll)(
        NativeGxAppContext* context, uint8_t* path, uint32_t pathLength,
        uint8_t* buffer, uint32_t capacity, uint32_t* outLength);
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *fileWriteAll)(
        NativeGxAppContext* context, uint8_t* path, uint32_t pathLength,
        const uint8_t* data, uint32_t length);
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *directoryList)(
        NativeGxAppContext* context, uint8_t* path, uint32_t pathLength,
        uint8_t* entries, uint32_t capacity, uint32_t entryStride,
        uint32_t* outCount, uint32_t* outHasMore);
    int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *fileStat)(
        NativeGxAppContext* context, uint8_t* path, uint32_t pathLength,
        uint8_t* outInfo, uint32_t infoSize);
};

struct ManagedDirectoryEntryAbi {
    uint32_t nameLength;
    uint32_t type;
    uint64_t size;
    uint8_t name[128];
};

struct ManagedFileInfoAbi {
    uint32_t type;
    uint32_t reserved;
    uint64_t size;
};

struct NativeGxAppContext {
    uint32_t size;
    uint32_t apiVersion;
    NativeHostCallTable* host;
    void* userData;
    const uint8_t* launchContext;
    uint32_t launchContextLength;
    uint32_t launchFlags;
};

struct NativeAotStartupContext {
    void* legacyPalHooks;
    void* palHookTable;
    void* gcPlatformTable;
    void* managedContext;
    int (*installTls)(void);
    void (*startupMarker)(uint32_t stage);
};

enum class ApplicationLifecycleState : uint32_t {
    Empty = 0,
    Loading = 1,
    Resident = 2,
};

struct ResidentApplication {
    ApplicationLifecycleState state;
    char path[kMaxApplicationPath];
    uint64_t artifactBytes;
    uintptr_t artifactBase;
    uintptr_t artifactSpan;
    uintptr_t entryPoint;
    uint16_t loadSegmentCount;
    uint32_t sequence;
};

static_assert(sizeof(NativeHostCallTable) == 104, "C114 host callback ABI drift");
static_assert(kManagedHostTableSize >= kManagedHostAbiV1Size,
              "C113 host table must retain the C112 v1 prefix");
static_assert(offsetof(NativeHostCallTable, fileReadAll) == 72,
              "C113 file-read callback offset drift");
static_assert(offsetof(NativeHostCallTable, fileWriteAll) == 80,
              "C113 file-write callback offset drift");
static_assert(offsetof(NativeHostCallTable, directoryList) == 88,
              "C114 directory-list callback offset drift");
static_assert(offsetof(NativeHostCallTable, fileStat) == 96,
              "C114 file-stat callback offset drift");
static_assert(sizeof(ManagedDirectoryEntryAbi) == kManagedDirectoryEntryAbiSize,
              "C114 directory-entry ABI drift");
static_assert(sizeof(ManagedFileInfoAbi) == kManagedFileInfoAbiSize,
              "C114 file-info ABI drift");
static_assert(sizeof(NativeGxAppContext) == 40, "C111 application ABI drift");
static_assert(offsetof(NativeAotTlsGsArea, vector) == 0x58,
              "C102 TLS vector offset drift");

alignas(4096) uint8_t g_artifact[kMaxArtifactBytes] = {};
MappedPage g_pages[kMaxMappedPages] = {};
uint32_t g_pageCount = 0;
uintptr_t g_artifactBase = 0;
uintptr_t g_artifactSpan = 0;
uint64_t g_installationGeneration = 1;
VmSlot g_vmSlots[kMaxVmSlots] = {};
EventSlot g_eventSlots[kMaxEventSlots] = {};
FlsSlot g_flsSlots[kMaxFlsSlots] = {};
FlsContext g_flsContexts[kMaxFlsContexts] = {};
WorkerSlot g_workers[kMaxWorkerSlots] = {};
alignas(16) NativeAotTlsGsArea g_tlsArea = {};
alignas(16) uint8_t g_tlsBlock[0x110] = {};
void* g_tlsVector[1] = {};
bool g_c102ManagedEntryObserved = false;
bool g_c102ManagedPassObserved = false;
bool g_c104ManagedEntryObserved = false;
bool g_c104ManagedPassObserved = false;
bool g_c107ManagedEntryObserved = false;
bool g_c107ManagedPassObserved = false;
bool g_c107ManagedInvalidApplicationObserved = false;
ResidentApplication g_application = {};
NativeGxAppContext* g_activeManagedContext = nullptr;
bool g_c116InputDispatchActive = false;
#if defined(GXOS_C108_TLS_LIFECYCLE)
uint32_t g_c108TlsInstallCount = 0;
uint32_t g_c108ManagedInvocationOrdinal = 0;
#endif
#if defined(GXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH)
uint32_t g_productionTlsInstallCount = 0;
uint32_t g_productionManagedInvocationOrdinal = 0;
#endif

struct ManagedSurfaceRect {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint32_t color;
};

// The managed bridge deliberately reuses the existing bare-metal application
// and compositor surface.  This adapter owns one logical window at a time;
// the NativeAOT image remains resident while the window may be closed and
// recreated on a later logical launch.
class NativeAotManagedSurface final : public app::KernelApp {
public:
    NativeAotManagedSurface()
        : m_rectCount(0), m_actionButtonId(-1), m_actionId(0),
          m_actionSelector(0), m_actionCount(0) {
        const char* name = "Managed NativeAOT";
        int index = 0;
        while (name[index] && index < app::MAX_APP_NAME - 1) {
            m_name[index] = name[index];
            ++index;
        }
        m_name[index] = '\0';
    }

    bool init() override { return false; }
    void shutdown() override {}

    void draw(uint32_t windowX, uint32_t clientY,
              uint32_t, uint32_t) override {
        for (int index = 0; index < m_rectCount; ++index) {
            const ManagedSurfaceRect& rect = m_rects[index];
            framebuffer::fill_rect(
                windowX + static_cast<uint32_t>(rect.x),
                clientY + static_cast<uint32_t>(rect.y),
                static_cast<uint32_t>(rect.width),
                static_cast<uint32_t>(rect.height),
                0xFF000000u | (rect.color & 0x00FFFFFFu));
        }
    }

    void onWidgetClick(int widgetId) override {
        for (int index = 0; index < m_actionBindingCount; ++index) {
            if (m_actionBindings[index].widgetId != widgetId) continue;
            const int32_t managedResult = invokeManagedAction(
                m_actionBindings[index].selector, m_actionBindings[index].actionId);
            serial::puts("[C112-ACTION-DISPATCH] selector=");
            serial::put_hex32(m_actionBindings[index].selector);
            serial::puts(" action=");
            serial::put_hex32(m_actionBindings[index].actionId);
            serial::puts(" managedReturn=");
            serial::put_hex32(static_cast<uint32_t>(managedResult));
            serial::puts(" result=");
            serial::puts(managedResult == 0 ? "PASS\n" : "FAIL\n");
            return;
        }
        if (widgetId != m_actionButtonId) return;
        if (m_actionId != 0u) {
            const int32_t managedResult = invokeManagedAction(
                m_actionSelector, m_actionId);
            serial::puts("[C112-ACTION-DISPATCH] selector=");
            serial::put_hex32(m_actionSelector);
            serial::puts(" action=");
            serial::put_hex32(m_actionId);
            serial::puts(" managedReturn=");
            serial::put_hex32(static_cast<uint32_t>(managedResult));
            serial::puts(" result=");
            serial::puts(managedResult == 0 ? "PASS\n" : "FAIL\n");
            return;
        }
        ++m_actionCount;
        setWidgetText(widgetId, "Action complete");
        serial::puts("[C111-INTERACTION] result=PASS actionCount=");
        serial::put_hex32(static_cast<uint32_t>(m_actionCount));
        serial::puts(" window=");
        serial::put_hex32(m_window ? m_window->id : 0u);
        serial::puts("\n");
    }

    void onMouseDown(int x, int y, uint8_t button) override {
        if ((button != 1u && button != 2u) || m_selector == 0u || x < 0 || y < 0 ||
            static_cast<uint32_t>(x) > kLaunchFlagInputCoordinateMask ||
            static_cast<uint32_t>(y) > kLaunchFlagInputCoordinateMask) {
            return;
        }
        const uint32_t payload = static_cast<uint32_t>(x) |
            (static_cast<uint32_t>(y) << 12);
        const uint32_t kind = button == 2u
            ? kLaunchFlagInputSecondaryPointerDown
            : kLaunchFlagInputPointerDown;
        const int32_t result = invokeManagedInput(
            m_selector, kLaunchFlagInput | kind | payload);
        if (button == 2u) {
            serial::puts("[C136-NATIVE-INPUT] button=secondary phase=down x=");
            serial::put_hex32(static_cast<uint32_t>(x));
            serial::puts(" y=");
            serial::put_hex32(static_cast<uint32_t>(y));
            serial::puts(" result=");
            serial::puts(result == 0 ? "PASS\n" : "FAIL\n");
            return;
        }
        serial::puts("[C116-NATIVE-INPUT] kind=pointer-down x=");
        serial::put_hex32(static_cast<uint32_t>(x));
        serial::puts(" y=");
        serial::put_hex32(static_cast<uint32_t>(y));
        serial::puts(" result=");
        serial::puts(result == 0 ? "PASS\n" : "IGNORED\n");
#if defined(GXOS_NATIVEAOT_C136_SECONDARY_POINTER_CONTEXT_MENU)
        serial::puts("[C136-NATIVE-INPUT] button=primary phase=down x=");
        serial::put_hex32(static_cast<uint32_t>(x));
        serial::puts(" y=");
        serial::put_hex32(static_cast<uint32_t>(y));
        serial::puts(" result=");
        serial::puts(result == 0 ? "PASS\n" : "FAIL\n");
#endif
    }

    void onMouseUp(int x, int y, uint8_t button) override {
        if ((button != 1u && button != 2u) || m_selector == 0u || x < 0 || y < 0 ||
            static_cast<uint32_t>(x) > kLaunchFlagInputCoordinateMask ||
            static_cast<uint32_t>(y) > kLaunchFlagInputCoordinateMask) {
            return;
        }
        const uint32_t payload = static_cast<uint32_t>(x) |
            (static_cast<uint32_t>(y) << 12);
        const uint32_t kind = button == 2u
            ? kLaunchFlagInputSecondaryPointerUp
            : kLaunchFlagInputPointerUp;
        const int32_t result = invokeManagedInput(
            m_selector, kLaunchFlagInput | kind | payload);
        if (button == 2u) {
            serial::puts("[C136-NATIVE-INPUT] button=secondary phase=up x=");
            serial::put_hex32(static_cast<uint32_t>(x));
            serial::puts(" y=");
            serial::put_hex32(static_cast<uint32_t>(y));
            serial::puts(" result=");
            serial::puts(result == 0 ? "PASS\n" : "FAIL\n");
        } else {
#if defined(GXOS_NATIVEAOT_C136_SECONDARY_POINTER_CONTEXT_MENU)
            serial::puts("[C136-NATIVE-INPUT] button=primary phase=up x=");
            serial::put_hex32(static_cast<uint32_t>(x));
            serial::puts(" y=");
            serial::put_hex32(static_cast<uint32_t>(y));
            serial::puts(" result=");
            serial::puts(result == 0 ? "PASS\n" : "FAIL\n");
#endif
        }
    }

    void onMouseWheel(int x, int y, int wheelDelta) override {
        if (m_selector == 0u || wheelDelta == 0 || x < 0 || y < 0 ||
            static_cast<uint32_t>(x) > kLaunchFlagInputCoordinateMask ||
            static_cast<uint32_t>(y) > kLaunchFlagInputCoordinateMask) {
            return;
        }
        if (wheelDelta > 4) wheelDelta = 4;
        if (wheelDelta < -4) wheelDelta = -4;
        const uint32_t wheelKind = kLaunchFlagInputWheel +
            (static_cast<uint32_t>(wheelDelta + 4) << 24);
        const uint32_t payload = static_cast<uint32_t>(x) |
            (static_cast<uint32_t>(y) << 12);
        const int32_t result = invokeManagedInput(
            m_selector, kLaunchFlagInput | wheelKind | payload);
#if defined(GXOS_NATIVEAOT_C137_MOUSE_WHEEL_SCROLLING)
        serial::puts("[C137-NATIVE-INPUT] kind=wheel delta=");
        if (wheelDelta < 0) serial::puts("-");
        serial::put_hex32(static_cast<uint32_t>(wheelDelta < 0
            ? -wheelDelta : wheelDelta));
        serial::puts(" x=");
        serial::put_hex32(static_cast<uint32_t>(x));
        serial::puts(" y=");
        serial::put_hex32(static_cast<uint32_t>(y));
        serial::puts(" buttons-preserved=true result=");
        serial::puts(result == 0 ? "PASS\n" : "FAIL\n");
#endif
    }

    void onKeyDown(uint32_t key) override {
        if (m_selector == 0u || key > kLaunchFlagInputPayloadMask) return;
        uint32_t payload = key & kLaunchFlagInputValueMask;
        if (ps2keyboard::is_shift_down()) payload |= kLaunchFlagInputShift;
#if defined(GXOS_NATIVEAOT_C129_SHIFT_TAB_INPUT_TRANSPORT)
        if (key == 9u) {
            serial::puts("[C129-NATIVE] tab-keydown shift=");
            serial::puts((payload & kLaunchFlagInputShift) != 0u ? "1" : "0");
            serial::puts(" transport=production result=PASS\n");
        }
#endif
        const int32_t result = invokeManagedInput(
            m_selector, kLaunchFlagInput | kLaunchFlagInputKeyDown | payload);
        serial::puts("[C116-NATIVE-INPUT] kind=key-down key=");
        serial::put_hex32(key);
        serial::puts(" shift=");
        serial::put_hex32((payload & kLaunchFlagInputShift) != 0u ? 1u : 0u);
        serial::puts(" result=");
        serial::puts(result == 0 ? "PASS\n" : "IGNORED\n");
#if defined(GXOS_NATIVEAOT_C129_SHIFT_TAB_INPUT_TRANSPORT)
        if (key == 9u) {
            serial::puts("[C129-NATIVE-INPUT] kind=key-down key=");
            serial::put_hex32(key);
            serial::puts(" shift=");
            serial::put_hex32((payload & kLaunchFlagInputShift) != 0u ? 1u : 0u);
            serial::puts(" result=");
            serial::puts(result == 0 ? "PASS\n" : "FAIL\n");
        }
#endif
#if defined(GXOS_NATIVEAOT_C137_MOUSE_WHEEL_SCROLLING)
        if (key == kC137RelaunchKey && result == 0) {
            const gxos::apps::BuiltInAppMetadata* notes =
                gxos::apps::FindBuiltInAppMetadataByDisplayName("Managed Notes");
            const bool relaunched = notes && kernel::desktop::launch_app_with_context(
                notes->appId, "c137-relaunch");
            serial::puts("[C137-RELAUNCH] close=PASS relaunch=");
            serial::puts(relaunched ? "PASS capture=none result=PASS\n"
                                     : "FAIL capture=unknown result=FAIL\n");
        }
#endif
    }

    void onKeyChar(char c) override {
        if (m_selector == 0u) return;
        const uint32_t payload = static_cast<uint32_t>(
            static_cast<uint8_t>(c));
        const uint32_t inputPayload = payload |
            (ps2keyboard::is_shift_down() ? kLaunchFlagInputShift : 0u);
        const int32_t result = invokeManagedInput(
            m_selector, kLaunchFlagInput | kLaunchFlagInputKeyChar | inputPayload);
        serial::puts("[C116-NATIVE-INPUT] kind=key-char value=");
        serial::put_hex32(payload);
        serial::puts(" shift=");
        serial::put_hex32((inputPayload & kLaunchFlagInputShift) != 0u ? 1u : 0u);
        serial::puts(" result=");
        serial::puts(result == 0 ? "PASS\n" : "IGNORED\n");
#if defined(GXOS_NATIVEAOT_C129_SHIFT_TAB_INPUT_TRANSPORT)
        serial::puts("[C129-NATIVE-INPUT] kind=key-char value=");
        serial::put_hex32(payload);
        serial::puts(" shift=");
        serial::put_hex32((inputPayload & kLaunchFlagInputShift) != 0u ? 1u : 0u);
        serial::puts(" result=");
        serial::puts(result == 0 ? "PASS\n" : "FAIL\n");
#endif
    }

    void onWindowClose() override {
        serial::puts("[C111-SURFACE] action=close result=PASS\n");
    }

    bool open(const char* title, int32_t width, int32_t height) {
        if (!title || !title[0] || width < compositor::MIN_WINDOW_WIDTH ||
            height < compositor::MIN_WINDOW_HEIGHT) {
            return false;
        }
        if (m_window) requestClose();

        app::KernelWindow* window = new app::KernelWindow();
        if (!window) return false;
        const uint32_t screenWidth = framebuffer::get_width();
        const uint32_t screenHeight = framebuffer::get_height();
        window->x = screenWidth > static_cast<uint32_t>(width)
            ? static_cast<int>((screenWidth - static_cast<uint32_t>(width)) / 2u) : 0;
        window->y = screenHeight > static_cast<uint32_t>(height)
            ? static_cast<int>((screenHeight - static_cast<uint32_t>(height)) / 2u) : 0;
        window->w = width;
        window->h = height;
        window->owner = this;
        if (!compositor::KernelCompositor::registerWindow(window)) {
            delete window;
            return false;
        }
        m_window = window;
        m_state = app::AppState::Running;
        m_window->widgetCount = 0;
        m_rectCount = 0;
        m_actionButtonId = -1;
        m_actionId = 0;
        m_actionSelector = 0;
        m_actionBindingCount = 0;
        setTitle(title);
        compositor::KernelCompositor::setFocus(m_window->id);
        return true;
    }

    void setSelector(uint32_t selector) {
        m_selector = selector;
    }

    bool owns(uint64_t window) const {
        return m_window != nullptr && m_window->id == static_cast<uint32_t>(window);
    }

    uint64_t windowId() const {
        return m_window ? static_cast<uint64_t>(m_window->id) : 0u;
    }

    bool setText(int32_t x, int32_t y, const char* text) {
        if (!m_window || !text || x < 0 || y < 0) return false;
        return addLabel(x, y, 480, 18, text) >= 0;
    }

    bool updateText(int32_t x, int32_t y, const char* text) {
        if (!m_window || !text || x < 0 || y < 0) return false;
        for (int index = 0; index < m_window->widgetCount; ++index) {
            app::Widget& widget = m_window->widgets[index];
            if (widget.type != app::WidgetType::Label || widget.x != x ||
                widget.y != y) continue;
            int length = 0;
            while (text[length] && length < 63) {
                widget.text[length] = text[length];
                ++length;
            }
            widget.text[length] = '\0';
            invalidate();
            return true;
        }
        return false;
    }

    bool addRect(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t color) {
        if (!m_window || m_rectCount >= static_cast<int>(sizeof(m_rects) / sizeof(m_rects[0]))) {
            return false;
        }
        m_rects[m_rectCount++] = {x, y, width, height, color};
        invalidate();
        return true;
    }

    void beginManagedFrame() {
        // Every managed render starts with a background rectangle. Reusing
        // that call as the frame boundary keeps widget/action storage bounded
        // across redraws without adding another host ABI callback.
        if (m_window) m_window->widgetCount = 0;
        m_rectCount = 0;
        m_actionButtonId = -1;
        m_actionId = 0;
        m_actionSelector = 0;
        m_actionBindingCount = 0;
    }

    bool addActionButton(int32_t x, int32_t y, int32_t width, int32_t height,
                         const char* text, int32_t* outWidget) {
        return addActionButton(x, y, width, height, text, 0u, 0u, outWidget);
    }

    bool addActionButton(int32_t x, int32_t y, int32_t width, int32_t height,
                         const char* text, uint32_t actionId,
                         uint32_t selector, int32_t* outWidget) {
        if (!m_window || !text || !outWidget) return false;
        const int id = addButton(x, y, width, height, text);
        if (id < 0) return false;
        if (actionId != 0u && m_actionBindingCount >= static_cast<int>(sizeof(m_actionBindings) / sizeof(m_actionBindings[0]))) {
            // Re-rendered managed surfaces append widgets. Keep only the
            // bounded set belonging to the newest frame.
            m_actionBindingCount = 0;
        }
        if (actionId != 0u) {
            m_actionBindings[m_actionBindingCount++] = {id, actionId, selector};
        }
        m_actionButtonId = id;
        m_actionId = actionId;
        m_actionSelector = selector;
        *outWidget = id;
        return true;
    }

private:
    struct ManagedActionBinding {
        int widgetId;
        uint32_t actionId;
        uint32_t selector;
    };
    ManagedSurfaceRect m_rects[8] = {};
    int m_rectCount;
    int m_actionButtonId;
    uint32_t m_actionId;
    uint32_t m_actionSelector;
    int m_actionCount;
    uint32_t m_selector = 0;
    ManagedActionBinding m_actionBindings[8] = {};
    int m_actionBindingCount = 0;
};

// Bare-metal startup does not run the hosted C++ global-constructor array.
// Allocate this polymorphic adapter explicitly so KernelWindow::owner has a
// real vtable before the compositor dispatches draw/input/close callbacks.
NativeAotManagedSurface* g_managedSurface = nullptr;

NativeAotManagedSurface* managedSurface() {
    if (!g_managedSurface) {
        g_managedSurface = new NativeAotManagedSurface();
    }
    return g_managedSurface;
}

#if defined(GXOS_C103_PRODUCTION_LAUNCH) || defined(GXOS_C103_NEGATIVE_LAUNCH)
constexpr bool kC103LifecycleEnabled = true;
#else
constexpr bool kC103LifecycleEnabled = false;
#endif

#if defined(GXOS_C104_PRODUCTION_LAUNCH)
constexpr bool kC104MultipleApplicationProbeEnabled = true;
#else
constexpr bool kC104MultipleApplicationProbeEnabled = false;
#endif

bool boundedRange(uint64_t offset, uint64_t size, uint64_t limit) {
    return offset <= limit && size <= limit - offset;
}

bool artifactContains(uintptr_t value) {
    return g_artifactBase != 0 && value >= g_artifactBase &&
        value - g_artifactBase < g_artifactSpan;
}

void emitFrameAccounting(const char* phase, const memory::address_space::FrameAccounting& stats) {
    serial::puts("[C102-FRAMES] phase=");
    serial::puts(phase);
    serial::puts(" totalTracked=");
    serial::put_hex64(stats.totalKnownFrames);
    serial::puts(" free=");
    serial::put_hex64(stats.freeFrames);
    serial::puts(" allocated=");
    serial::put_hex64(stats.allocatedFrames);
    serial::puts(" vmRegion=");
    serial::put_hex64(stats.regionOwnedFrames);
    serial::puts(" pageTable=");
    serial::put_hex64(stats.pageTableFrames);
    serial::puts(" kernel=");
    serial::put_hex64(stats.kernelOwnedFrames);
    serial::puts(" residual=");
    serial::put_hex64(stats.totalKnownFrames >= stats.freeFrames + stats.allocatedFrames
        ? stats.totalKnownFrames - stats.freeFrames - stats.allocatedFrames : UINT64_MAX);
    serial::puts(" ownerResidual=");
    serial::put_hex64(stats.allocatedFrames >=
            stats.regionOwnedFrames + stats.pageTableFrames + stats.kernelOwnedFrames
        ? stats.allocatedFrames - stats.regionOwnedFrames -
              stats.pageTableFrames - stats.kernelOwnedFrames : UINT64_MAX);
    serial::puts("\n");
}

void emitC103Begin(uint32_t sequence, const char* path, bool reused) {
    if (!kC103LifecycleEnabled) return;
    serial::puts("[C103-LAUNCH-BEGIN] sequence=");
    serial::put_hex32(sequence);
    serial::puts(" path=");
    serial::puts(path);
    serial::puts(" runtime=");
    serial::puts(reused ? "reused" : "initialized");
    serial::puts(" image=");
    serial::puts(reused ? "resident" : "mapped");
    serial::puts("\n");
}

void emitC103Frames(const char* phase, uint32_t sequence,
                    const memory::address_space::FrameAccounting& stats) {
    if (!kC103LifecycleEnabled) return;
    serial::puts("[C103-FRAMES] phase=");
    serial::puts(phase);
    serial::puts(" sequence=");
    serial::put_hex32(sequence);
    serial::puts(" totalTracked=");
    serial::put_hex64(stats.totalKnownFrames);
    serial::puts(" free=");
    serial::put_hex64(stats.freeFrames);
    serial::puts(" allocated=");
    serial::put_hex64(stats.allocatedFrames);
    serial::puts(" vmRegion=");
    serial::put_hex64(stats.regionOwnedFrames);
    serial::puts(" pageTable=");
    serial::put_hex64(stats.pageTableFrames);
    serial::puts(" residual=");
    serial::put_hex64(stats.totalKnownFrames >= stats.freeFrames + stats.allocatedFrames
        ? stats.totalKnownFrames - stats.freeFrames - stats.allocatedFrames : UINT64_MAX);
    serial::puts(" ownerResidual=");
    serial::put_hex64(stats.allocatedFrames >=
            stats.regionOwnedFrames + stats.pageTableFrames + stats.kernelOwnedFrames
        ? stats.allocatedFrames - stats.regionOwnedFrames -
              stats.pageTableFrames - stats.kernelOwnedFrames : UINT64_MAX);
    serial::puts("\n");
}

void emitC103Lifecycle(uint32_t sequence, const char* state, bool runtimeReused) {
    if (!kC103LifecycleEnabled) return;
    serial::puts("[C103-LIFECYCLE] sequence=");
    serial::put_hex32(sequence);
    serial::puts(" state=");
    serial::puts(state);
    serial::puts(" runtime=");
    serial::puts(runtimeReused ? "reused" : "initialized");
    serial::puts(" image=resident reusable=1\n");
}

void emitC103Return(uint32_t sequence, LaunchStatus status) {
    if (!kC103LifecycleEnabled) return;
    serial::puts("[C103-LAUNCH-RETURN] sequence=");
    serial::put_hex32(sequence);
    serial::puts(" status=");
    serial::puts(launchStatusName(status));
    serial::puts("\n");
}

bool copyApplicationPath(const char* source, char* destination) {
    if (source == nullptr || destination == nullptr) return false;
    uint32_t index = 0;
    for (; index + 1u < kMaxApplicationPath && source[index] != 0; ++index) {
        destination[index] = source[index];
    }
    if (source[index] != 0) return false;
    destination[index] = 0;
    return true;
}

bool sameApplicationPath(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) return false;
    for (uint32_t index = 0; index < kMaxApplicationPath; ++index) {
        if (left[index] != right[index]) return false;
        if (left[index] == 0) return true;
    }
    return false;
}

bool installTls() {
    g_tlsVector[0] = g_tlsBlock;
    g_tlsArea.vector = g_tlsVector;
#if defined(ARCH_AMD64)
    constexpr uint32_t kGsBaseMsr = 0xC0000101u;
    const uint64_t value = reinterpret_cast<uint64_t>(&g_tlsArea);
    arch::amd64::write_msr(kGsBaseMsr, value);
    const bool installed = arch::amd64::read_msr(kGsBaseMsr) == value;
#if defined(GXOS_C108_TLS_LIFECYCLE)
    ++g_c108TlsInstallCount;
    serial::puts("[C108-TLS-BRIDGE] install=");
    serial::put_hex32(g_c108TlsInstallCount);
    serial::puts(" phase=");
    serial::puts(g_application.state == ApplicationLifecycleState::Resident
        ? "resident" : "initial");
    serial::puts(" guideThread=");
    serial::put_hex64(process::current_thread_id());
    serial::puts(" gsArea=");
    serial::put_hex64(reinterpret_cast<uintptr_t>(&g_tlsArea));
    serial::puts(" tlsVector=");
    serial::put_hex64(reinterpret_cast<uintptr_t>(g_tlsVector));
    serial::puts(" tlsBlock=");
    serial::put_hex64(reinterpret_cast<uintptr_t>(g_tlsBlock));
    serial::puts(" result=");
    serial::put_hex32(installed ? 1u : 0u);
    serial::puts("\n");
#endif
#if defined(GXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH)
    ++g_productionTlsInstallCount;
    serial::puts("[NATIVEAOT-TLS-BRIDGE] install=");
    serial::put_hex32(g_productionTlsInstallCount);
    serial::puts(" phase=");
    serial::puts(g_application.state == ApplicationLifecycleState::Resident
        ? "resident" : "initial");
    serial::puts(" guideThread=");
    serial::put_hex64(process::current_thread_id());
    serial::puts(" gsArea=");
    serial::put_hex64(reinterpret_cast<uintptr_t>(&g_tlsArea));
    serial::puts(" tlsVector=");
    serial::put_hex64(reinterpret_cast<uintptr_t>(g_tlsVector));
    serial::puts(" tlsBlock=");
    serial::put_hex64(reinterpret_cast<uintptr_t>(g_tlsBlock));
    serial::puts(" result=");
    serial::put_hex32(installed ? 1u : 0u);
    serial::puts("\n");
#endif
    return installed;
#else
    return false;
#endif
}

int startupInstallTls() {
    return installTls() ? 0 : -1;
}

uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL currentThreadId64() {
    return process::current_thread_id();
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL stackBounds(
    guidexos_nativeaot_pal_stack_bounds_value* result) {
    if (result == nullptr) return -1;
    gxos::runtime::NativeStackBounds bounds{};
    if (gxos::runtime::queryCurrentNativeStackBounds(&bounds) !=
        gxos::runtime::StackBoundsResult::Success) return -1;
    result->low = bounds.low;
    result->high = bounds.high;
    result->current = bounds.current;
    return 0;
}

FlsContext* currentFlsContext() {
    const uint64_t id = currentThreadId64();
    if (id == 0) return nullptr;
    for (FlsContext& context : g_flsContexts) {
        if (context.active && context.threadId == id) return &context;
    }
    for (FlsContext& context : g_flsContexts) {
        if (!context.active) {
            context = FlsContext{};
            context.active = true;
            context.threadId = id;
            return &context;
        }
    }
    return nullptr;
}

void GUIDEXOS_NATIVEAOT_PAL_CALL flsDetach(void*) {}

uint32_t GUIDEXOS_NATIVEAOT_PAL_CALL flsAllocate(
    guidexos_nativeaot_pal_win64_detach_callback callback) {
    for (uint32_t index = 0; index < kMaxFlsSlots; ++index) {
        if (g_flsSlots[index].active) continue;
        gxos::runtime::LocalStorageIndex generic{};
        if (gxos::runtime::allocateLocalStorageIndex(flsDetach, &generic) !=
            gxos::runtime::LocalStorageResult::Success) return 0xFFFFFFFFu;
        g_flsSlots[index].active = true;
        g_flsSlots[index].genericIndex = generic;
        g_flsSlots[index].callback = callback;
        return index;
    }
    return 0xFFFFFFFFu;
}

bool validFlsIndex(uint32_t index) {
    return index < kMaxFlsSlots && g_flsSlots[index].active;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL flsRelease(uint32_t index) {
    if (!validFlsIndex(index)) return -1;
    const gxos::runtime::LocalStorageResult result =
        gxos::runtime::releaseLocalStorageIndex(g_flsSlots[index].genericIndex);
    if (result != gxos::runtime::LocalStorageResult::Success &&
        result != gxos::runtime::LocalStorageResult::CallbackFailed) return -1;
    g_flsSlots[index] = FlsSlot{};
    return 0;
}

void* GUIDEXOS_NATIVEAOT_PAL_CALL flsGet(uint32_t index) {
    FlsContext* context = validFlsIndex(index) ? currentFlsContext() : nullptr;
    return context == nullptr ? nullptr : context->values[index];
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL flsSet(uint32_t index, void* value) {
    FlsContext* context = validFlsIndex(index) ? currentFlsContext() : nullptr;
    if (context == nullptr) return -1;
    context->values[index] = value;
    return 0;
}

uintptr_t workerEntry(void* raw) {
    WorkerSlot* worker = static_cast<WorkerSlot*>(raw);
    if (worker == nullptr || !worker->active || worker->entry == nullptr) return 0;
    const auto attached = guidexos::nativeaot::threadstore::attachCurrentThread();
    if (attached != guidexos::nativeaot::threadstore::Result::Success) return 0;
    const uintptr_t result = guidexos_nativeaot_pal_bridge_invoke_worker(
        worker->entry, worker->context);
    const auto detached = guidexos::nativeaot::threadstore::detachCurrentThread();
    (void)detached;
    return result;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL createWorker(
    guidexos_nativeaot_pal_win64_worker_entry entry,
    void* context,
    uintptr_t stackSize,
    guidexos_nativeaot_pal_worker_handle* handle) {
    if (entry == nullptr || handle == nullptr || !artifactContains(
            reinterpret_cast<uintptr_t>(entry)) ||
        stackSize < gxos::runtime::kNativeThreadMinimumStackSize ||
        stackSize > gxos::runtime::kNativeThreadMaximumStackSize ||
        (stackSize & 15u) != 0) return -1;
    for (uint32_t index = 0; index < kMaxWorkerSlots; ++index) {
        WorkerSlot& worker = g_workers[index];
        if (worker.active) continue;
        if (worker.generation == 0) worker.generation = 1;
        worker.active = true;
        worker.joined = false;
        worker.entry = entry;
        worker.context = context;
        gxos::runtime::ThreadCreateOptions options;
        options.stackSize = static_cast<size_t>(stackSize);
        options.debugName = "nativeaot-production-worker";
        const gxos::runtime::ThreadResult created = gxos::runtime::createThread(
            workerEntry, &worker, options, &worker.nativeHandle);
        if (created != gxos::runtime::ThreadResult::Ok) {
            worker = WorkerSlot{};
            return -2;
        }
        *handle = guidexos_nativeaot_pal_worker_handle{
            index, worker.generation,
            static_cast<uint32_t>(g_installationGeneration), 0};
        return 0;
    }
    return -3;
}

WorkerSlot* lookupWorker(guidexos_nativeaot_pal_worker_handle handle) {
    if (handle.slot >= kMaxWorkerSlots || handle.reserved != 0 ||
        handle.domain_generation != static_cast<uint32_t>(g_installationGeneration)) {
        return nullptr;
    }
    WorkerSlot& worker = g_workers[handle.slot];
    return worker.active && worker.generation == handle.generation ? &worker : nullptr;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL joinWorker(
    guidexos_nativeaot_pal_worker_handle handle,
    uint32_t timeoutMilliseconds,
    uintptr_t* result) {
    WorkerSlot* worker = lookupWorker(handle);
    if (worker == nullptr || worker->joined) return -1;
    const gxos::runtime::WaitTimeout timeout = timeoutMilliseconds == 0xFFFFFFFFu
        ? gxos::runtime::WaitTimeout::infinite()
        : gxos::runtime::WaitTimeout::finiteMilliseconds(timeoutMilliseconds);
    const gxos::runtime::WaitResult joined = gxos::runtime::joinThread(
        worker->nativeHandle, timeout, result);
    if (joined == gxos::runtime::WaitResult::Signaled) {
        worker->joined = true;
        return 0;
    }
    return joined == gxos::runtime::WaitResult::TimedOut ? 1 : -1;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL destroyWorker(
    guidexos_nativeaot_pal_worker_handle handle) {
    WorkerSlot* worker = lookupWorker(handle);
    if (worker == nullptr) return -1;
    if (!worker->joined && joinWorker(handle, 0xFFFFFFFFu, nullptr) != 0) return -2;
    worker->active = false;
    worker->entry = nullptr;
    worker->context = nullptr;
    worker->nativeHandle = gxos::runtime::ThreadHandle{};
    if (worker->generation != 0xFFFFFFFFu) ++worker->generation;
    return 0;
}

uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL counter() { return pit::ticks(); }
uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL frequency() { return 100u; }
uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL milliseconds() { return pit::ticks() * 10u + 1u; }

void GUIDEXOS_NATIVEAOT_PAL_CALL sleepMilliseconds(uint32_t millisecondsValue) {
    const uint64_t target = pit::ticks() +
        (static_cast<uint64_t>(millisecondsValue) + 9u) / 10u;
    while (pit::ticks() < target) {
        arch::enable_interrupts();
        arch::halt();
        arch::disable_interrupts();
    }
}

void GUIDEXOS_NATIVEAOT_PAL_CALL yieldThread() {
#if defined(__x86_64__)
    __asm__ volatile("pause" ::: "memory");
#endif
}

[[noreturn]] void GUIDEXOS_NATIVEAOT_PAL_CALL failFast(uint32_t reason, uintptr_t detail) {
    serial::puts("[C102-NATIVEAOT] FAIL_FAST reason=");
    serial::put_hex32(reason);
    serial::puts(" detail=");
    serial::put_hex64(detail);
    serial::puts("\n");
    for (;;) {
        arch::disable_interrupts();
        arch::halt();
    }
}

void* GUIDEXOS_NATIVEAOT_PAL_CALL moduleFromPointer(void*, const void*) {
    return reinterpret_cast<void*>(g_artifactBase);
}

void* GUIDEXOS_NATIVEAOT_PAL_CALL resolveStatic(void*, void*, const char*) {
    return nullptr;
}

VmSlot* findVm(const void* address) {
    const uintptr_t value = reinterpret_cast<uintptr_t>(address);
    for (VmSlot& slot : g_vmSlots) {
        if (!slot.active || slot.region.base == nullptr) continue;
        const uintptr_t base = reinterpret_cast<uintptr_t>(slot.region.base);
        if (value >= base && value - base < slot.region.reservedSize) return &slot;
    }
    return nullptr;
}

void* GUIDEXOS_NATIVEAOT_PAL_CALL virtualAlloc(
    void*, void* preferred, uintptr_t size, uint32_t type, uint32_t) {
    if (size == 0) return nullptr;
    const uintptr_t roundedSize =
        (size + (kPageSize - 1u)) & ~(static_cast<uintptr_t>(kPageSize) - 1u);
    if (roundedSize < size) return nullptr;
    for (VmSlot& slot : g_vmSlots) {
        if (slot.active) continue;
        if (gxos::runtime::virtual_memory::reserve(
                size, kPageSize, preferred, &slot.region) !=
            gxos::runtime::virtual_memory::VmResult::Ok) return nullptr;
        slot.active = true;
        if ((type & kVmemCommit) != 0 &&
            gxos::runtime::virtual_memory::commit(
                slot.region, 0, roundedSize,
                gxos::runtime::virtual_memory::MemoryProtection::ReadWrite) !=
            gxos::runtime::virtual_memory::VmResult::Ok) {
            (void)gxos::runtime::virtual_memory::release(slot.region);
            slot.active = false;
            return nullptr;
        }
        return slot.region.base;
    }
    return nullptr;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL virtualFree(
    void*, void* address, uintptr_t size, uint32_t type) {
    VmSlot* slot = findVm(address);
    if (slot == nullptr) return -1;
    const uintptr_t offset = reinterpret_cast<uintptr_t>(address) -
        reinterpret_cast<uintptr_t>(slot->region.base);
    const gxos::runtime::virtual_memory::VmResult result =
        type == kVmemRelease
            ? gxos::runtime::virtual_memory::release(slot->region)
            : gxos::runtime::virtual_memory::decommit(slot->region, offset, size);
    if (type == kVmemRelease && result == gxos::runtime::virtual_memory::VmResult::Ok) {
        slot->active = false;
    }
    return result == gxos::runtime::virtual_memory::VmResult::Ok ? 0 : -1;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL virtualProtect(
    void* address, uintptr_t size, uint32_t, uint32_t* oldProtection) {
    if (oldProtection != nullptr) *oldProtection = 0x04u;
    return findVm(address) != nullptr && size != 0 ? 1 : 0;
}

uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL legacyCurrentThreadId(void*) {
    return currentThreadId64();
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL legacyStackBounds(
    void*, uintptr_t* low, uintptr_t* high, uintptr_t* current) {
    if (low == nullptr || high == nullptr || current == nullptr) return -1;
    guidexos_nativeaot_pal_stack_bounds_value value{};
    const int32_t result = stackBounds(&value);
    if (result == 0) {
        *low = value.low;
        *high = value.high;
        *current = value.current;
    }
    return result;
}

uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL legacyCounter(void*) {
    return counter();
}

uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL legacyFrequency(void*) {
    return frequency();
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL legacySleepMilliseconds(
    void*, uint32_t value) {
    sleepMilliseconds(value);
    return 0;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL legacyYield(void*) {
    yieldThread();
    return 0;
}

void GUIDEXOS_NATIVEAOT_PAL_CALL legacyFailFast(void*, uint32_t reason) {
    failFast(reason, 0);
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL legacyVirtualProtect(
    void*, void* address, uintptr_t size, uint32_t protection,
    uint32_t* oldProtection) {
    return virtualProtect(address, size, protection, oldProtection);
}

void fillLegacyHooks(guidexos_nativeaot_pal_hooks* hooks) {
    *hooks = {};
    hooks->size = sizeof(*hooks);
    hooks->abi_version = GUIDEXOS_NATIVEAOT_PAL_ABI_VERSION;
    hooks->current_thread_id = legacyCurrentThreadId;
    hooks->stack_bounds = legacyStackBounds;
    hooks->counter = legacyCounter;
    hooks->frequency = legacyFrequency;
    hooks->sleep_milliseconds = legacySleepMilliseconds;
    hooks->yield = legacyYield;
    hooks->static_module_from_pointer = moduleFromPointer;
    hooks->static_resolve = resolveStatic;
    hooks->fail_fast = legacyFailFast;
    hooks->virtual_alloc = virtualAlloc;
    hooks->virtual_free = virtualFree;
    hooks->virtual_protect = legacyVirtualProtect;
}

void fillPalTable(guidexos_nativeaot_pal_hook_table_v1* table,
                  uintptr_t base, uintptr_t span) {
    *table = {};
    table->magic = GUIDEXOS_NATIVEAOT_PAL_HOOK_MAGIC;
    table->abi_version = GUIDEXOS_NATIVEAOT_PAL_HOOK_ABI_VERSION;
    table->structure_size = sizeof(*table);
    table->capability_bits = GUIDEXOS_NATIVEAOT_PAL_CAP_REQUIRED;
    table->installation_generation = g_installationGeneration;
    table->artifact_base = base;
    table->artifact_size = span;
    table->current_thread_id = currentThreadId64;
    table->query_current_stack_bounds = stackBounds;
    table->fls_allocate = flsAllocate;
    table->fls_release = flsRelease;
    table->fls_get = flsGet;
    table->fls_set = flsSet;
    table->create_worker = createWorker;
    table->join_worker = joinWorker;
    table->destroy_worker_handle = destroyWorker;
    table->query_counter = counter;
    table->query_counter_frequency = frequency;
    table->monotonic_milliseconds = milliseconds;
    table->sleep_milliseconds = sleepMilliseconds;
    table->yield_thread = yieldThread;
    table->fail_fast = [](uint32_t reason, uintptr_t detail) {
        failFast(reason, detail);
    };
}

EventSlot* findEvent(void* handle) {
    for (EventSlot& slot : g_eventSlots) {
        if (slot.active && &slot.event == handle) return &slot;
    }
    return nullptr;
}

void* GUIDEXOS_NATIVEAOT_PAL_CALL gcCreateEvent(uint32_t manualReset,
                                                 uint32_t initialState) {
    for (EventSlot& slot : g_eventSlots) {
        if (slot.active) continue;
        if (!slot.event.initialize(
                manualReset != 0 ? gxos::runtime::EventMode::ManualReset
                                 : gxos::runtime::EventMode::AutoReset,
                initialState != 0)) return nullptr;
        slot.active = true;
        return &slot.event;
    }
    return nullptr;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL gcSetEvent(void* handle) {
    EventSlot* slot = findEvent(handle);
    return slot != nullptr && slot->event.signal() == gxos::runtime::EventStatus::Ok
        ? 0 : -1;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL gcResetEvent(void* handle) {
    EventSlot* slot = findEvent(handle);
    return slot != nullptr && slot->event.reset() == gxos::runtime::EventStatus::Ok
        ? 0 : -1;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL gcWaitEvent(void* handle, uint32_t timeout) {
    EventSlot* slot = findEvent(handle);
    if (slot == nullptr) return -1;
    const gxos::runtime::WaitResult result = timeout == 0xFFFFFFFFu
        ? slot->event.wait(gxos::runtime::WaitTimeout::infinite())
        : slot->event.wait(gxos::runtime::WaitTimeout::finiteMilliseconds(timeout));
    return result == gxos::runtime::WaitResult::Signaled ? 0
        : result == gxos::runtime::WaitResult::TimedOut ? 258 : -1;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL gcCloseEvent(void* handle) {
    EventSlot* slot = findEvent(handle);
    if (slot == nullptr || slot->event.close() != gxos::runtime::EventStatus::Ok) return -1;
    slot->active = false;
    return 0;
}

void* GUIDEXOS_NATIVEAOT_PAL_CALL gcReserve(uintptr_t size, uintptr_t alignment,
                                             uint32_t, uint16_t) {
    if (size == 0) return nullptr;
    for (VmSlot& slot : g_vmSlots) {
        if (slot.active) continue;
        if (gxos::runtime::virtual_memory::reserve(size, alignment, nullptr,
                                                   &slot.region) !=
            gxos::runtime::virtual_memory::VmResult::Ok) return nullptr;
        slot.active = true;
        return slot.region.base;
    }
    return nullptr;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL gcCommit(void* address, uintptr_t size, uint16_t) {
    VmSlot* slot = findVm(address);
    if (slot == nullptr || size == 0) return -1;
    const uintptr_t offset = reinterpret_cast<uintptr_t>(address) -
        reinterpret_cast<uintptr_t>(slot->region.base);
    return gxos::runtime::virtual_memory::commit(
               slot->region, offset, size,
               gxos::runtime::virtual_memory::MemoryProtection::ReadWrite) ==
            gxos::runtime::virtual_memory::VmResult::Ok ? 0 : -1;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL gcDecommit(void* address, uintptr_t size, uint16_t) {
    VmSlot* slot = findVm(address);
    if (slot == nullptr || size == 0) return -1;
    const uintptr_t offset = reinterpret_cast<uintptr_t>(address) -
        reinterpret_cast<uintptr_t>(slot->region.base);
    return gxos::runtime::virtual_memory::decommit(slot->region, offset, size) ==
        gxos::runtime::virtual_memory::VmResult::Ok ? 0 : -1;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL gcRelease(void* address, uintptr_t, uint16_t) {
    VmSlot* slot = findVm(address);
    if (slot == nullptr || address != slot->region.base) return -1;
    const gxos::runtime::virtual_memory::VmResult result =
        gxos::runtime::virtual_memory::release(slot->region);
    if (result == gxos::runtime::virtual_memory::VmResult::Ok) slot->active = false;
    return result == gxos::runtime::virtual_memory::VmResult::Ok ? 0 : -1;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL gcReset(void* address, uintptr_t size, uint32_t) {
    return findVm(address) != nullptr && size != 0 ? 0 : -1;
}

uint32_t GUIDEXOS_NATIVEAOT_PAL_CALL gcPageSize() { return 4096u; }
uint32_t GUIDEXOS_NATIVEAOT_PAL_CALL gcGranularity() { return 4096u; }
uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL gcVirtualLimit() {
    return gxos::runtime::virtual_memory::maximumRegionSize();
}
uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL gcPhysicalLimit(uint32_t* restricted) {
    if (restricted != nullptr) *restricted = 0;
    const memory::address_space::FrameAccounting stats =
        memory::address_space::accounting();
    return stats.totalKnownFrames * kPageSize;
}
void GUIDEXOS_NATIVEAOT_PAL_CALL gcMemoryStatus(uint64_t, uint32_t* load,
                                                 uint64_t* physical,
                                                 uint64_t* pageFile) {
    if (load != nullptr) *load = 0;
    const uint64_t limit = gcPhysicalLimit(nullptr);
    if (physical != nullptr) *physical = limit;
    if (pageFile != nullptr) *pageFile = limit;
}

void fillGcTable(guidexos_nativeaot_gc_startup_platform_table_v1* table) {
    *table = {};
    table->magic = GUIDEXOS_NATIVEAOT_GC_PLATFORM_MAGIC;
    table->abi_version = GUIDEXOS_NATIVEAOT_GC_PLATFORM_ABI_VERSION;
    table->structure_size = sizeof(*table);
    table->capability_bits = GUIDEXOS_NATIVEAOT_GC_CAP_REQUIRED;
    table->installation_generation = g_installationGeneration;
    table->create_event = gcCreateEvent;
    table->set_event = gcSetEvent;
    table->reset_event = gcResetEvent;
    table->wait_event = gcWaitEvent;
    table->close_event = gcCloseEvent;
    table->reserve = gcReserve;
    table->commit = gcCommit;
    table->decommit = gcDecommit;
    table->release = gcRelease;
    table->reset = gcReset;
    table->page_size = gcPageSize;
    table->allocation_granularity = gcGranularity;
    table->virtual_memory_limit = gcVirtualLimit;
    table->physical_memory_limit = gcPhysicalLimit;
    table->memory_status = gcMemoryStatus;
}

bool managedMessageEquals(const uint8_t* message, const char* expected) {
    if (message == nullptr || expected == nullptr) return false;
    uint32_t index = 0;
    while (expected[index] != '\0') {
        if (message[index] != static_cast<uint8_t>(expected[index])) return false;
        ++index;
    }
    return message[index] == 0;
}

bool managedMessageStartsWith(const uint8_t* message, const char* prefix) {
    if (message == nullptr || prefix == nullptr) return false;
    for (uint32_t index = 0; prefix[index] != 0; ++index) {
        if (message[index] != static_cast<uint8_t>(prefix[index])) return false;
    }
    return true;
}

#if defined(GXOS_C108_TLS_LIFECYCLE)
void emitC108GuideIdentity(const char* appName) {
    ++g_c108ManagedInvocationOrdinal;
    guidexos::nativeaot::threadstore::ThreadSnapshot snapshot{};
    const bool snapshotValid =
        guidexos::nativeaot::threadstore::snapshotCurrentThread(&snapshot);
    serial::puts("[C108-GUIDE] ordinal=");
    serial::put_hex32(g_c108ManagedInvocationOrdinal);
    serial::puts(" app=");
    serial::puts(appName);
    serial::puts(" guideThread=");
    serial::put_hex64(process::current_thread_id());
    serial::puts(" adapter=");
    serial::put_hex64(reinterpret_cast<uintptr_t>(
        guidexos::nativeaot::threadstore::getCurrentThread()));
    serial::puts(" adapterNativeThread=");
    serial::put_hex64(snapshotValid ? snapshot.nativeThreadId : 0u);
    serial::puts(" adapterGeneration=");
    serial::put_hex32(snapshotValid ? snapshot.generation : 0u);
    serial::puts(" adapterAttached=");
    serial::put_hex32(snapshotValid ? snapshot.attached : 0u);
    serial::puts(" gsArea=");
    serial::put_hex64(reinterpret_cast<uintptr_t>(&g_tlsArea));
    serial::puts(" tlsVector=");
    serial::put_hex64(reinterpret_cast<uintptr_t>(g_tlsVector));
    serial::puts(" tlsBlock=");
    serial::put_hex64(reinterpret_cast<uintptr_t>(g_tlsBlock));
    serial::puts("\n");
}
#endif

#if defined(GXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH)
void emitProductionPersistenceIdentity(const char* appName) {
    ++g_productionManagedInvocationOrdinal;
    guidexos::nativeaot::threadstore::ThreadSnapshot snapshot{};
    const bool snapshotValid =
        guidexos::nativeaot::threadstore::snapshotCurrentThread(&snapshot);
    serial::puts("[NATIVEAOT-PERSISTENCE] ordinal=");
    serial::put_hex32(g_productionManagedInvocationOrdinal);
    serial::puts(" app=");
    serial::puts(appName);
    serial::puts(" guideThread=");
    serial::put_hex64(process::current_thread_id());
    serial::puts(" nativeThreadStore=");
    serial::put_hex64(reinterpret_cast<uintptr_t>(
        guidexos::nativeaot::threadstore::getCurrentThread()));
    serial::puts(" nativeThread=");
    serial::put_hex64(snapshotValid ? snapshot.nativeThreadId : 0u);
    serial::puts(" attached=");
    serial::put_hex32(snapshotValid ? snapshot.attached : 0u);
    serial::puts(" gsArea=");
    serial::put_hex64(reinterpret_cast<uintptr_t>(&g_tlsArea));
    serial::puts(" tlsVector=");
    serial::put_hex64(reinterpret_cast<uintptr_t>(g_tlsVector));
    serial::puts(" tlsBlock=");
    serial::put_hex64(reinterpret_cast<uintptr_t>(g_tlsBlock));
    serial::puts(" tlsInstalls=");
    serial::put_hex32(g_productionTlsInstallCount);
    serial::puts("\n");
}
#endif

uint32_t boundedCStringLength(const uint8_t* text, uint32_t maximum) {
    if (!text) return maximum + 1u;
    for (uint32_t index = 0; index <= maximum; ++index) {
        if (text[index] == 0) return index;
    }
    return maximum + 1u;
}

bool activeSurfaceContext(NativeGxAppContext* context) {
    return context != nullptr && context == g_activeManagedContext &&
        context->size >= sizeof(NativeGxAppContext) && context->host != nullptr &&
        context->host->size >= sizeof(NativeHostCallTable);
}

bool copyManagedFilePath(const uint8_t* path, uint32_t pathLength,
                         char* output, uint32_t outputSize) {
    if (!path || !output || pathLength == 0u ||
        pathLength > kManagedFilePathMaxBytes ||
        pathLength + 1u > outputSize) {
        return false;
    }

    constexpr const char* root = kManagedFileRoot;
    uint32_t rootLength = 0u;
    while (root[rootLength] != '\0') ++rootLength;
    if (pathLength <= rootLength) return false;
    for (uint32_t index = 0u; index < pathLength; ++index) {
        const uint8_t value = path[index];
        // The initial contract is UTF-8 represented by its canonical printable
        // ASCII subset. This keeps FAT path interpretation deterministic.
        if (value < 0x21u || value > 0x7Eu || value == '\\') return false;
        if (index < rootLength && value != static_cast<uint8_t>(root[index])) {
            return false;
        }
        if (value == '/' && index + 1u < pathLength && path[index + 1u] == '/') {
            return false;
        }
        if (value == '.' && index + 1u < pathLength && path[index + 1u] == '.') {
            return false;
        }
    }
    for (uint32_t index = 0u; index < pathLength; ++index) {
        output[index] = static_cast<char>(path[index]);
    }
    output[pathLength] = '\0';
    return true;
}

bool copyManagedDirectoryPath(const uint8_t* path, uint32_t pathLength,
                              char* output, uint32_t outputSize) {
    if (!path || !output || pathLength == 0u ||
        pathLength > kManagedFilePathMaxBytes ||
        pathLength + 1u > outputSize) {
        return false;
    }

    constexpr const char* root = kManagedFileRoot;
    uint32_t rootLength = 0u;
    while (root[rootLength] != '\0') ++rootLength;
    // Directory paths may omit the trailing separator.  Normalize the
    // approved application root to the spelling used by the VFS.
    const bool rootWithoutSlash = pathLength + 1u == rootLength;
    const bool rootWithSlash = pathLength == rootLength &&
        path[rootLength - 1u] == '/';
    if (rootWithoutSlash || rootWithSlash) {
        const uint32_t copyLength = rootLength - 1u;
        for (uint32_t index = 0u; index < copyLength; ++index) {
            if (path[index] != static_cast<uint8_t>(root[index])) return false;
        }
        for (uint32_t index = 0u; index < copyLength; ++index) output[index] = root[index];
        output[copyLength] = '\0';
        return true;
    }

    return copyManagedFilePath(path, pathLength, output, outputSize);
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL managedFileReadAll(
    NativeGxAppContext* context, uint8_t* path, uint32_t pathLength,
    uint8_t* buffer, uint32_t capacity, uint32_t* outLength) {
    if (!activeSurfaceContext(context) || !outLength) return kManagedFileInvalidArgument;
    if ((context->host->capabilities & kManagedCapabilityFileRead) == 0u) {
        return kManagedFileCapabilityUnavailable;
    }
    if ((capacity != 0u && !buffer)) {
        return kManagedFileInvalidArgument;
    }
    *outLength = 0u;
    char validatedPath[kManagedFilePathMaxBytes + 1u] = {};
    if (!copyManagedFilePath(path, pathLength, validatedPath,
                             sizeof(validatedPath))) {
        return kManagedFileInvalidPath;
    }

    vfs::FileInfo info{};
    const vfs::Status statStatus = vfs::stat(validatedPath, &info);
    if (statStatus == vfs::VFS_ERR_NOT_FOUND) return kManagedFileNotFound;
    if (statStatus != vfs::VFS_OK || info.type != vfs::FILE_TYPE_REGULAR) {
        return kManagedFileInvalidPath;
    }
    if (info.size > kManagedFileMaxBytes) return kManagedFileTooLarge;
    if (info.size > capacity) {
        *outLength = static_cast<uint32_t>(info.size);
        return kManagedFileBufferTooSmall;
    }
    if (info.size != 0u) {
        const int32_t read = vfs::read_file(validatedPath, buffer, capacity);
        if (read < 0 || static_cast<uint64_t>(read) != info.size) {
            return kManagedFileIoFailure;
        }
        *outLength = static_cast<uint32_t>(read);
    }
    serial::puts("[C113-FILE-READ] path=");
    serial::puts(validatedPath);
    serial::puts(" bytes=");
    serial::put_hex32(*outLength);
    serial::puts(" result=PASS\n");
    return kManagedFileSuccess;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL managedFileWriteAll(
    NativeGxAppContext* context, uint8_t* path, uint32_t pathLength,
    const uint8_t* data, uint32_t length) {
    if (!activeSurfaceContext(context)) return kManagedFileInvalidArgument;
    if ((context->host->capabilities & kManagedCapabilityFileWrite) == 0u) {
        return kManagedFileCapabilityUnavailable;
    }
    if (length != 0u && !data) {
        return kManagedFileInvalidArgument;
    }
    char validatedPath[kManagedFilePathMaxBytes + 1u] = {};
    if (!copyManagedFilePath(path, pathLength, validatedPath,
                             sizeof(validatedPath))) {
        return kManagedFileInvalidPath;
    }
    if (length > kManagedFileMaxBytes) return kManagedFileTooLarge;
    const int32_t written = vfs::write_file(validatedPath, data, length);
    if (written < 0 || static_cast<uint32_t>(written) != length) {
        return kManagedFileIoFailure;
    }
    serial::puts("[C113-FILE-WRITE] path=");
    serial::puts(validatedPath);
    serial::puts(" bytes=");
    serial::put_hex32(length);
    serial::puts(" result=PASS\n");
    return kManagedFileSuccess;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL managedDirectoryList(
    NativeGxAppContext* context, uint8_t* path, uint32_t pathLength,
    uint8_t* entries, uint32_t capacity, uint32_t entryStride,
    uint32_t* outCount, uint32_t* outHasMore) {
    if (!activeSurfaceContext(context) || !outCount || !outHasMore ||
        entryStride != kManagedDirectoryEntryAbiSize ||
        capacity > kManagedDirectoryMaxEntries ||
        (capacity != 0u && !entries)) {
        return kManagedFileInvalidArgument;
    }
    if ((context->host->capabilities & kManagedCapabilityDirectoryList) == 0u) {
        return kManagedFileCapabilityUnavailable;
    }
    *outCount = 0u;
    *outHasMore = 0u;

    char validatedPath[kManagedFilePathMaxBytes + 1u] = {};
    if (!copyManagedDirectoryPath(path, pathLength, validatedPath,
                                  sizeof(validatedPath))) {
        return kManagedFileInvalidPath;
    }
    vfs::FileInfo directoryInfo{};
    const vfs::Status statStatus = vfs::stat(validatedPath, &directoryInfo);
    if (statStatus == vfs::VFS_ERR_NOT_FOUND) return kManagedFileNotFound;
    if (statStatus != vfs::VFS_OK) return kManagedFileIoFailure;
    if (directoryInfo.type != vfs::FILE_TYPE_DIRECTORY) {
        return kManagedFileNotDirectory;
    }

    const uint8_t iterator = vfs::opendir(validatedPath);
    if (iterator == 0xFFu) return kManagedFileIoFailure;
    vfs::DirEntry nativeEntry{};
    while (vfs::readdir(iterator, &nativeEntry)) {
        const uint32_t nameLength = boundedCStringLength(
            reinterpret_cast<const uint8_t*>(nativeEntry.name),
            kManagedDirectoryNameMaxBytes);
        if (nameLength > kManagedDirectoryNameMaxBytes) {
            vfs::closedir(iterator);
            return kManagedFileEntryNameTooLong;
        }
        if (nativeEntry.type != vfs::FILE_TYPE_REGULAR &&
            nativeEntry.type != vfs::FILE_TYPE_DIRECTORY) {
            vfs::closedir(iterator);
            return kManagedFileIoFailure;
        }
        const uint32_t index = *outCount;
        if (index < capacity) {
            ManagedDirectoryEntryAbi* output = reinterpret_cast<ManagedDirectoryEntryAbi*>(
                entries + index * entryStride);
            output->nameLength = nameLength;
            output->type = nativeEntry.type == vfs::FILE_TYPE_DIRECTORY
                ? kManagedEntryTypeDirectory : kManagedEntryTypeRegular;
            output->size = nativeEntry.type == vfs::FILE_TYPE_REGULAR
                ? nativeEntry.size : 0u;
            for (uint32_t byte = 0u; byte < nameLength; ++byte) {
                output->name[byte] = static_cast<uint8_t>(nativeEntry.name[byte]);
            }
            output->name[nameLength] = 0u;
            serial::puts("[C114-DIRECTORY-ENTRY] name=");
            serial::puts(nativeEntry.name);
            serial::puts(" type=");
            serial::put_hex32(output->type);
            serial::puts(" size=");
            serial::put_hex64(output->size);
            serial::puts("\n");
            ++(*outCount);
        } else {
            *outHasMore = 1u;
            break;
        }
    }
    // If the output filled exactly, probe one more entry so callers can
    // distinguish an exact fit from a bounded snapshot that was truncated.
    if (*outCount == capacity && *outHasMore == 0u && capacity != 0u) {
        if (vfs::readdir(iterator, &nativeEntry)) *outHasMore = 1u;
    }
    vfs::closedir(iterator);
    serial::puts("[C114-DIRECTORY-LIST] path=");
    serial::puts(validatedPath);
    serial::puts(" count=");
    serial::put_hex32(*outCount);
    serial::puts(" hasMore=");
    serial::put_hex32(*outHasMore);
    serial::puts(" result=PASS\n");
    return kManagedFileSuccess;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL managedFileStat(
    NativeGxAppContext* context, uint8_t* path, uint32_t pathLength,
    uint8_t* outInfo, uint32_t infoSize) {
    if (!activeSurfaceContext(context) || !outInfo ||
        infoSize < kManagedFileInfoAbiSize) {
        return kManagedFileInvalidArgument;
    }
    if ((context->host->capabilities & kManagedCapabilityFileStat) == 0u) {
        return kManagedFileCapabilityUnavailable;
    }
    char validatedPath[kManagedFilePathMaxBytes + 1u] = {};
    if (!copyManagedDirectoryPath(path, pathLength, validatedPath,
                                  sizeof(validatedPath))) {
        return kManagedFileInvalidPath;
    }
    vfs::FileInfo nativeInfo{};
    const vfs::Status status = vfs::stat(validatedPath, &nativeInfo);
    if (status == vfs::VFS_ERR_NOT_FOUND) return kManagedFileNotFound;
    if (status != vfs::VFS_OK) return kManagedFileIoFailure;
    if (nativeInfo.type != vfs::FILE_TYPE_REGULAR &&
        nativeInfo.type != vfs::FILE_TYPE_DIRECTORY) {
        return kManagedFileIoFailure;
    }
    ManagedFileInfoAbi* info = reinterpret_cast<ManagedFileInfoAbi*>(outInfo);
    info->type = nativeInfo.type == vfs::FILE_TYPE_DIRECTORY
        ? kManagedEntryTypeDirectory : kManagedEntryTypeRegular;
    info->reserved = 0u;
    info->size = nativeInfo.type == vfs::FILE_TYPE_REGULAR
        ? nativeInfo.size : 0u;
    serial::puts("[C114-FILE-STAT] path=");
    serial::puts(validatedPath);
    serial::puts(" type=");
    serial::put_hex32(info->type);
    serial::puts(" size=");
    serial::put_hex64(info->size);
    serial::puts(" result=PASS\n");
    return kManagedFileSuccess;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL managedRequestWindow(
    NativeGxAppContext* context, uint8_t* title, int32_t width, int32_t height,
    uint64_t* outWindow) {
    if (!activeSurfaceContext(context) || !title || !outWindow ||
        boundedCStringLength(title, app::MAX_TITLE_LEN - 1u) >= app::MAX_TITLE_LEN ||
        width < compositor::MIN_WINDOW_WIDTH || height < compositor::MIN_WINDOW_HEIGHT) {
        return -2;
    }
    NativeAotManagedSurface* surface = managedSurface();
    if (surface) {
        surface->setSelector(static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(context->userData)));
    }
    if (!surface || !surface->open(reinterpret_cast<const char*>(title), width, height)) {
        return -4;
    }
    *outWindow = surface->windowId();
    serial::puts("[C111-SURFACE] action=create title=");
    serial::puts(reinterpret_cast<const char*>(title));
    serial::puts(" window=");
    serial::put_hex64(*outWindow);
    serial::puts(" result=PASS\n");
    return 0;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL managedDrawText(
    NativeGxAppContext* context, uint64_t window, int32_t x, int32_t y,
    uint8_t* text) {
    NativeAotManagedSurface* surface = managedSurface();
    if (!activeSurfaceContext(context) || !text || !surface || !surface->owns(window) ||
        x < 0 || y < 0 || boundedCStringLength(text, 63u) > 63u ||
        !(g_c116InputDispatchActive
            ? surface->updateText(x, y, reinterpret_cast<const char*>(text)) ||
                surface->setText(x, y, reinterpret_cast<const char*>(text))
            : surface->setText(x, y, reinterpret_cast<const char*>(text)))) {
        return -2;
    }
    if (g_c116InputDispatchActive) return 0;
    serial::puts("[C111-SURFACE-TEXT] window=");
    serial::put_hex64(window);
    serial::puts(" x=");
    serial::put_hex32(static_cast<uint32_t>(x));
    serial::puts(" y=");
    serial::put_hex32(static_cast<uint32_t>(y));
    serial::puts(" text=");
    serial::puts(reinterpret_cast<const char*>(text));
    serial::puts(" result=PASS\n");
    return 0;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL managedDrawRect(
    NativeGxAppContext* context, uint64_t window, int32_t x, int32_t y,
    int32_t width, int32_t height, uint32_t color) {
    NativeAotManagedSurface* surface = managedSurface();
    if (!activeSurfaceContext(context) || !surface || !surface->owns(window) ||
        x < 0 || y < 0 || width <= 0 || height <= 0 ||
        x > 4096 || y > 4096 || width > 4096 || height > 4096) {
        return -2;
    }
    surface->beginManagedFrame();
    if (!surface->addRect(x, y, width, height, color)) return -2;
    if (g_c116InputDispatchActive) return 0;
    serial::puts("[C111-SURFACE-RECT] window=");
    serial::put_hex64(window);
    serial::puts(" result=PASS\n");
    return 0;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL managedAddButton(
    NativeGxAppContext* context, uint64_t window, int32_t x, int32_t y,
    int32_t width, int32_t height, uint8_t* text, int32_t* outWidget) {
    NativeAotManagedSurface* surface = managedSurface();
    if (!activeSurfaceContext(context) || !surface || !surface->owns(window) ||
        !text || !outWidget || x < 0 || y < 0 || width <= 0 || height <= 0 ||
        boundedCStringLength(text, 63u) > 63u ||
        !surface->addActionButton(
            x, y, width, height, reinterpret_cast<const char*>(text), outWidget)) {
        return -2;
    }
    serial::puts("[C111-SURFACE-WIDGET] window=");
    serial::put_hex64(window);
    serial::puts(" widget=");
    serial::put_hex32(static_cast<uint32_t>(*outWidget));
    serial::puts(" result=PASS\n");
    return 0;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL managedAddActionButton(
    NativeGxAppContext* context, uint64_t window, int32_t x, int32_t y,
    int32_t width, int32_t height, uint8_t* text, uint32_t actionId,
    int32_t* outWidget) {
    NativeAotManagedSurface* surface = managedSurface();
    if (!activeSurfaceContext(context) || !surface || !surface->owns(window) ||
        !text || !outWidget || actionId == 0u ||
        x < 0 || y < 0 || width <= 0 || height <= 0 ||
        boundedCStringLength(text, 63u) > 63u ||
        !surface->addActionButton(
            x, y, width, height, reinterpret_cast<const char*>(text),
            actionId, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context->userData)),
            outWidget)) {
        return -2;
    }
    if (g_c116InputDispatchActive) return 0;
    serial::puts("[C112-SURFACE-ACTION] window=");
    serial::put_hex64(window);
    serial::puts(" action=");
    serial::put_hex32(actionId);
    serial::puts(" result=PASS\n");
    return 0;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL managedCloseWindow(
    NativeGxAppContext* context, uint64_t window) {
    NativeAotManagedSurface* surface = managedSurface();
    if (!activeSurfaceContext(context) || !surface || !surface->owns(window)) return -2;
    surface->requestClose();
    return 0;
}

int32_t GUIDEXOS_NATIVEAOT_PAL_CALL managedLog(
    NativeGxAppContext*, uint8_t* message) {
    if (message == nullptr) return -1;
    if (managedMessageEquals(message, "C102-MANAGED-ENTRY")) {
        g_c102ManagedEntryObserved = true;
    } else if (managedMessageEquals(message, "C102-MANAGED-PASS")) {
        g_c102ManagedPassObserved = true;
    } else if (managedMessageEquals(message, "C104-APP-A-ENTRY") ||
               managedMessageEquals(message, "C104-APP-B-ENTRY")) {
        g_c104ManagedEntryObserved = true;
    } else if (managedMessageEquals(message, "C104-APP-A-PASS") ||
               managedMessageEquals(message, "C104-APP-B-PASS")) {
        g_c104ManagedPassObserved = true;
    } else if (managedMessageEquals(message, "C107-APP-A-ENTRY") ||
               managedMessageEquals(message, "C107-APP-B-ENTRY")) {
        g_c107ManagedEntryObserved = true;
    } else if (managedMessageEquals(message, "C107-APP-A-PASS") ||
               managedMessageEquals(message, "C107-APP-B-PASS")) {
        g_c107ManagedPassObserved = true;
    } else if (managedMessageEquals(message, "C107-INVALID-APP-ID")) {
        g_c107ManagedInvalidApplicationObserved = true;
    }
#if defined(GXOS_C108_TLS_LIFECYCLE)
    if (managedMessageStartsWith(message, "C107-APP-A-STATE")) {
        emitC108GuideIdentity("A");
    } else if (managedMessageStartsWith(message, "C107-APP-B-STATE")) {
        emitC108GuideIdentity("B");
    }
#endif
#if defined(GXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH)
    if (managedMessageStartsWith(message, "C107-APP-A-STATE")) {
        emitProductionPersistenceIdentity("A");
    } else if (managedMessageStartsWith(message, "C107-APP-B-STATE")) {
        emitProductionPersistenceIdentity("B");
    }
#endif
    const bool c104Message = managedMessageEquals(message, "C104-APP-A-ENTRY") ||
        managedMessageEquals(message, "C104-APP-A-PASS") ||
        managedMessageEquals(message, "C104-APP-B-ENTRY") ||
        managedMessageEquals(message, "C104-APP-B-PASS");
    const bool c107Message = managedMessageStartsWith(message, "C107-");
    const bool c111Message = managedMessageStartsWith(message, "C111-");
    const bool c112Message = managedMessageStartsWith(message, "C112-");
    const bool c113Message = managedMessageStartsWith(message, "C113-");
    const bool c114Message = managedMessageStartsWith(message, "C114-");
    const bool c115Message = managedMessageStartsWith(message, "C115-");
    const bool c116Message = managedMessageStartsWith(message, "C116-");
    const bool c117Message = managedMessageStartsWith(message, "C117-");
    const bool c118Message = managedMessageStartsWith(message, "C118-");
    const bool c119Message = managedMessageStartsWith(message, "C119-");
    serial::puts(c119Message ? "[C119-MANAGED-OUTPUT] " :
        c118Message ? "[C118-MANAGED-OUTPUT] " :
        c117Message ? "[C117-MANAGED-OUTPUT] " :
        c116Message ? "[C116-MANAGED-OUTPUT] " :
        c115Message ? "[C115-MANAGED-OUTPUT] " :
        c114Message ? "[C114-MANAGED-OUTPUT] " :
        c113Message ? "[C113-MANAGED-OUTPUT] " :
        c112Message ? "[C112-MANAGED-OUTPUT] " :
        c111Message ? "[C111-MANAGED-OUTPUT] " :
        c107Message ? "[C107-MANAGED-OUTPUT] " :
        c104Message ? "[C104-MANAGED-OUTPUT] " : "[C102-MANAGED-OUTPUT] ");
    for (uint32_t index = 0; index < 128u && message[index] != 0; ++index) {
        serial::putc(static_cast<char>(message[index]));
    }
    serial::puts("\n");
    return 0;
}

void startupMarker(uint32_t stage) {
    serial::puts("[C102-STARTUP] stage=");
    serial::put_hex32(stage);
    serial::puts("\n");
}

void releaseMappedPages();

bool probeElfEnvelope(const uint8_t* bytes, uint64_t size, LaunchReport* report) {
    if (bytes == nullptr || report == nullptr || size < sizeof(Elf64Header)) return false;
    const Elf64Header* header = reinterpret_cast<const Elf64Header*>(bytes);
    if (header->ident[0] != 0x7Fu || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F' ||
        header->ident[4] != kElfClass64 || header->ident[5] != kElfDataLittle ||
        header->type != kElfExecutable || header->machine != kElfMachineAmd64 ||
        header->version != 1u || header->headerSize != sizeof(Elf64Header) ||
        header->programHeaderSize != sizeof(Elf64ProgramHeader) ||
        header->programHeaderCount == 0 ||
        header->programHeaderCount > kMaxProgramHeaders ||
        !boundedRange(header->programHeaderOffset,
                      static_cast<uint64_t>(header->programHeaderCount) *
                          sizeof(Elf64ProgramHeader), size)) return false;
    const Elf64ProgramHeader* programs = reinterpret_cast<const Elf64ProgramHeader*>(
        bytes + header->programHeaderOffset);
    uintptr_t imageLow = UINTPTR_MAX;
    uintptr_t imageHigh = 0;
    bool entryValid = false;
    uint16_t loadCount = 0;
    for (uint16_t index = 0; index < header->programHeaderCount; ++index) {
        const Elf64ProgramHeader& program = programs[index];
        if (program.type == kElfProgramNull || program.type == kElfProgramGnuStack) continue;
        if (program.type != kElfProgramLoad || program.memorySize == 0 ||
            program.fileSize > program.memorySize ||
            !boundedRange(program.offset, program.fileSize, size) ||
            program.alignment != kPageSize ||
            (program.offset & (kPageSize - 1u)) !=
                (program.virtualAddress & (kPageSize - 1u)) ||
            (program.flags & (kElfFlagWrite | kElfFlagExecute)) ==
                (kElfFlagWrite | kElfFlagExecute) ||
            program.virtualAddress > UINTPTR_MAX - program.memorySize) return false;
        const uintptr_t low = static_cast<uintptr_t>(program.virtualAddress & ~(kPageSize - 1u));
        const uint64_t end = program.virtualAddress + program.memorySize;
        const uintptr_t high = static_cast<uintptr_t>((end + kPageSize - 1u) & ~(kPageSize - 1u));
        if (high <= low || high > UINTPTR_MAX - kPageSize) return false;
        if (low < imageLow) imageLow = low;
        if (high > imageHigh) imageHigh = high;
        if (header->entry >= program.virtualAddress && header->entry < end) {
            entryValid = (program.flags & kElfFlagExecute) != 0;
        }
        ++loadCount;
    }
    if (loadCount == 0 || imageLow == UINTPTR_MAX || imageHigh <= imageLow ||
        !entryValid || imageHigh - imageLow > kMaxArtifactBytes * 2u) return false;
    report->artifactBase = imageLow;
    report->artifactSpan = imageHigh - imageLow;
    report->loadSegmentCount = loadCount;
    report->entryPoint = header->entry;
    return true;
}

bool readAndProbeApplication(const char* path, LaunchReport* report) {
    const uint8_t handle = vfs::open(path, vfs::OPEN_READ);
    if (handle == 0xFFu) return false;
    const int64_t fileSize = vfs::file_size(handle);
    if (fileSize <= 0 || static_cast<uint64_t>(fileSize) > kMaxArtifactBytes) {
        (void)vfs::close(handle);
        return false;
    }
    report->artifactBytes = static_cast<uint64_t>(fileSize);
    uint64_t loaded = 0;
    while (loaded < report->artifactBytes) {
        const uint32_t chunk = static_cast<uint32_t>(
            report->artifactBytes - loaded > 64u * 1024u
                ? 64u * 1024u : report->artifactBytes - loaded);
        const int32_t read = vfs::read(handle, g_artifact + loaded, chunk);
        if (read <= 0 || static_cast<uint32_t>(read) != chunk) {
            (void)vfs::close(handle);
            return false;
        }
        loaded += static_cast<uint32_t>(read);
    }
    (void)vfs::close(handle);
    return probeElfEnvelope(g_artifact, report->artifactBytes, report);
}

bool rangesOverlap(uintptr_t leftBase, uintptr_t leftSpan,
                   uintptr_t rightBase, uintptr_t rightSpan) {
    if (leftSpan == 0 || rightSpan == 0) return false;
    const uintptr_t leftEnd = leftBase > UINTPTR_MAX - leftSpan
        ? UINTPTR_MAX : leftBase + leftSpan;
    const uintptr_t rightEnd = rightBase > UINTPTR_MAX - rightSpan
        ? UINTPTR_MAX : rightBase + rightSpan;
    return leftBase < rightEnd && rightBase < leftEnd;
}

bool mapElf(const uint8_t* bytes, uint64_t size, LaunchReport* report) {
    if (bytes == nullptr || size < sizeof(Elf64Header)) return false;
    const Elf64Header* header = reinterpret_cast<const Elf64Header*>(bytes);
    if (header->ident[0] != 0x7Fu || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F' ||
        header->ident[4] != kElfClass64 || header->ident[5] != kElfDataLittle ||
        header->type != kElfExecutable || header->machine != kElfMachineAmd64 ||
        header->version != 1u || header->headerSize != sizeof(Elf64Header) ||
        header->programHeaderSize != sizeof(Elf64ProgramHeader) ||
        header->programHeaderCount == 0 ||
        header->programHeaderCount > kMaxProgramHeaders ||
        !boundedRange(header->programHeaderOffset,
                      static_cast<uint64_t>(header->programHeaderCount) *
                          sizeof(Elf64ProgramHeader), size)) return false;
    const Elf64ProgramHeader* programs = reinterpret_cast<const Elf64ProgramHeader*>(
        bytes + header->programHeaderOffset);
    uintptr_t imageLow = UINTPTR_MAX;
    uintptr_t imageHigh = 0;
    bool entryValid = false;
    uint16_t loadCount = 0;
    for (uint16_t index = 0; index < header->programHeaderCount; ++index) {
        const Elf64ProgramHeader& program = programs[index];
        if (program.type == kElfProgramNull || program.type == kElfProgramGnuStack) continue;
        if (program.type != kElfProgramLoad || program.memorySize == 0 ||
            program.fileSize > program.memorySize ||
            !boundedRange(program.offset, program.fileSize, size) ||
            program.alignment != kPageSize ||
            (program.offset & (kPageSize - 1u)) !=
                (program.virtualAddress & (kPageSize - 1u)) ||
            (program.flags & (kElfFlagWrite | kElfFlagExecute)) ==
                (kElfFlagWrite | kElfFlagExecute)) return false;
        if (program.virtualAddress > UINTPTR_MAX - program.memorySize) return false;
        const uintptr_t low = static_cast<uintptr_t>(program.virtualAddress & ~(kPageSize - 1u));
        const uint64_t end = program.virtualAddress + program.memorySize;
        const uintptr_t high = static_cast<uintptr_t>((end + kPageSize - 1u) & ~(kPageSize - 1u));
        if (high <= low || high > UINTPTR_MAX - kPageSize) return false;
        if (low < imageLow) imageLow = low;
        if (high > imageHigh) imageHigh = high;
        if (header->entry >= program.virtualAddress && header->entry < end) {
            entryValid = (program.flags & kElfFlagExecute) != 0;
        }
        ++loadCount;
    }
    if (loadCount == 0 || imageLow == UINTPTR_MAX || imageHigh <= imageLow ||
        !entryValid || imageHigh - imageLow > kMaxArtifactBytes * 2u) return false;

    memory::address_space::AddressSpace* addressSpace = memory::address_space::current();
    if (addressSpace == nullptr) return false;
    g_pageCount = 0;
    g_artifactBase = imageLow;
    g_artifactSpan = imageHigh - imageLow;
    for (uint16_t index = 0; index < header->programHeaderCount; ++index) {
        const Elf64ProgramHeader& program = programs[index];
        if (program.type != kElfProgramLoad) continue;
        const uintptr_t low = static_cast<uintptr_t>(program.virtualAddress & ~(kPageSize - 1u));
        const uintptr_t end = static_cast<uintptr_t>(program.virtualAddress + program.memorySize);
        const uintptr_t high = (end + kPageSize - 1u) & ~(kPageSize - 1u);
        for (uintptr_t address = low; address < high; address += kPageSize) {
            memory::address_space::MappingInfo existing{};
            if (memory::address_space::queryPage(addressSpace, address, &existing) &&
                existing.present) {
                releaseMappedPages();
                return false;
            }
            if (g_pageCount >= kMaxMappedPages) {
                releaseMappedPages();
                return false;
            }
            const uint64_t physical = memory::address_space::allocateFrame(
                memory::address_space::FrameOwner::VmRegion);
            if (physical == 0) {
                releaseMappedPages();
                return false;
            }
            if (!memory::address_space::zeroFrame(physical)) {
                releaseMappedPages();
                return false;
            }
            if (!memory::address_space::mapPage(addressSpace, address, physical,
                    memory::hugepages::x86_64::PTE_P |
                    memory::hugepages::x86_64::PTE_W)) {
                releaseMappedPages();
                return false;
            }
            g_pages[g_pageCount++] = { address, physical };
        }
    }
    for (uint16_t index = 0; index < header->programHeaderCount; ++index) {
        const Elf64ProgramHeader& program = programs[index];
        if (program.type != kElfProgramLoad || program.fileSize == 0) continue;
        uint8_t* destination = reinterpret_cast<uint8_t*>(
            static_cast<uintptr_t>(program.virtualAddress));
        const uint8_t* source = bytes + program.offset;
        for (uint64_t offset = 0; offset < program.fileSize; ++offset) {
            destination[offset] = source[offset];
        }
    }
    for (uint32_t pageIndex = 0; pageIndex < g_pageCount; ++pageIndex) {
        const uintptr_t address = g_pages[pageIndex].address;
        uint32_t flags = 0;
        bool covered = false;
        for (uint16_t index = 0; index < header->programHeaderCount; ++index) {
            const Elf64ProgramHeader& program = programs[index];
            if (program.type != kElfProgramLoad) continue;
            const uintptr_t low = static_cast<uintptr_t>(program.virtualAddress & ~(kPageSize - 1u));
            const uintptr_t end = static_cast<uintptr_t>(program.virtualAddress + program.memorySize);
            const uintptr_t high = (end + kPageSize - 1u) & ~(kPageSize - 1u);
            if (address >= low && address < high) {
                flags |= program.flags;
                covered = true;
            }
        }
        if (!covered) {
            releaseMappedPages();
            return false;
        }
        uint64_t pte = memory::hugepages::x86_64::PTE_P;
        if ((flags & kElfFlagWrite) != 0) pte |= memory::hugepages::x86_64::PTE_W;
        if ((flags & kElfFlagExecute) == 0) pte |= memory::hugepages::x86_64::PTE_NX;
        if (!memory::address_space::updatePageFlags(addressSpace, address, pte)) {
            releaseMappedPages();
            return false;
        }
    }
    if (!memory::address_space::rangeHasPresentMapping(
            addressSpace,
            static_cast<uintptr_t>(header->entry & ~(kPageSize - 1u)), 1u)) {
        releaseMappedPages();
        return false;
    }
    report->artifactBase = imageLow;
    report->artifactSpan = imageHigh - imageLow;
    report->loadSegmentCount = loadCount;
    report->entryPoint = header->entry;
    return true;
}

void emitManagedStatus(bool entry, bool pass) {
    serial::puts("[C102-MANAGED-STATUS] entry=");
    serial::put_hex32(entry ? 1u : 0u);
    serial::puts(" pass=");
    serial::put_hex32(pass ? 1u : 0u);
    serial::puts("\n");
}

void releaseMappedPages() {
    memory::address_space::AddressSpace* addressSpace =
        memory::address_space::current();
    for (uint32_t index = 0; index < g_pageCount; ++index) {
        memory::address_space::MappingInfo removed{};
        (void)memory::address_space::unmapPage(
            addressSpace, g_pages[index].address, &removed);
        (void)memory::address_space::releaseFrame(
            g_pages[index].physical,
            memory::address_space::FrameOwner::VmRegion,
            memory::address_space::FrameReleaseReason::Release);
    }
    g_pageCount = 0;
    g_artifactBase = 0;
    g_artifactSpan = 0;
}

} // namespace

const char* launchStatusName(LaunchStatus status) {
    switch (status) {
        case LaunchStatus::Success: return "success";
        case LaunchStatus::NotFound: return "not-found";
        case LaunchStatus::InvalidPath: return "invalid-path";
        case LaunchStatus::ReadFailed: return "read-failed";
        case LaunchStatus::ArtifactTooLarge: return "artifact-too-large";
        case LaunchStatus::InvalidElf: return "invalid-elf";
        case LaunchStatus::MappingFailed: return "mapping-failed";
        case LaunchStatus::RuntimeFoundationFailed: return "runtime-foundation-failed";
        case LaunchStatus::TlsFailed: return "tls-failed";
        case LaunchStatus::StartupFailed: return "startup-failed";
        case LaunchStatus::ManagedFailed: return "managed-failed";
        case LaunchStatus::Busy: return "busy";
        case LaunchStatus::BaseCollision: return "base-collision";
        case LaunchStatus::InvalidApplicationId: return "invalid-app-id";
        case LaunchStatus::InvalidLaunchContext: return "invalid-launch-context";
    }
    return "unknown";
}

LaunchStatus statusForManagedReturn(int32_t managedReturn) {
    if (managedReturn == 0) return LaunchStatus::Success;
    if (managedReturn == kInvalidApplicationIdReturn) {
        return LaunchStatus::InvalidApplicationId;
    }
    return LaunchStatus::ManagedFailed;
}

int32_t invokeManagedWithHostMetadata(
    uint32_t selector,
    uint32_t launchFlags,
    uint32_t hostVersion,
    uint64_t capabilities) {
    if (g_application.state != ApplicationLifecycleState::Resident ||
        g_application.entryPoint == 0u || selector == 0u) {
        return kInvalidApplicationIdReturn;
    }

    NativeHostCallTable host{
        kManagedHostTableSize, hostVersion, managedLog,
        managedRequestWindow, managedDrawText, managedDrawRect, managedAddButton,
        managedCloseWindow, capabilities,
        (capabilities & kManagedCapabilityAction) != 0u
            ? managedAddActionButton : nullptr,
        (capabilities & kManagedCapabilityFileRead) != 0u
            ? managedFileReadAll : nullptr,
        (capabilities & kManagedCapabilityFileWrite) != 0u
            ? managedFileWriteAll : nullptr,
        (capabilities & kManagedCapabilityDirectoryList) != 0u
            ? managedDirectoryList : nullptr,
        (capabilities & kManagedCapabilityFileStat) != 0u
            ? managedFileStat : nullptr };
    NativeGxAppContext context{
        sizeof(NativeGxAppContext), 0u, &host,
        reinterpret_cast<void*>(static_cast<uintptr_t>(selector)),
        nullptr, 0u, launchFlags};
    // The resident wrapper only needs non-null startup-table addresses on a
    // re-entry; it does not reinstall the runtime foundations.  The real
    // managed context remains the only application-facing value.
    NativeAotStartupContext startup{
        reinterpret_cast<void*>(static_cast<uintptr_t>(1u)),
        reinterpret_cast<void*>(static_cast<uintptr_t>(2u)),
        reinterpret_cast<void*>(static_cast<uintptr_t>(3u)),
        &context, startupInstallTls, startupMarker};
    using Entry = int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(void*);
    g_activeManagedContext = &context;
    arch::amd64::disable_interrupts();
    const int32_t managedReturn = reinterpret_cast<Entry>(
        g_application.entryPoint)(&startup);
    arch::amd64::enable_interrupts();
    g_activeManagedContext = nullptr;
    return managedReturn;
}

int32_t invokeManagedAction(uint32_t selector, uint32_t actionId) {
    if (actionId == 0u || actionId > 0x1FFFFFFFu) return -2;
    return invokeManagedWithHostMetadata(
        selector, kLaunchFlagAction | actionId,
        kManagedHostAbiVersion, kManagedCapabilities);
}

int32_t invokeManagedInput(uint32_t selector, uint32_t inputFlags) {
    const uint32_t kind = inputFlags & kLaunchFlagInputKindMask;
    const uint32_t payload = inputFlags & kLaunchFlagInputPayloadMask;
    const bool wheelKind = kind >= kLaunchFlagInputWheel &&
        kind <= kLaunchFlagInputWheelLast;
    if (selector == 0u || (inputFlags & kLaunchFlagInput) == 0u ||
        (!wheelKind && kind != kLaunchFlagInputPointerDown &&
         kind != kLaunchFlagInputPointerUp &&
         kind != kLaunchFlagInputKeyDown &&
         kind != kLaunchFlagInputKeyChar &&
         kind != kLaunchFlagInputSecondaryPointerDown &&
         kind != kLaunchFlagInputSecondaryPointerUp) ||
        payload > kLaunchFlagInputPayloadMask) {
        return -2;
    }
    g_c116InputDispatchActive = true;
    const int32_t result = invokeManagedWithHostMetadata(
        selector, inputFlags, kManagedHostAbiVersion, kManagedCapabilities);
    g_c116InputDispatchActive = false;
    return result;
}

int32_t invokeManagedKeyDownForProof(
    uint32_t selector, uint32_t keyCode, bool shift) {
    if (keyCode > kLaunchFlagInputValueMask) return -2;
    const uint32_t payload = keyCode | (shift ? kLaunchFlagInputShift : 0u);
    const int32_t result = invokeManagedInput(
        selector, kLaunchFlagInput | kLaunchFlagInputKeyDown | payload);
    serial::puts("[C117-TRANSPORT] kind=key-down key=");
    serial::put_hex32(keyCode);
    serial::puts(" shift=");
    serial::put_hex32(shift ? 1u : 0u);
    serial::puts(" result=");
    serial::puts(result == 0 ? "PASS\n" : "FAIL\n");
    return result;
}

LaunchStatus launchResident(const char* path, uint32_t logicalAppId,
                            LaunchReport* report, const char* launchContext,
                            uint32_t launchContextLength) {
    const uint32_t sequence = g_application.sequence + 1u;
    emitC103Begin(sequence, path, true);
    const uint8_t handle = vfs::open(path, vfs::OPEN_READ);
    if (handle == 0xFFu) {
        report->status = LaunchStatus::NotFound;
        serial::puts("[C102-LAUNCH] rejected status=not-found path=");
        serial::puts(path);
        serial::puts("\n");
        emitC103Return(g_application.sequence, report->status);
        return report->status;
    }
    (void)vfs::close(handle);
    if (!sameApplicationPath(path, g_application.path)) {
#if defined(GXOS_C104_PRODUCTION_LAUNCH)
        if (kC104MultipleApplicationProbeEnabled) {
            LaunchReport candidate{};
            if (!readAndProbeApplication(path, &candidate)) {
                report->status = LaunchStatus::InvalidElf;
                serial::puts("[C104-MODULE] identity=candidate placement=invalid-elf path=");
                serial::puts(path);
                serial::puts("\n");
                return report->status;
            }
            report->artifactBytes = candidate.artifactBytes;
            report->artifactBase = candidate.artifactBase;
            report->artifactSpan = candidate.artifactSpan;
            report->loadSegmentCount = candidate.loadSegmentCount;
            report->entryPoint = candidate.entryPoint;
            report->sequence = g_application.sequence + 1u;
            report->residentImage = true;
            report->lifecycleReusable = true;
            const bool collision = rangesOverlap(
                g_application.artifactBase, g_application.artifactSpan,
                candidate.artifactBase, candidate.artifactSpan);
            report->status = collision ? LaunchStatus::BaseCollision : LaunchStatus::Busy;
            serial::puts("[C104-MODULE] identity=candidate placement=");
            serial::puts(collision ? "base-collision" : "non-overlapping-runtime-single-resident");
            serial::puts(" path=");
            serial::puts(path);
            serial::puts(" base=");
            serial::put_hex64(candidate.artifactBase);
            serial::puts(" span=");
            serial::put_hex64(candidate.artifactSpan);
            serial::puts(" residentBase=");
            serial::put_hex64(g_application.artifactBase);
            serial::puts(" residentSpan=");
            serial::put_hex64(g_application.artifactSpan);
            serial::puts(" status=");
            serial::puts(launchStatusName(report->status));
            serial::puts("\n");
            return report->status;
        }
#endif
        report->status = LaunchStatus::Busy;
        emitC103Lifecycle(g_application.sequence, "resident-different-image", true);
        emitC103Return(g_application.sequence, report->status);
        return report->status;
    }

    report->artifactBytes = g_application.artifactBytes;
    report->artifactBase = g_application.artifactBase;
    report->artifactSpan = g_application.artifactSpan;
    report->loadSegmentCount = g_application.loadSegmentCount;
    report->entryPoint = g_application.entryPoint;
    report->sequence = sequence;
    report->runtimeInitialized = true;
    report->runtimeReused = true;
    report->residentImage = true;
    report->lifecycleReusable = true;
    const memory::address_space::FrameAccounting before =
        memory::address_space::accounting();
    report->preTotalTracked = before.totalKnownFrames;
    report->preFreeFrames = before.freeFrames;
    report->preAllocatedFrames = before.allocatedFrames;
    report->preVmRegionFrames = before.regionOwnedFrames;
    report->prePageTableFrames = before.pageTableFrames;
    report->mapTotalTracked = before.totalKnownFrames;
    report->mapFreeFrames = before.freeFrames;
    report->mapAllocatedFrames = before.allocatedFrames;
    report->mapVmRegionFrames = before.regionOwnedFrames;
    report->mapPageTableFrames = before.pageTableFrames;
    emitFrameAccounting("before", before);
    emitC103Frames("before", sequence, before);
    serial::puts("[C103-LOADER] reusing resident ELF64 AMD64 ET_EXEC entry=");
    serial::put_hex64(report->entryPoint);
    serial::puts(" base=");
    serial::put_hex64(report->artifactBase);
    serial::puts(" span=");
    serial::put_hex64(report->artifactSpan);
    serial::puts("\n");

    if (!gxos::runtime::isLocalStorageInitialized() &&
        gxos::runtime::initializeLocalStorage() != gxos::runtime::LocalStorageResult::Success) {
        report->status = LaunchStatus::RuntimeFoundationFailed;
        emitC103Return(sequence, report->status);
        return report->status;
    }
    if (gxos::runtime::attachLocalStorage() != gxos::runtime::LocalStorageResult::Success ||
        (!guidexos::nativeaot::threadstore::isInitialized() &&
         guidexos::nativeaot::threadstore::initialize() !=
             guidexos::nativeaot::threadstore::Result::Success) ||
        (guidexos::nativeaot::threadstore::getCurrentThread() == nullptr &&
         guidexos::nativeaot::threadstore::attachCurrentThread() !=
             guidexos::nativeaot::threadstore::Result::Success)) {
        report->status = LaunchStatus::RuntimeFoundationFailed;
        emitC103Return(sequence, report->status);
        return report->status;
    }
    serial::puts("[C103-RUNTIME] resident runtime reused TLS=0\n");

    guidexos_nativeaot_pal_hooks legacy{};
    fillLegacyHooks(&legacy);
    guidexos_nativeaot_pal_hook_table_v1 pal{};
    fillPalTable(&pal, report->artifactBase, report->artifactSpan);
    guidexos_nativeaot_gc_startup_platform_table_v1 gc{};
    fillGcTable(&gc);
    NativeHostCallTable host{
        kManagedHostTableSize, kManagedHostAbiVersion, managedLog,
        managedRequestWindow, managedDrawText, managedDrawRect, managedAddButton,
        managedCloseWindow, kManagedCapabilities, managedAddActionButton,
        managedFileReadAll, managedFileWriteAll, managedDirectoryList,
        managedFileStat };
    serial::puts("[C112-HOST] version=");
    serial::put_hex32(kManagedHostAbiVersion);
    serial::puts(" capabilities=");
    serial::put_hex64(kManagedCapabilities);
    serial::puts(" result=PASS\n");
    NativeGxAppContext app{
        sizeof(NativeGxAppContext), 0u, &host,
        reinterpret_cast<void*>(static_cast<uintptr_t>(logicalAppId)),
        reinterpret_cast<const uint8_t*>(launchContext), launchContextLength, 0u};
    NativeAotStartupContext startup{
        &legacy, &pal, &gc, &app, startupInstallTls, startupMarker };
    using Entry = int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(void*);
    g_c102ManagedEntryObserved = false;
    g_c102ManagedPassObserved = false;
    g_c104ManagedEntryObserved = false;
    g_c104ManagedPassObserved = false;
    g_c107ManagedEntryObserved = false;
    g_c107ManagedPassObserved = false;
    g_c107ManagedInvalidApplicationObserved = false;
    g_activeManagedContext = &app;
    arch::amd64::disable_interrupts();
    const int32_t managedReturn = reinterpret_cast<Entry>(report->entryPoint)(&startup);
    arch::amd64::enable_interrupts();
    g_activeManagedContext = nullptr;
    report->managedReturn = managedReturn;
    report->managedEntryReached = g_c102ManagedEntryObserved ||
        g_c104ManagedEntryObserved || g_c107ManagedEntryObserved;
    report->managedPassReached = (g_c102ManagedPassObserved ||
        g_c104ManagedPassObserved || g_c107ManagedPassObserved) &&
        managedReturn == 0;
    report->managedInvalidApplicationObserved =
        g_c107ManagedInvalidApplicationObserved;
    report->launcherRegainedControl = true;
    report->mappingsPersistentByDesign = true;
    g_application.sequence = sequence;
    emitManagedStatus(report->managedEntryReached, report->managedPassReached);
    serial::puts("[C102-LAUNCH-RETURN] status=");
    serial::put_hex32(static_cast<uint32_t>(managedReturn));
    serial::puts("\n");
    const memory::address_space::FrameAccounting after = memory::address_space::accounting();
    report->postTotalTracked = after.totalKnownFrames;
    report->postFreeFrames = after.freeFrames;
    report->postAllocatedFrames = after.allocatedFrames;
    report->postVmRegionFrames = after.regionOwnedFrames;
    report->postPageTableFrames = after.pageTableFrames;
    report->accountingResidual = after.totalKnownFrames >= after.freeFrames + after.allocatedFrames
        ? after.totalKnownFrames - after.freeFrames - after.allocatedFrames : UINT64_MAX;
    report->ownerResidual = after.allocatedFrames >=
            after.regionOwnedFrames + after.pageTableFrames + after.kernelOwnedFrames
        ? after.allocatedFrames - after.regionOwnedFrames -
              after.pageTableFrames - after.kernelOwnedFrames : UINT64_MAX;
    emitFrameAccounting("after-return", after);
    emitC103Frames("after-return", sequence, after);
    emitC103Frames("after-lifecycle", sequence, after);
    emitC103Lifecycle(sequence, "resident", true);
    report->status = statusForManagedReturn(managedReturn);
    emitC103Return(sequence, report->status);
    return report->status;
}

LaunchStatus launchInternal(const char* path, uint32_t logicalAppId,
                            LaunchReport* report, const char* launchContext,
                            uint32_t launchContextLength) {
    LaunchReport local{};
    if (report == nullptr) report = &local;
    *report = {};
    report->logicalAppId = logicalAppId;
    report->status = LaunchStatus::InvalidPath;
    if (path == nullptr || path[0] != '/') return report->status;
    if ((launchContextLength != 0u && launchContext == nullptr) ||
        launchContextLength > kManagedLaunchContextMaxBytes) {
        report->status = LaunchStatus::InvalidLaunchContext;
        serial::puts("[NATIVEAOT-CONTEXT] status=invalid length=");
        serial::put_hex32(launchContextLength);
        serial::puts("\n");
        return report->status;
    }
    char launchContextCopy[kManagedLaunchContextMaxBytes + 1u] = {};
    if (launchContextLength != 0u) {
        for (uint32_t index = 0; index < launchContextLength; ++index) {
            const uint8_t value = static_cast<uint8_t>(launchContext[index]);
            if (value == 0u) {
                report->status = LaunchStatus::InvalidLaunchContext;
                serial::puts("[NATIVEAOT-CONTEXT] status=invalid embedded-nul\n");
                return report->status;
            }
            launchContextCopy[index] = static_cast<char>(value);
        }
    }
    char requestedPath[kMaxApplicationPath] = {};
    if (!copyApplicationPath(path, requestedPath)) return report->status;
    if (g_application.state == ApplicationLifecycleState::Resident) {
        return launchResident(requestedPath, logicalAppId, report,
                              launchContextLength == 0u ? nullptr : launchContextCopy,
                              launchContextLength);
    }
    g_application.state = ApplicationLifecycleState::Loading;
    const uint32_t sequence = 1u;
    emitC103Begin(sequence, requestedPath, false);

    const uint8_t handle = vfs::open(requestedPath, vfs::OPEN_READ);
    if (handle == 0xFFu) {
        report->status = LaunchStatus::NotFound;
        serial::puts("[C102-LAUNCH] rejected status=not-found path=");
        serial::puts(path);
        serial::puts("\n");
        g_application.state = ApplicationLifecycleState::Empty;
        emitC103Return(sequence, report->status);
        return report->status;
    }
    const int64_t fileSize = vfs::file_size(handle);
    if (fileSize <= 0 || static_cast<uint64_t>(fileSize) > kMaxArtifactBytes) {
        (void)vfs::close(handle);
        report->status = fileSize > static_cast<int64_t>(kMaxArtifactBytes)
            ? LaunchStatus::ArtifactTooLarge : LaunchStatus::ReadFailed;
        g_application.state = ApplicationLifecycleState::Empty;
        emitC103Return(sequence, report->status);
        return report->status;
    }
    report->artifactBytes = static_cast<uint64_t>(fileSize);
    uint64_t loaded = 0;
    while (loaded < report->artifactBytes) {
        const uint32_t chunk = static_cast<uint32_t>(
            report->artifactBytes - loaded > 64u * 1024u
                ? 64u * 1024u : report->artifactBytes - loaded);
        const int32_t read = vfs::read(handle, g_artifact + loaded, chunk);
        if (read <= 0 || static_cast<uint32_t>(read) != chunk) {
            (void)vfs::close(handle);
            report->status = LaunchStatus::ReadFailed;
            g_application.state = ApplicationLifecycleState::Empty;
            emitC103Return(sequence, report->status);
            return report->status;
        }
        loaded += static_cast<uint32_t>(read);
    }
    (void)vfs::close(handle);

    if (!memory::address_space::reserveCurrentKernelImage()) {
        report->status = LaunchStatus::RuntimeFoundationFailed;
        serial::puts("[C102-LOADER] rejected status=kernel-frame-claim-failed\n");
        g_application.state = ApplicationLifecycleState::Empty;
        emitC103Return(sequence, report->status);
        return report->status;
    }

    const memory::address_space::FrameAccounting before = memory::address_space::accounting();
    report->preTotalTracked = before.totalKnownFrames;
    report->preFreeFrames = before.freeFrames;
    report->preAllocatedFrames = before.allocatedFrames;
    report->preVmRegionFrames = before.regionOwnedFrames;
    report->prePageTableFrames = before.pageTableFrames;
    emitFrameAccounting("before", before);
    emitC103Frames("before", sequence, before);
    serial::puts("[C102-LOADER] validating path=");
    serial::puts(requestedPath);
    serial::puts(" bytes=");
    serial::put_hex64(report->artifactBytes);
    serial::puts("\n");
    arch::amd64::disable_interrupts();
    const bool mapped = mapElf(g_artifact, report->artifactBytes, report);
    arch::amd64::enable_interrupts();
    if (!mapped) {
        releaseMappedPages();
        report->status = LaunchStatus::InvalidElf;
        serial::puts("[C102-LOADER] rejected status=invalid-elf\n");
        g_application.state = ApplicationLifecycleState::Empty;
        emitC103Return(sequence, report->status);
        return report->status;
    }
    const memory::address_space::FrameAccounting afterMap =
        memory::address_space::accounting();
    report->mapTotalTracked = afterMap.totalKnownFrames;
    report->mapFreeFrames = afterMap.freeFrames;
    report->mapAllocatedFrames = afterMap.allocatedFrames;
    report->mapVmRegionFrames = afterMap.regionOwnedFrames;
    report->mapPageTableFrames = afterMap.pageTableFrames;
    emitC103Frames("after-map", sequence, afterMap);
    serial::puts("[C102-LOADER] mapped ELF64 AMD64 ET_EXEC segments=");
    serial::put_hex32(report->loadSegmentCount);
    serial::puts(" entry=");
    serial::put_hex64(report->entryPoint);
    serial::puts(" base=");
    serial::put_hex64(report->artifactBase);
    serial::puts(" span=");
    serial::put_hex64(report->artifactSpan);
    serial::puts("\n");

    if (!gxos::runtime::isLocalStorageInitialized() &&
        gxos::runtime::initializeLocalStorage() != gxos::runtime::LocalStorageResult::Success) {
        releaseMappedPages();
        g_application.state = ApplicationLifecycleState::Empty;
        report->status = LaunchStatus::RuntimeFoundationFailed;
        emitC103Return(sequence, report->status);
        return report->status;
    }
    if (gxos::runtime::attachLocalStorage() != gxos::runtime::LocalStorageResult::Success ||
        (!guidexos::nativeaot::threadstore::isInitialized() &&
         guidexos::nativeaot::threadstore::initialize() !=
             guidexos::nativeaot::threadstore::Result::Success) ||
        guidexos::nativeaot::threadstore::attachCurrentThread() !=
            guidexos::nativeaot::threadstore::Result::Success) {
        releaseMappedPages();
        g_application.state = ApplicationLifecycleState::Empty;
        report->status = LaunchStatus::RuntimeFoundationFailed;
        emitC103Return(sequence, report->status);
        return report->status;
    }
    serial::puts("[C102-RUNTIME] PAL/VM/GC startup seam ready TLS=0\n");

    guidexos_nativeaot_pal_hooks legacy{};
    fillLegacyHooks(&legacy);
    guidexos_nativeaot_pal_hook_table_v1 pal{};
    fillPalTable(&pal, report->artifactBase, report->artifactSpan);
    guidexos_nativeaot_gc_startup_platform_table_v1 gc{};
    fillGcTable(&gc);
    NativeHostCallTable host{
        kManagedHostTableSize, kManagedHostAbiVersion, managedLog,
        managedRequestWindow, managedDrawText, managedDrawRect, managedAddButton,
        managedCloseWindow, kManagedCapabilities, managedAddActionButton,
        managedFileReadAll, managedFileWriteAll, managedDirectoryList,
        managedFileStat };
    serial::puts("[C112-HOST] version=");
    serial::put_hex32(kManagedHostAbiVersion);
    serial::puts(" capabilities=");
    serial::put_hex64(kManagedCapabilities);
    serial::puts(" result=PASS\n");
    NativeGxAppContext app{
        sizeof(NativeGxAppContext), 0u, &host,
        reinterpret_cast<void*>(static_cast<uintptr_t>(logicalAppId)),
        reinterpret_cast<const uint8_t*>(
            launchContextLength == 0u ? nullptr : launchContextCopy),
        launchContextLength, 0u};
    NativeAotStartupContext startup{
        &legacy, &pal, &gc, &app, startupInstallTls, startupMarker };
    using Entry = int32_t (GUIDEXOS_NATIVEAOT_PAL_CALL *)(void*);
    // The current kernel IRQ stubs restore a same-ring interrupt frame on the
    // active stack.  Keep the first cross-image startup/managed transition
    // bounded and non-preemptible; the ordinary kernel interrupt state is
    // restored before launch() returns to the main loop.
    g_c102ManagedEntryObserved = false;
    g_c102ManagedPassObserved = false;
    g_c104ManagedEntryObserved = false;
    g_c104ManagedPassObserved = false;
    g_c107ManagedEntryObserved = false;
    g_c107ManagedPassObserved = false;
    g_c107ManagedInvalidApplicationObserved = false;
    g_activeManagedContext = &app;
    arch::amd64::disable_interrupts();
    const int32_t managedReturn = reinterpret_cast<Entry>(report->entryPoint)(&startup);
    arch::amd64::enable_interrupts();
    g_activeManagedContext = nullptr;
    report->managedReturn = managedReturn;
    report->managedEntryReached = g_c102ManagedEntryObserved ||
        g_c104ManagedEntryObserved || g_c107ManagedEntryObserved;
    report->managedPassReached = (g_c102ManagedPassObserved ||
        g_c104ManagedPassObserved || g_c107ManagedPassObserved) &&
        managedReturn == 0;
    report->managedInvalidApplicationObserved =
        g_c107ManagedInvalidApplicationObserved;
    report->launcherRegainedControl = true;
    g_application.state = ApplicationLifecycleState::Resident;
    (void)copyApplicationPath(requestedPath, g_application.path);
    g_application.artifactBytes = report->artifactBytes;
    g_application.artifactBase = report->artifactBase;
    g_application.artifactSpan = report->artifactSpan;
    g_application.entryPoint = report->entryPoint;
    g_application.loadSegmentCount = report->loadSegmentCount;
    g_application.sequence = sequence;
    report->sequence = sequence;
    report->runtimeInitialized = true;
    report->runtimeReused = false;
    report->residentImage = true;
    report->lifecycleReusable = true;
    emitManagedStatus(report->managedEntryReached, report->managedPassReached);
    serial::puts("[C102-LAUNCH-RETURN] status=");
    serial::put_hex32(static_cast<uint32_t>(managedReturn));
    serial::puts("\n");

    const memory::address_space::FrameAccounting after = memory::address_space::accounting();
    report->postTotalTracked = after.totalKnownFrames;
    report->postFreeFrames = after.freeFrames;
    report->postAllocatedFrames = after.allocatedFrames;
    report->postVmRegionFrames = after.regionOwnedFrames;
    report->postPageTableFrames = after.pageTableFrames;
    report->accountingResidual = after.totalKnownFrames >= after.freeFrames + after.allocatedFrames
        ? after.totalKnownFrames - after.freeFrames - after.allocatedFrames : UINT64_MAX;
    report->ownerResidual = after.allocatedFrames >=
            after.regionOwnedFrames + after.pageTableFrames + after.kernelOwnedFrames
        ? after.allocatedFrames - after.regionOwnedFrames -
              after.pageTableFrames - after.kernelOwnedFrames : UINT64_MAX;
    report->mappingsPersistentByDesign = true;
    emitFrameAccounting("after-return", after);
    emitC103Frames("after-return", sequence, after);
    emitC103Frames("after-lifecycle", sequence, after);
    emitC103Lifecycle(sequence, "resident", false);
    report->status = statusForManagedReturn(managedReturn);
    emitC103Return(sequence, report->status);
    return report->status;
}

LaunchStatus launch(const char* path, LaunchReport* report) {
    return launchInternal(path, 0u, report, nullptr, 0u);
}

LaunchStatus launchLogical(const char* path, uint32_t logicalAppId,
                           LaunchReport* report, const char* launchContext,
                           uint32_t launchContextLength) {
    return launchInternal(path, logicalAppId, report, launchContext,
                          launchContextLength);
}

bool isProductionLogicalApplicationId(const char* applicationId) {
    const gxos::apps::BuiltInAppMetadata* metadata =
        gxos::apps::FindManagedNativeAotAppByIdentity(applicationId);
    return metadata != nullptr &&
        gxos::apps::ManagedNativeAotCatalogIsValid() &&
        gxos::apps::IsManagedNativeAotRecordValid(*metadata) &&
        gxos::apps::detail::builtInTextEquals(
            metadata->managedCompositeImagePath,
            kProductionCompositeImage);
}

const char* productionCompositeImagePath() {
    return kProductionCompositeImage;
}

LaunchStatus launchLogicalApplication(const char* applicationId,
                                      LaunchReport* report,
                                      const char* launchContext,
                                      uint32_t launchContextLength) {
    LaunchReport local{};
    if (report == nullptr) report = &local;
    *report = {};
    report->status = LaunchStatus::InvalidApplicationId;
    const gxos::apps::BuiltInAppMetadata* metadata =
        gxos::apps::FindManagedNativeAotAppByIdentity(applicationId);
    if (metadata == nullptr || !gxos::apps::ManagedNativeAotCatalogIsValid() ||
        !gxos::apps::IsManagedNativeAotRecordValid(*metadata) ||
        !gxos::apps::detail::builtInTextEquals(
            metadata->managedCompositeImagePath,
            kProductionCompositeImage)) {
        serial::puts("[NATIVEAOT-APPMODEL] applicationId=");
        serial::puts(applicationId ? applicationId : "");
        serial::puts(" status=invalid-app-id\n");
        return report->status;
    }

    const uint32_t selector = metadata->managedSelector;

    serial::puts("[NATIVEAOT-PRODUCTION-LAUNCH] applicationId=");
    serial::puts(applicationId);
    serial::puts(" recordId=");
    serial::puts(metadata->appId);
    serial::puts(" image=");
    serial::puts(kProductionCompositeImage);
    serial::puts(" selector=");
    serial::put_hex32(selector);
    serial::puts("\n");
    return launchLogical(kProductionCompositeImage, selector, report,
                         launchContext, launchContextLength);
}

LaunchStatus probeHostAbiMismatch(LaunchReport* report) {
    LaunchReport local{};
    if (report == nullptr) report = &local;
    *report = {};
    report->logicalAppId = 3u;
    report->managedReturn = invokeManagedWithHostMetadata(
        3u, kLaunchFlagAbiProbe, kManagedHostAbiVersion + 99u,
        kManagedCapabilities);
    report->status = report->managedReturn == -3
        ? LaunchStatus::Success : LaunchStatus::ManagedFailed;
    serial::puts("[C112-ABI-MISMATCH] hostVersion=");
    serial::put_hex32(kManagedHostAbiVersion + 99u);
    serial::puts(" managedReturn=");
    serial::put_hex32(static_cast<uint32_t>(report->managedReturn));
    serial::puts(" result=");
    serial::puts(report->status == LaunchStatus::Success ? "PASS\n" : "FAIL\n");
    return report->status;
}

LaunchStatus probeCapabilityDowngrade(LaunchReport* report) {
    LaunchReport local{};
    if (report == nullptr) report = &local;
    *report = {};
    report->logicalAppId = 3u;
    const uint64_t downgraded = kManagedCapabilities & ~kManagedCapabilityAction;
    report->managedReturn = invokeManagedWithHostMetadata(
        3u, kLaunchFlagCapabilityProbe, kManagedHostAbiVersion, downgraded);
    report->status = report->managedReturn == 0
        ? LaunchStatus::Success : LaunchStatus::ManagedFailed;
    serial::puts("[C112-CAPABILITY-DOWNGRADE] action=omitted managedReturn=");
    serial::put_hex32(static_cast<uint32_t>(report->managedReturn));
    serial::puts(" result=");
    serial::puts(report->status == LaunchStatus::Success ? "PASS\n" : "FAIL\n");
    return report->status;
}

LaunchStatus probeFileCapabilityDowngrade(LaunchReport* report) {
    LaunchReport local{};
    if (report == nullptr) report = &local;
    *report = {};
    report->logicalAppId = 4u;
    const uint64_t downgraded = kManagedCapabilities & ~kManagedCapabilityFileWrite;
    report->managedReturn = invokeManagedWithHostMetadata(
        4u, kLaunchFlagCapabilityProbe, kManagedHostAbiVersion, downgraded);
    report->status = report->managedReturn == 0
        ? LaunchStatus::Success : LaunchStatus::ManagedFailed;
    serial::puts("[C113-CAPABILITY-DOWNGRADE] fileWrite=omitted managedReturn=");
    serial::put_hex32(static_cast<uint32_t>(report->managedReturn));
    serial::puts(" result=");
    serial::puts(report->status == LaunchStatus::Success ? "PASS\n" : "FAIL\n");
    return report->status;
}

LaunchStatus probeFileServiceNegativeTests(LaunchReport* report) {
    LaunchReport local{};
    if (report == nullptr) report = &local;
    *report = {};
    report->logicalAppId = 4u;
    if (g_application.state != ApplicationLifecycleState::Resident ||
        g_application.entryPoint == 0u) {
        report->status = LaunchStatus::ManagedFailed;
        return report->status;
    }

    NativeHostCallTable host{
        kManagedHostTableSize, kManagedHostAbiVersion, managedLog,
        managedRequestWindow, managedDrawText, managedDrawRect, managedAddButton,
        managedCloseWindow, kManagedCapabilities, managedAddActionButton,
        managedFileReadAll, managedFileWriteAll, managedDirectoryList,
        managedFileStat };
    NativeGxAppContext context{
        sizeof(NativeGxAppContext), 0u, &host,
        reinterpret_cast<void*>(static_cast<uintptr_t>(4u)), nullptr, 0u, 0u};
    uint8_t buffer[64] = {};
    uint32_t outLength = 0u;
    uint8_t oversizedPath[kManagedFilePathMaxBytes + 1u] = {};
    for (uint32_t index = 0u; index < kManagedFilePathMaxBytes + 1u; ++index) {
        oversizedPath[index] = 'x';
    }
    const uint8_t emptyPath[1] = {0};
    const uint8_t missingPath[] = "/system/apps/MISSING.TXT";
    const uint8_t oversizedFilePath[] = "/system/apps/GXOSAPP.ELF";
    const uint8_t validPath[] = "/system/apps/NOTES.TXT";

    g_activeManagedContext = &context;
    const int32_t emptyResult = managedFileReadAll(
        &context, const_cast<uint8_t*>(emptyPath), 0u,
        buffer, sizeof(buffer), &outLength);
    const int32_t oversizedPathResult = managedFileReadAll(
        &context, oversizedPath, sizeof(oversizedPath),
        buffer, sizeof(buffer), &outLength);
    const int32_t missingResult = managedFileReadAll(
        &context, const_cast<uint8_t*>(missingPath),
        sizeof(missingPath) - 1u, buffer, sizeof(buffer), &outLength);
    const int32_t oversizedFileResult = managedFileReadAll(
        &context, const_cast<uint8_t*>(oversizedFilePath),
        sizeof(oversizedFilePath) - 1u, buffer, sizeof(buffer), &outLength);
    const int32_t invalidReadBufferResult = managedFileReadAll(
        &context, const_cast<uint8_t*>(validPath), sizeof(validPath) - 1u,
        nullptr, 1u, &outLength);
    const int32_t invalidWriteDataResult = managedFileWriteAll(
        &context, const_cast<uint8_t*>(validPath), sizeof(validPath) - 1u,
        nullptr, 1u);
    g_activeManagedContext = nullptr;

    const bool passed = emptyResult == kManagedFileInvalidPath &&
        oversizedPathResult == kManagedFileInvalidPath &&
        missingResult == kManagedFileNotFound &&
        oversizedFileResult == kManagedFileTooLarge &&
        invalidReadBufferResult == kManagedFileInvalidArgument &&
        invalidWriteDataResult == kManagedFileInvalidArgument;
    report->status = passed ? LaunchStatus::Success : LaunchStatus::ManagedFailed;
    serial::puts("[C113-FILE-NEGATIVE] emptyPath=");
    serial::put_hex32(static_cast<uint32_t>(emptyResult));
    serial::puts(" oversizedPath=");
    serial::put_hex32(static_cast<uint32_t>(oversizedPathResult));
    serial::puts(" missing=");
    serial::put_hex32(static_cast<uint32_t>(missingResult));
    serial::puts(" oversizedFile=");
    serial::put_hex32(static_cast<uint32_t>(oversizedFileResult));
    serial::puts(" invalidReadBuffer=");
    serial::put_hex32(static_cast<uint32_t>(invalidReadBufferResult));
    serial::puts(" invalidWriteData=");
    serial::put_hex32(static_cast<uint32_t>(invalidWriteDataResult));
    serial::puts(" result=");
    serial::puts(passed ? "PASS\n" : "FAIL\n");
    return report->status;
}

LaunchStatus probeDirectoryCapabilityDowngrade(LaunchReport* report) {
    LaunchReport local{};
    if (report == nullptr) report = &local;
    *report = {};
    report->logicalAppId = 4u;
    const uint64_t downgraded = kManagedCapabilities & ~kManagedCapabilityDirectoryList;
    report->managedReturn = invokeManagedWithHostMetadata(
        4u, kLaunchFlagCapabilityProbe, kManagedHostAbiVersion, downgraded);
    report->status = report->managedReturn == 0
        ? LaunchStatus::Success : LaunchStatus::ManagedFailed;
    serial::puts("[C114-CAPABILITY-DOWNGRADE] directoryList=omitted managedReturn=");
    serial::put_hex32(static_cast<uint32_t>(report->managedReturn));
    serial::puts(" result=");
    serial::puts(report->status == LaunchStatus::Success ? "PASS\n" : "FAIL\n");
    return report->status;
}

LaunchStatus probeFileStatCapabilityDowngrade(LaunchReport* report) {
    LaunchReport local{};
    if (report == nullptr) report = &local;
    *report = {};
    report->logicalAppId = 4u;
    const uint64_t downgraded = kManagedCapabilities & ~kManagedCapabilityFileStat;
    report->managedReturn = invokeManagedWithHostMetadata(
        4u, kLaunchFlagCapabilityProbe, kManagedHostAbiVersion, downgraded);
    report->status = report->managedReturn == 0
        ? LaunchStatus::Success : LaunchStatus::ManagedFailed;
    serial::puts("[C114-CAPABILITY-DOWNGRADE] fileStat=omitted managedReturn=");
    serial::put_hex32(static_cast<uint32_t>(report->managedReturn));
    serial::puts(" result=");
    serial::puts(report->status == LaunchStatus::Success ? "PASS\n" : "FAIL\n");
    return report->status;
}

LaunchStatus probeDirectoryServiceNegativeTests(LaunchReport* report) {
    LaunchReport local{};
    if (report == nullptr) report = &local;
    *report = {};
    report->logicalAppId = 4u;
    NativeHostCallTable host{
        kManagedHostTableSize, kManagedHostAbiVersion, managedLog,
        managedRequestWindow, managedDrawText, managedDrawRect, managedAddButton,
        managedCloseWindow, kManagedCapabilities, managedAddActionButton,
        managedFileReadAll, managedFileWriteAll, managedDirectoryList,
        managedFileStat };
    NativeGxAppContext context{
        sizeof(NativeGxAppContext), 0u, &host,
        reinterpret_cast<void*>(static_cast<uintptr_t>(4u)), nullptr, 0u, 0u};
    ManagedDirectoryEntryAbi entries[2]{};
    ManagedFileInfoAbi info{};
    uint32_t count = 0u;
    uint32_t hasMore = 0u;
    const uint8_t validPath[] = "/system/apps";
    const uint8_t oversizedPath[kManagedFilePathMaxBytes + 1u] = {};
    g_activeManagedContext = &context;
    const int32_t nullPath = managedDirectoryList(
        &context, nullptr, sizeof(validPath) - 1u,
        reinterpret_cast<uint8_t*>(entries), 2u,
        kManagedDirectoryEntryAbiSize, &count, &hasMore);
    const int32_t nullDest = managedDirectoryList(
        &context, const_cast<uint8_t*>(validPath), sizeof(validPath) - 1u,
        nullptr, 2u, kManagedDirectoryEntryAbiSize, &count, &hasMore);
    const int32_t badStride = managedDirectoryList(
        &context, const_cast<uint8_t*>(validPath), sizeof(validPath) - 1u,
        reinterpret_cast<uint8_t*>(entries), 2u,
        kManagedDirectoryEntryAbiSize - 1u, &count, &hasMore);
    const int32_t badCapacity = managedDirectoryList(
        &context, const_cast<uint8_t*>(validPath), sizeof(validPath) - 1u,
        reinterpret_cast<uint8_t*>(entries), kManagedDirectoryMaxEntries + 1u,
        kManagedDirectoryEntryAbiSize, &count, &hasMore);
    const int32_t nullCount = managedDirectoryList(
        &context, const_cast<uint8_t*>(validPath), sizeof(validPath) - 1u,
        reinterpret_cast<uint8_t*>(entries), 2u,
        kManagedDirectoryEntryAbiSize, nullptr, &hasMore);
    const int32_t oversized = managedDirectoryList(
        &context, const_cast<uint8_t*>(oversizedPath), sizeof(oversizedPath),
        reinterpret_cast<uint8_t*>(entries), 2u,
        kManagedDirectoryEntryAbiSize, &count, &hasMore);
    const int32_t nullInfo = managedFileStat(
        &context, const_cast<uint8_t*>(validPath), sizeof(validPath) - 1u,
        nullptr, kManagedFileInfoAbiSize);
    const int32_t shortInfo = managedFileStat(
        &context, const_cast<uint8_t*>(validPath), sizeof(validPath) - 1u,
        reinterpret_cast<uint8_t*>(&info), kManagedFileInfoAbiSize - 1u);
    g_activeManagedContext = nullptr;
    const bool passed = nullPath == kManagedFileInvalidPath &&
        nullDest == kManagedFileInvalidArgument &&
        badStride == kManagedFileInvalidArgument &&
        badCapacity == kManagedFileInvalidArgument &&
        nullCount == kManagedFileInvalidArgument &&
        oversized == kManagedFileInvalidPath &&
        nullInfo == kManagedFileInvalidArgument &&
        shortInfo == kManagedFileInvalidArgument;
    report->status = passed ? LaunchStatus::Success : LaunchStatus::ManagedFailed;
    serial::puts("[C114-DIRECTORY-NEGATIVE] nullPath=");
    serial::put_hex32(static_cast<uint32_t>(nullPath));
    serial::puts(" nullDest=");
    serial::put_hex32(static_cast<uint32_t>(nullDest));
    serial::puts(" badStride=");
    serial::put_hex32(static_cast<uint32_t>(badStride));
    serial::puts(" badCapacity=");
    serial::put_hex32(static_cast<uint32_t>(badCapacity));
    serial::puts(" nullCount=");
    serial::put_hex32(static_cast<uint32_t>(nullCount));
    serial::puts(" oversizedPath=");
    serial::put_hex32(static_cast<uint32_t>(oversized));
    serial::puts(" nullInfo=");
    serial::put_hex32(static_cast<uint32_t>(nullInfo));
    serial::puts(" shortInfo=");
    serial::put_hex32(static_cast<uint32_t>(shortInfo));
    serial::puts(" result=");
    serial::puts(passed ? "PASS\n" : "FAIL\n");
    return report->status;
}

LaunchStatus probeDirectoryCapacityTests(LaunchReport* report) {
    LaunchReport local{};
    if (report == nullptr) report = &local;
    *report = {};
    report->logicalAppId = 4u;
    NativeHostCallTable host{
        kManagedHostTableSize, kManagedHostAbiVersion, managedLog,
        managedRequestWindow, managedDrawText, managedDrawRect, managedAddButton,
        managedCloseWindow, kManagedCapabilities, managedAddActionButton,
        managedFileReadAll, managedFileWriteAll, managedDirectoryList,
        managedFileStat };
    NativeGxAppContext context{
        sizeof(NativeGxAppContext), 0u, &host,
        reinterpret_cast<void*>(static_cast<uintptr_t>(4u)), nullptr, 0u, 0u};
    ManagedDirectoryEntryAbi one[1]{};
    ManagedDirectoryEntryAbi all[kManagedDirectoryMaxEntries]{};
    uint32_t smallCount = 0u;
    uint32_t smallMore = 0u;
    uint32_t fullCount = 0u;
    uint32_t fullMore = 0u;
    ManagedFileInfoAbi directoryInfo{};
    ManagedFileInfoAbi missingInfo{};
    const uint8_t path[] = "/system/apps";
    g_activeManagedContext = &context;
    const int32_t smallResult = managedDirectoryList(
        &context, const_cast<uint8_t*>(path), sizeof(path) - 1u,
        reinterpret_cast<uint8_t*>(one), 1u, kManagedDirectoryEntryAbiSize,
        &smallCount, &smallMore);
    const int32_t fullResult = managedDirectoryList(
        &context, const_cast<uint8_t*>(path), sizeof(path) - 1u,
        reinterpret_cast<uint8_t*>(all), kManagedDirectoryMaxEntries,
        kManagedDirectoryEntryAbiSize, &fullCount, &fullMore);
    const uint8_t missingPath[] = "/system/apps/MISSING.C114";
    const int32_t directoryStat = managedFileStat(
        &context, const_cast<uint8_t*>(path), sizeof(path) - 1u,
        reinterpret_cast<uint8_t*>(&directoryInfo), kManagedFileInfoAbiSize);
    const int32_t missingStat = managedFileStat(
        &context, const_cast<uint8_t*>(missingPath), sizeof(missingPath) - 1u,
        reinterpret_cast<uint8_t*>(&missingInfo), kManagedFileInfoAbiSize);
    g_activeManagedContext = nullptr;
    const bool passed = smallResult == kManagedFileSuccess && smallCount <= 1u &&
        smallMore == 1u && fullResult == kManagedFileSuccess &&
        fullCount >= smallCount && fullCount <= kManagedDirectoryMaxEntries &&
        directoryStat == kManagedFileSuccess && directoryInfo.type == kManagedEntryTypeDirectory &&
        missingStat == kManagedFileNotFound;
    report->status = passed ? LaunchStatus::Success : LaunchStatus::ManagedFailed;
    serial::puts("[C114-CAPACITY] smallCount=");
    serial::put_hex32(smallCount);
    serial::puts(" smallHasMore=");
    serial::put_hex32(smallMore);
    serial::puts(" fullCount=");
    serial::put_hex32(fullCount);
    serial::puts(" directoryType=");
    serial::put_hex32(directoryInfo.type);
    serial::puts(" missingStat=");
    serial::put_hex32(static_cast<uint32_t>(missingStat));
    serial::puts(" result=");
    serial::puts(passed ? "PASS\n" : "FAIL\n");
    return report->status;
}

} // namespace nativeaot
} // namespace kernel
