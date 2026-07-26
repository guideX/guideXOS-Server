#include "include/kernel/native_elf_baremetal.h"

#include "include/kernel/arch.h"
#include "include/kernel/desktop.h"
#include "include/kernel/framebuffer.h"
#include "include/kernel/input_manager.h"
#include "include/kernel/kernel_app.h"
#include "include/kernel/kernel_compositor.h"
#include "include/kernel/pit.h"
#include "include/kernel/ps2keyboard.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/vfs.h"

#include "sdk/include/guidexos/abi.h"
#include "sdk/include/guidexos/app.h"

namespace kernel {
namespace native_elf {

#if defined(ARCH_AMD64)

static const uint32_t kPageSize = 0x1000u;
static const uint32_t kMaxPackages = 8u;
static const uint32_t kMaxManifestBytes = 8192u;
static const uint32_t kReadChunkBytes = 65536u;
static const uint32_t kRuntimeStackBytes = 128u * 1024u;
static const uint32_t kMaxLoadedImageBytes = 32u * 1024u * 1024u;
static const uint32_t kMaxFrameBytes = 16u * 1024u * 1024u;

struct Package {
    bool valid;
    char directory[64];
    char root[128];
    char id[80];
    char displayName[80];
    char executable[160];
    char entryPoint[48];
    char abi[64];
    uint64_t executableBytes;
};

struct Runtime;

class NativeWindowOwner final : public app::KernelApp {
public:
    explicit NativeWindowOwner(Runtime* runtime)
        : m_runtime(runtime), m_pixels(nullptr), m_pixelBytes(0), m_width(0), m_height(0),
          m_strideBytes(0), m_closed(false) {
        m_name[0] = '\0';
    }

    ~NativeWindowOwner() override {
        release_pixels();
    }

    bool init() override { return true; }

    void shutdown() override {
        m_closed = true;
        release_pixels();
    }

    void draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) override {
        framebuffer::fill_rect(x, y, w, h, 0xFF000000u);
        if (!m_pixels || m_width == 0 || m_height == 0) return;

        const uint32_t drawX = w > m_width ? x + (w - m_width) / 2u : x;
        const uint32_t drawY = h > m_height ? y + (h - m_height) / 2u : y;
        const uint32_t visibleW = w < m_width ? w : m_width;
        const uint32_t visibleH = h < m_height ? h : m_height;
        uint32_t* output = framebuffer::get_back_buffer();
        const uint32_t pitchWords = framebuffer::get_pitch() / 4u;
        if (!output || pitchWords == 0) return;

        for (uint32_t row = 0; row < visibleH; ++row) {
            for (uint32_t col = 0; col < visibleW; ++col) {
                const uint32_t src = m_pixels[row * (m_strideBytes / 4u) + col];
                output[(drawY + row) * pitchWords + drawX + col] = 0xFF000000u | (src & 0x00FFFFFFu);
            }
        }
    }

    bool create(const char* title, int width, int height, uint32_t flags) {
        if (!title || width <= 0 || height <= 0 || m_window) return false;
        m_window = new app::KernelWindow();
        if (!m_window) return false;
        m_window->owner = this;
        m_window->w = width;
        m_window->h = height;
        m_window->flags = app::WF_VISIBLE | app::WF_TITLEBAR | app::WF_CLOSABLE;
        if (flags & GX_WINDOW_FLAG_RESIZABLE) m_window->flags |= app::WF_RESIZABLE;
        const uint32_t screenW = framebuffer::get_width();
        const uint32_t screenH = framebuffer::get_height();
        m_window->x = screenW > static_cast<uint32_t>(width) ?
            static_cast<int>((screenW - static_cast<uint32_t>(width)) / 2u) : 0;
        m_window->y = screenH > static_cast<uint32_t>(height) ?
            static_cast<int>((screenH - static_cast<uint32_t>(height)) / 2u) : 0;
        setTitle(title);
        if (!compositor::KernelCompositor::registerWindow(m_window)) {
            delete m_window;
            m_window = nullptr;
            return false;
        }
        m_state = app::AppState::Running;
        m_closed = false;
        desktop_request_redraw();
        return true;
    }

    void present(const void* pixels, uint32_t width, uint32_t height, uint32_t strideBytes) {
        release_pixels();
        const uint32_t bytes = strideBytes * height;
        m_pixels = new uint32_t[bytes / 4u];
        if (!m_pixels) return;
        const uint8_t* source = static_cast<const uint8_t*>(pixels);
        uint8_t* destination = reinterpret_cast<uint8_t*>(m_pixels);
        for (uint32_t i = 0; i < bytes; ++i) destination[i] = source[i];
        m_pixelBytes = bytes;
        m_width = width;
        m_height = height;
        m_strideBytes = strideBytes;
        invalidate();
    }

    void release_pixels() {
        if (m_pixels) delete[] m_pixels;
        m_pixels = nullptr;
        m_pixelBytes = 0;
        m_width = 0;
        m_height = 0;
        m_strideBytes = 0;
    }

    bool closed() const { return m_closed || m_window == nullptr; }
    app::KernelWindow* window() const { return m_window; }

private:
    Runtime* m_runtime;
    uint32_t* m_pixels;
    uint32_t m_pixelBytes;
    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_strideBytes;
    bool m_closed;
};

struct Runtime {
    Package* package;
    NativeWindowOwner* owner;
    gx_app_context context;
    gx_host_calls host;
    uint8_t* image;
    uint64_t imageBytes;
    gx_result (GX_CALL *entry)(gx_app_context*);
    uint8_t* stack;
    bool exitRequested;
    gx_result exitCode;
    bool lastFocus;
    bool focusKnown;
    uint32_t readLogCount;
    uint32_t tickLogCount;
};

static Package s_packages[kMaxPackages];
static uint32_t s_packageCount = 0;
static char s_manifest[kMaxManifestBytes];
static bool s_discovered = false;
static bool s_launchInProgress = false;
static uint64_t s_runtimeSequence = 0;

extern "C" gx_result gxos_native_call_on_stack(
    gx_result (GX_CALL *entry)(gx_app_context*), gx_app_context* context, uint8_t* stackTop);

static bool text_equal(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb + ('a' - 'A'));
        if (ca != cb) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static bool starts_with(const char* value, const char* prefix) {
    if (!value || !prefix) return false;
    while (*prefix) {
        if (*value++ != *prefix++) return false;
    }
    return true;
}

static uint32_t text_length(const char* value) {
    uint32_t length = 0;
    if (value) while (value[length]) ++length;
    return length;
}

static bool text_copy(char* destination, uint32_t capacity, const char* source) {
    if (!destination || capacity == 0 || !source) return false;
    uint32_t i = 0;
    while (source[i] && i + 1u < capacity) {
        destination[i] = source[i];
        ++i;
    }
    destination[i] = '\0';
    return source[i] == '\0';
}

static const char* find_text(const char* text, const char* needle) {
    if (!text || !needle || !needle[0]) return text;
    const uint32_t needleLength = text_length(needle);
    for (const char* current = text; *current; ++current) {
        uint32_t i = 0;
        while (i < needleLength && current[i] == needle[i]) ++i;
        if (i == needleLength) return current;
    }
    return nullptr;
}

static bool json_string(const char* json, const char* key, char* output, uint32_t capacity) {
    const char* cursor = json;
    while ((cursor = find_text(cursor, key)) != nullptr) {
        cursor += text_length(key);
        while (*cursor && *cursor != ':') ++cursor;
        if (!*cursor) return false;
        ++cursor;
        while (*cursor && *cursor != '"') ++cursor;
        if (!*cursor) return false;
        ++cursor;
        uint32_t length = 0;
        while (cursor[length] && cursor[length] != '"') ++length;
        if (length + 1u > capacity) return false;
        for (uint32_t i = 0; i < length; ++i) output[i] = cursor[i];
        output[length] = '\0';
        return true;
    }
    return false;
}

static void log_package_line(const char* prefix, const Package* package) {
    serial::puts("[NATIVE-ELF] ");
    serial::puts(prefix);
    if (package) {
        serial::puts(" package=");
        serial::puts(package->directory);
        serial::puts(" id=");
        serial::puts(package->id);
        serial::puts(" displayName=");
        serial::puts(package->displayName);
    }
    serial::putc('\n');
}

static void log_runtime(Runtime* runtime, const char* message) {
    serial::puts("[NATIVE-ELF] runtime=");
    serial::put_hex64(s_runtimeSequence);
    if (runtime && runtime->package) {
        serial::puts(" app=");
        serial::puts(runtime->package->displayName);
    }
    serial::puts(" ");
    serial::puts(message ? message : "");
    serial::putc('\n');
}

static void log_hex(const char* label, uint64_t value) {
    serial::puts(label);
    serial::put_hex64(value);
}

static bool package_path(const Package* package, const char* relative, char* output, uint32_t capacity) {
    if (!package || !relative || !relative[0] || relative[0] == '/') return false;
    const char* component = relative;
    while (*component) {
        const char* end = component;
        while (*end && *end != '/') ++end;
        if ((end - component) == 2 && component[0] == '.' && component[1] == '.') return false;
        component = *end ? end + 1 : end;
    }
    const uint32_t rootLength = text_length(package->root);
    const uint32_t relativeLength = text_length(relative);
    if (rootLength + 1u + relativeLength + 1u > capacity) return false;
    for (uint32_t i = 0; i < rootLength; ++i) output[i] = package->root[i];
    output[rootLength] = '/';
    for (uint32_t i = 0; i < relativeLength; ++i) {
        if (relative[i] == '\\') return false;
        output[rootLength + 1u + i] = relative[i];
    }
    output[rootLength + 1u + relativeLength] = '\0';
    return true;
}

static bool read_at(const char* path, uint64_t offset, void* buffer, uint32_t length, uint32_t* outRead) {
    if (outRead) *outRead = 0;
    if (!path || !buffer || length == 0) return false;
    const uint8_t handle = vfs::open(path, vfs::OPEN_READ);
    if (handle == 0xFF) return false;
    bool okay = vfs::seek(handle, static_cast<int64_t>(offset), vfs::SEEK_SET) == vfs::VFS_OK;
    uint32_t total = 0;
    while (okay && total < length) {
        const int32_t amount = vfs::read(handle, static_cast<uint8_t*>(buffer) + total, length - total);
        if (amount <= 0) break;
        total += static_cast<uint32_t>(amount);
    }
    vfs::close(handle);
    if (outRead) *outRead = total;
    return okay && total == length;
}

static bool read_file_exact(const char* path, void* buffer, uint32_t length) {
    uint32_t amount = 0;
    return read_at(path, 0, buffer, length, &amount) && amount == length;
}

static bool parse_package(const char* directory, Package* package) {
    if (!directory || !package) return false;
    for (uint32_t i = 0; i < sizeof(Package); ++i) reinterpret_cast<uint8_t*>(package)[i] = 0;
    package->valid = false;
    if (!text_copy(package->directory, sizeof(package->directory), directory)) return false;
    package->root[0] = '/';
    package->root[1] = 'A'; package->root[2] = 'p'; package->root[3] = 'p'; package->root[4] = 's';
    package->root[5] = '/';
    if (!text_copy(package->root + 6, sizeof(package->root) - 6u, directory)) return false;

    char manifestPath[256];
    if (!package_path(package, "app.json", manifestPath, sizeof(manifestPath))) return false;
    const int32_t count = vfs::read_file(manifestPath, s_manifest, kMaxManifestBytes - 1u);
    if (count <= 0) return false;
    s_manifest[count] = '\0';
    char kind[32];
    char runtime[32];
    char architecture[32];
    if (!json_string(s_manifest, "\"kind\"", kind, sizeof(kind)) ||
        !text_equal(kind, "NativeElf") ||
        !json_string(s_manifest, "\"runtime\"", runtime, sizeof(runtime)) ||
        !text_equal(runtime, "native-elf")) return false;
    if (!json_string(s_manifest, "\"id\"", package->id, sizeof(package->id)) ||
        !json_string(s_manifest, "\"displayName\"", package->displayName, sizeof(package->displayName)) ||
        !json_string(s_manifest, "\"architecture\"", architecture, sizeof(architecture)) ||
        !text_equal(architecture, "amd64") ||
        !json_string(s_manifest, "\"path\"", package->executable, sizeof(package->executable)) ||
        !json_string(s_manifest, "\"entryPoint\"", package->entryPoint, sizeof(package->entryPoint)) ||
        !json_string(s_manifest, "\"abi\"", package->abi, sizeof(package->abi))) return false;
    if (!text_equal(package->entryPoint, "gx_main") || !text_equal(package->abi, GX_ABI_NAME)) return false;
    char executablePath[256];
    if (!package_path(package, package->executable, executablePath, sizeof(executablePath))) return false;
    vfs::FileInfo info;
    if (vfs::stat(executablePath, &info) != vfs::VFS_OK || info.type != vfs::FILE_TYPE_REGULAR) return false;
    package->executableBytes = info.size;
    package->valid = true;
    log_package_line("App Model package discovered", package);
    serial::puts("[NATIVE-ELF] kind=NativeElf arch=amd64 entryPoint=");
    serial::puts(package->entryPoint);
    serial::puts(" abi=");
    serial::puts(package->abi);
    serial::puts(" executable=");
    serial::puts(executablePath);
    serial::puts(" bytes=0x");
    serial::put_hex64(package->executableBytes);
    serial::puts(" resources=package-relative VFS\n");
    return true;
}

void discover() {
    s_packageCount = 0;
    s_discovered = true;
    serial::puts("[NATIVE-ELF] App Model discovery root=/Apps source=bare-metal-VFS\n");
    const uint8_t iterator = vfs::opendir("/Apps");
    if (iterator == 0xFF) {
        serial::puts("[NATIVE-ELF] discovery result=NO_APPS_DIRECTORY\n");
        return;
    }
    vfs::DirEntry entry;
    while (s_packageCount < kMaxPackages && vfs::readdir(iterator, &entry)) {
        if (entry.type != vfs::FILE_TYPE_DIRECTORY || entry.name[0] == '.') continue;
        if (parse_package(entry.name, &s_packages[s_packageCount])) ++s_packageCount;
    }
    vfs::closedir(iterator);
    serial::puts("[NATIVE-ELF] discovery result packages=0x");
    serial::put_hex32(s_packageCount);
    serial::putc('\n');
}

static Package* find_package(const char* appName) {
    if (!appName) return nullptr;
    for (uint32_t i = 0; i < s_packageCount; ++i) {
        Package* package = &s_packages[i];
        if (text_equal(appName, package->displayName) || text_equal(appName, package->id) ||
            text_equal(appName, package->directory) ||
            (text_equal(appName, "PacMan") && text_equal(package->directory, "PacMan"))) return package;
    }
    return nullptr;
}

bool is_available(const char* appName) {
    if (!s_discovered) discover();
    return find_package(appName) != nullptr;
}

struct Elf64Header {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};

struct Elf64ProgramHeader {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
};

static const uint32_t kElfLoad = 1u;

static uint64_t align_down(uint64_t value) { return value & ~(static_cast<uint64_t>(kPageSize) - 1u); }
static uint64_t align_up(uint64_t value) {
    return (value + kPageSize - 1u) & ~(static_cast<uint64_t>(kPageSize) - 1u);
}

static uint32_t read_u32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8u) |
        (static_cast<uint32_t>(bytes[2]) << 16u) |
        (static_cast<uint32_t>(bytes[3]) << 24u);
}

static uint64_t read_u64(const uint8_t* bytes) {
    uint64_t value = 0;
    for (uint32_t byte = 0; byte < sizeof(uint64_t); ++byte) {
        value |= static_cast<uint64_t>(bytes[byte]) << (byte * 8u);
    }
    return value;
}

static void write_u32(uint8_t* bytes, uint32_t value) {
    for (uint32_t byte = 0; byte < sizeof(uint32_t); ++byte) {
        bytes[byte] = static_cast<uint8_t>(value >> (byte * 8u));
    }
}

static void write_u64(uint8_t* bytes, uint64_t value) {
    for (uint32_t byte = 0; byte < sizeof(uint64_t); ++byte) {
        bytes[byte] = static_cast<uint8_t>(value >> (byte * 8u));
    }
}

static uint64_t rebase_static_absolute_words(Runtime* runtime, uint64_t minimum, uint64_t maximum,
                                              uint64_t executableMinimum, uint64_t executableMaximum) {
    // The production package is intentionally static ET_EXEC and has no
    // dynamic relocation table.  Its small-model x86-64 code uses three
    // forms of absolute address: aligned data pointers, movabs imm64 values,
    // and SIB disp32 fields.  Rebase only those instruction encodings and
    // aligned data words; RIP-relative code remains valid under one delta.
    const uint64_t delta = reinterpret_cast<uintptr_t>(runtime->image) - minimum;
    uint64_t count = 0;
    const uint64_t executableStart = executableMinimum - minimum;
    const uint64_t executableEnd = executableMaximum - minimum;

    // REX.W + MOV r64, imm64 (48..4F B8..BF imm64).  The immediate begins at
    // an intentionally unaligned offset in several gx_main call sites.
    for (uint64_t offset = executableStart; offset + 10u <= executableEnd; ++offset) {
        const uint8_t rex = runtime->image[offset];
        const uint8_t opcode = runtime->image[offset + 1u];
        if ((rex & 0xF8u) != 0x48u || opcode < 0xB8u || opcode > 0xBFu) continue;
        const uint64_t value = read_u64(runtime->image + offset + 2u);
        if (value < minimum || value >= maximum) continue;
        write_u64(runtime->image + offset + 2u, value + delta);
        ++count;
    }

    // REX.W + MOV r64, [index*8 + disp32] with no base register.  This is
    // the form emitted for the static maze pointer table.
    for (uint64_t offset = executableStart; offset + 8u <= executableEnd; ++offset) {
        const uint8_t rex = runtime->image[offset];
        const uint8_t opcode = runtime->image[offset + 1u];
        const uint8_t modrm = runtime->image[offset + 2u];
        if ((rex & 0xF8u) != 0x48u || opcode != 0x8Bu || (modrm & 0xC7u) != 0x04u ||
            runtime->image[offset + 3u] != 0xC5u) continue;
        const uint32_t value = read_u32(runtime->image + offset + 4u);
        if (static_cast<uint64_t>(value) < minimum || static_cast<uint64_t>(value) >= maximum ||
            static_cast<uint64_t>(value) + delta > 0xFFFFFFFFull) continue;
        write_u32(runtime->image + offset + 4u, value + static_cast<uint32_t>(delta));
        ++count;
    }

    // Static pointer tables are naturally eight-byte aligned in .data.
    for (uint64_t offset = 0; offset + sizeof(uint64_t) <= runtime->imageBytes; offset += sizeof(uint64_t)) {
        const uint64_t value = read_u64(runtime->image + offset);
        if (value < minimum || value >= maximum) continue;
        write_u64(runtime->image + offset, value + delta);
        ++count;
    }
    return count;
}

static bool load_image(Runtime* runtime) {
    char path[256];
    if (!package_path(runtime->package, runtime->package->executable, path, sizeof(path))) return false;
    Elf64Header header;
    if (!read_file_exact(path, &header, sizeof(header))) {
        log_runtime(runtime, "load FAIL=elf-header-read");
        return false;
    }
    if (header.ident[0] != 0x7F || header.ident[1] != 'E' || header.ident[2] != 'L' || header.ident[3] != 'F' ||
        header.ident[4] != 2 || header.ident[5] != 1 || header.type != 2 || header.machine != 62 ||
        header.phentsize != sizeof(Elf64ProgramHeader) || header.phnum == 0 || header.phnum > 32u ||
        header.entry == 0) {
        log_runtime(runtime, "load FAIL=elf-header-validation");
        return false;
    }

    uint64_t minimum = ~0ull;
    uint64_t maximum = 0;
    uint64_t executableMinimum = ~0ull;
    uint64_t executableMaximum = 0;
    uint32_t loadCount = 0;
    for (uint16_t i = 0; i < header.phnum; ++i) {
        Elf64ProgramHeader program;
        const uint64_t offset = header.phoff + static_cast<uint64_t>(i) * header.phentsize;
        if (offset > runtime->package->executableBytes || !read_at(path, offset, &program, sizeof(program), nullptr)) {
            log_runtime(runtime, "load FAIL=program-header-read");
            return false;
        }
        if (program.type != kElfLoad) continue;
        if (program.filesz > program.memsz || program.offset + program.filesz > runtime->package->executableBytes ||
            program.vaddr + program.memsz < program.vaddr) {
            log_runtime(runtime, "load FAIL=segment-bounds");
            return false;
        }
        const uint64_t start = align_down(program.vaddr);
        const uint64_t end = align_up(program.vaddr + program.memsz);
        if (start < minimum) minimum = start;
        if (end > maximum) maximum = end;
        if ((program.flags & 1u) != 0u) {
            if (start < executableMinimum) executableMinimum = start;
            if (end > executableMaximum) executableMaximum = end;
        }
        ++loadCount;
    }
    if (loadCount == 0 || maximum <= minimum || maximum - minimum > kMaxLoadedImageBytes ||
        header.entry < minimum || header.entry >= maximum || executableMaximum <= executableMinimum) {
        log_runtime(runtime, "load FAIL=load-layout");
        return false;
    }

    const uint64_t imageBytes = maximum - minimum;
    runtime->image = new uint8_t[static_cast<uint32_t>(imageBytes)];
    if (!runtime->image) {
        log_runtime(runtime, "load FAIL=kernel-heap-image-allocation");
        return false;
    }
    runtime->imageBytes = imageBytes;
    runtime->entry = reinterpret_cast<gx_result (GX_CALL *)(gx_app_context*)>(
        runtime->image + (header.entry - minimum));
    for (uint64_t i = 0; i < imageBytes; ++i) runtime->image[i] = 0;

    for (uint16_t i = 0; i < header.phnum; ++i) {
        Elf64ProgramHeader program;
        const uint64_t offset = header.phoff + static_cast<uint64_t>(i) * header.phentsize;
        if (!read_at(path, offset, &program, sizeof(program), nullptr) || program.type != kElfLoad) continue;
        uint64_t copied = 0;
        while (copied < program.filesz) {
            uint32_t chunk = static_cast<uint32_t>(program.filesz - copied);
            if (chunk > kReadChunkBytes) chunk = kReadChunkBytes;
            uint32_t amount = 0;
            if (!read_at(path, program.offset + copied,
                         runtime->image + (program.vaddr - minimum) + copied, chunk, &amount) || amount != chunk) {
                log_runtime(runtime, "load FAIL=segment-copy");
                return false;
            }
            copied += chunk;
        }
    }

    const uint64_t rebasedWords = rebase_static_absolute_words(runtime, minimum, maximum,
        executableMinimum, executableMaximum);

    serial::puts("[NATIVE-ELF] runtime="); serial::put_hex64(s_runtimeSequence);
    serial::puts(" load PASS format=ELF64 machine=amd64 segments=0x"); serial::put_hex32(loadCount);
    serial::puts(" source="); serial::puts(path);
    serial::puts(" imageBytes=0x"); serial::put_hex64(imageBytes);
    serial::puts(" minVaddr=0x"); serial::put_hex64(minimum);
    serial::puts(" maxVaddr=0x"); serial::put_hex64(maximum);
    serial::puts(" relocatedBase=0x"); serial::put_hex64(reinterpret_cast<uintptr_t>(runtime->image));
    serial::puts(" rebasedAbsoluteWords=0x"); serial::put_hex64(rebasedWords);
    serial::puts(" entry=0x"); serial::put_hex64(reinterpret_cast<uintptr_t>(runtime->image + (header.entry - minimum)));
    serial::puts(" stack=app-owned\n");
    return true;
}

static Runtime* runtime_from(gx_app_context* context) {
    return context ? static_cast<Runtime*>(context->userData) : nullptr;
}

static gx_result GX_CALL host_log(gx_app_context* context, const char* message) {
    Runtime* runtime = runtime_from(context);
    serial::puts("[NATIVE-ELF] app=");
    serial::puts(runtime && runtime->package ? runtime->package->displayName : "(none)");
    serial::puts(" ");
    serial::puts(message ? message : "");
    serial::putc('\n');
    return GX_OK;
}

static uint32_t GX_CALL host_get_api_version(gx_app_context*) { return GX_API_VERSION; }

static gx_result GX_CALL host_request_window_ex(gx_app_context* context, const char* title,
                                                 int width, int height, uint32_t flags, gx_handle* outWindow) {
    Runtime* runtime = runtime_from(context);
    if (!runtime || !outWindow || !title || runtime->owner) return GX_ERROR_INVALID_ARGUMENT;
    if (width != 480 || height != 640) return GX_ERROR_UNSUPPORTED;
    runtime->owner = new NativeWindowOwner(runtime);
    if (!runtime->owner || !runtime->owner->create(title, width, height, flags)) {
        if (runtime->owner) delete runtime->owner;
        runtime->owner = nullptr;
        return GX_ERROR_FAILED;
    }
    *outWindow = runtime->owner->window()->id;
    log_runtime(runtime, "window PASS title=Nexgen PacMan size=480x640 fixed centered compositor");
    return GX_OK;
}

static gx_result GX_CALL host_request_window(gx_app_context* context, const char* title,
                                             int width, int height, gx_handle* outWindow) {
    return host_request_window_ex(context, title, width, height, GX_WINDOW_FLAG_FIXED_SIZE, outWindow);
}

static gx_result GX_CALL host_draw_text(gx_app_context*, gx_handle, int, int, const char*) {
    return GX_ERROR_UNSUPPORTED;
}

static gx_result GX_CALL host_draw_rect(gx_app_context*, gx_handle, int, int, int, int, uint32_t) {
    return GX_ERROR_UNSUPPORTED;
}

static void pump_desktop(Runtime* runtime) {
    input::poll();
    const int8_t wheel = input::mouse_scroll_y();
    if (input::mouse_dirty()) {
        input::mouse_clear_dirty();
        desktop::handle_mouse(input::mouse_x(), input::mouse_y(), input::mouse_buttons());
    }
    if (wheel != 0) desktop::handle_mouse_wheel(input::mouse_x(), input::mouse_y(), wheel);
    desktop::draw();
    desktop::draw_cursor(input::mouse_x(), input::mouse_y());
    (void)runtime;
}

static gx_result GX_CALL host_poll_event(gx_app_context* context, gx_event* outEvent, int timeoutMs) {
    Runtime* runtime = runtime_from(context);
    if (!runtime || !outEvent) return GX_ERROR_INVALID_ARGUMENT;
    outEvent->size = sizeof(gx_event);
    outEvent->type = GX_EVENT_NONE;
    outEvent->window = runtime->owner && runtime->owner->window() ? runtime->owner->window()->id : 0;
    outEvent->param1 = outEvent->param2 = outEvent->param3 = outEvent->param4 = 0;
    const uint64_t start = pit::ticks();
    const uint64_t deadline = start + (timeoutMs > 0 ? static_cast<uint64_t>((timeoutMs + 9) / 10) : 0);
    for (;;) {
        if (!runtime->owner || runtime->owner->closed()) {
            outEvent->type = GX_EVENT_WINDOW_CLOSE;
            return GX_OK;
        }

        pump_desktop(runtime);
        const bool focused = compositor::KernelCompositor::getFocusedWindow() == runtime->owner->window();
        if (!runtime->focusKnown || focused != runtime->lastFocus) {
            runtime->focusKnown = true;
            runtime->lastFocus = focused;
            outEvent->type = focused ? GX_EVENT_WINDOW_FOCUS : GX_EVENT_WINDOW_BLUR;
            return GX_OK;
        }

        ps2keyboard::KeyEvent keyEvent;
        if (ps2keyboard::get_event(&keyEvent)) {
            outEvent->type = GX_EVENT_KEY;
            outEvent->param1 = static_cast<int>(keyEvent.key == ps2keyboard::KEY_EVENT_LEFT ? GX_KEY_LEFT :
                keyEvent.key == ps2keyboard::KEY_EVENT_UP ? GX_KEY_UP :
                keyEvent.key == ps2keyboard::KEY_EVENT_RIGHT ? GX_KEY_RIGHT :
                keyEvent.key == ps2keyboard::KEY_EVENT_DOWN ? GX_KEY_DOWN : keyEvent.key);
            outEvent->param2 = keyEvent.action == ps2keyboard::KeyAction::Down ? GX_KEY_ACTION_DOWN : GX_KEY_ACTION_UP;
            outEvent->param3 = (ps2keyboard::is_shift_down() ? GX_KEY_MOD_SHIFT : 0) |
                (ps2keyboard::is_ctrl_down() ? GX_KEY_MOD_CTRL : 0) |
                (ps2keyboard::is_alt_down() ? GX_KEY_MOD_ALT : 0);
            return GX_OK;
        }

        if (timeoutMs <= 0 || pit::ticks() >= deadline) return GX_ERROR_TIMEOUT;
        arch::halt();
    }
}

static gx_result GX_CALL host_wait_for_close(gx_app_context* context, gx_handle window, int timeoutMs) {
    Runtime* runtime = runtime_from(context);
    if (!runtime || !runtime->owner || window != runtime->owner->window()->id) return GX_ERROR_INVALID_ARGUMENT;
    for (;;) {
        gx_event event;
        const gx_result result = host_poll_event(context, &event, timeoutMs);
        if (result != GX_OK) return result;
        if (event.type == GX_EVENT_WINDOW_CLOSE) return GX_OK;
    }
}

static gx_result GX_CALL host_exit(gx_app_context* context, gx_result exitCode) {
    Runtime* runtime = runtime_from(context);
    if (!runtime) return GX_ERROR_INVALID_ARGUMENT;
    runtime->exitRequested = true;
    runtime->exitCode = exitCode;
    return GX_OK;
}

static gx_result GX_CALL host_file_exists(gx_app_context* context, const char* relative, uint32_t* outExists) {
    Runtime* runtime = runtime_from(context);
    if (!runtime || !outExists) return GX_ERROR_INVALID_ARGUMENT;
    char path[256];
    if (!package_path(runtime->package, relative, path, sizeof(path))) return GX_ERROR_PERMISSION_DENIED;
    *outExists = vfs::exists(path) ? 1u : 0u;
    return GX_OK;
}

static gx_result GX_CALL host_file_read(gx_app_context* context, const char* relative, uint64_t offset,
                                        void* buffer, uint32_t bufferSize, uint32_t* outBytesRead) {
    Runtime* runtime = runtime_from(context);
    if (outBytesRead) *outBytesRead = 0;
    if (!runtime || !relative || !buffer || !outBytesRead || bufferSize == 0) return GX_ERROR_INVALID_ARGUMENT;
    char path[256];
    if (!package_path(runtime->package, relative, path, sizeof(path))) return GX_ERROR_PERMISSION_DENIED;
    const uint8_t handle = vfs::open(path, vfs::OPEN_READ);
    if (handle == 0xFF) return GX_ERROR_FAILED;
    const int64_t size = vfs::file_size(handle);
    if (size < 0 || offset > static_cast<uint64_t>(size) ||
        vfs::seek(handle, static_cast<int64_t>(offset), vfs::SEEK_SET) != vfs::VFS_OK) {
        vfs::close(handle);
        return GX_ERROR_FAILED;
    }
    uint32_t requested = bufferSize;
    if (requested > kReadChunkBytes) requested = kReadChunkBytes;
    const int32_t amount = vfs::read(handle, buffer, requested);
    vfs::close(handle);
    if (amount < 0) return GX_ERROR_FAILED;
    *outBytesRead = static_cast<uint32_t>(amount);
    if (runtime->readLogCount < 80u) {
        ++runtime->readLogCount;
        serial::puts("[NATIVE-ELF] file_read app="); serial::puts(runtime->package->displayName);
        serial::puts(" relative="); serial::puts(relative);
        serial::puts(" offset=0x"); serial::put_hex64(offset);
        serial::puts(" requested=0x"); serial::put_hex32(requested);
        serial::puts(" returned=0x"); serial::put_hex32(static_cast<uint32_t>(amount));
        serial::puts(" size=0x"); serial::put_hex64(static_cast<uint64_t>(size));
        serial::puts(" path="); serial::puts(path); serial::putc('\n');
    }
    return GX_OK;
}

static gx_result GX_CALL host_file_read_all(gx_app_context* context, const char* relative,
                                            void* buffer, uint32_t bufferSize, uint32_t* outBytesRead) {
    if (!outBytesRead) return GX_ERROR_INVALID_ARGUMENT;
    *outBytesRead = 0;
    uint64_t offset = 0;
    uint8_t* output = static_cast<uint8_t*>(buffer);
    while (offset < bufferSize) {
        uint32_t amount = 0;
        const gx_result result = host_file_read(context, relative, offset, output + offset,
            bufferSize - static_cast<uint32_t>(offset), &amount);
        if (result != GX_OK) return result;
        if (amount == 0) break;
        offset += amount;
        *outBytesRead = static_cast<uint32_t>(offset);
        if (amount < kReadChunkBytes) break;
    }
    return GX_OK;
}

static gx_result GX_CALL host_present_frame(gx_app_context* context, gx_handle window, int x, int y,
                                            int width, int height, uint32_t strideBytes, uint32_t pixelFormat,
                                            const void* pixels, uint32_t pixelBytes) {
    Runtime* runtime = runtime_from(context);
    if (!runtime || !runtime->owner || !runtime->owner->window() ||
        window != runtime->owner->window()->id || !pixels || x != 0 || y != 0 ||
        width != 448 || height != 553 || strideBytes != 1792u ||
        pixelFormat != GX_PIXEL_FORMAT_XRGB8888 || pixelBytes < strideBytes * static_cast<uint32_t>(height) ||
        pixelBytes > kMaxFrameBytes) return GX_ERROR_INVALID_ARGUMENT;
    runtime->owner->present(pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height), strideBytes);
    if (runtime->owner->closed()) return GX_ERROR_FAILED;
    serial::puts("[NATIVE-ELF] frame PASS app="); serial::puts(runtime->package->displayName);
    serial::puts(" window=0x"); serial::put_hex64(window);
    serial::puts(" size=448x553 stride=1792 bytes=990976 pixelFormat=XRGB8888\n");
    return GX_OK;
}

static uint64_t GX_CALL host_get_ticks_ms(gx_app_context* context) {
    Runtime* runtime = runtime_from(context);
    const uint64_t value = pit::ticks() * 10u;
    if (runtime && runtime->tickLogCount < 3u) {
        ++runtime->tickLogCount;
        serial::puts("[NATIVE-ELF] ticks_ms app="); serial::puts(runtime->package->displayName);
        serial::puts(" value=0x"); serial::put_hex64(value); serial::puts(" source=PIT-100Hz\n");
    }
    return value;
}

static void initialize_host_table(Runtime* runtime) {
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&runtime->host);
    for (uint32_t i = 0; i < sizeof(runtime->host); ++i) bytes[i] = 0;
    runtime->host.size = sizeof(runtime->host);
    runtime->host.version = GX_API_VERSION;
    runtime->host.log = host_log;
    runtime->host.get_api_version = host_get_api_version;
    runtime->host.request_window = host_request_window;
    runtime->host.draw_text = host_draw_text;
    runtime->host.draw_rect = host_draw_rect;
    runtime->host.wait_for_close = host_wait_for_close;
    runtime->host.poll_event = host_poll_event;
    runtime->host.exit = host_exit;
    runtime->host.file_read_all = host_file_read_all;
    runtime->host.file_exists = host_file_exists;
    runtime->host.request_window_ex = host_request_window_ex;
    runtime->host.file_read = host_file_read;
    runtime->host.present_frame = host_present_frame;
    runtime->host.get_ticks_ms = host_get_ticks_ms;
}

static bool run_package(Package* package) {
    Runtime runtime{};
    runtime.package = package;
    runtime.exitCode = GX_OK;
    ++s_runtimeSequence;
    initialize_host_table(&runtime);
    runtime.context.size = sizeof(runtime.context);
    runtime.context.apiVersion = GX_API_VERSION;
    runtime.context.host = &runtime.host;
    runtime.context.userData = &runtime;
    log_runtime(&runtime, "launch begin path=package-relative runtime=native-elf abi=guidexos-c-abi-v1");
    if (!load_image(&runtime)) return false;
    runtime.stack = new uint8_t[kRuntimeStackBytes];
    if (!runtime.stack) {
        delete[] runtime.image;
        runtime.image = nullptr;
        log_runtime(&runtime, "launch FAIL=app-stack-allocation");
        return false;
    }
    serial::puts("[NATIVE-ELF] runtime="); serial::put_hex64(s_runtimeSequence);
    serial::puts(" stack allocated bytes=0x"); serial::put_hex32(kRuntimeStackBytes);
    serial::puts(" range=0x"); serial::put_hex64(reinterpret_cast<uintptr_t>(runtime.stack));
    serial::puts("..0x"); serial::put_hex64(reinterpret_cast<uintptr_t>(runtime.stack + kRuntimeStackBytes));
    serial::putc('\n');

    gx_result result = gxos_native_call_on_stack(runtime.entry, &runtime.context,
        runtime.stack + kRuntimeStackBytes);
    serial::puts("[NATIVE-ELF] runtime="); serial::put_hex64(s_runtimeSequence);
    serial::puts(" exit result=0x"); serial::put_hex32(static_cast<uint32_t>(result));
    serial::puts(" requested="); serial::puts(runtime.exitRequested ? "1" : "0");
    serial::puts(" fault=none\n");

    if (runtime.owner) {
        if (runtime.owner->window()) runtime.owner->requestClose();
        delete runtime.owner;
        runtime.owner = nullptr;
    }
    delete[] runtime.stack;
    delete[] runtime.image;
    runtime.stack = nullptr;
    runtime.image = nullptr;
    log_runtime(&runtime, result == GX_OK ? "lifecycle PASS window/resource cleanup complete" : "lifecycle FAIL app-returned-error");
    return result == GX_OK;
}

bool launch(const char* appName) {
    if (s_launchInProgress) return false;
    if (!s_discovered) discover();
    Package* package = find_package(appName);
    if (!package) return false;
    s_launchInProgress = true;
    const bool result = run_package(package);
    s_launchInProgress = false;
    return result;
}

#else

void discover() {}
bool launch(const char*) { return false; }
bool is_available(const char*) { return false; }

#endif

} // namespace native_elf
} // namespace kernel
