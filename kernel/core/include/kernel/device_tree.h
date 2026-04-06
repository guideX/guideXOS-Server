// Device Tree (Flattened Device Tree / DTB) Parser
//
// Provides parsing of Device Tree Blob (DTB) files for hardware discovery
// on ARM, ARM64, RISC-V, and other architectures that use Device Tree.
//
// The Device Tree is a data structure for describing hardware that cannot
// be discovered at runtime. It's passed by firmware (U-Boot, OpenSBI, etc.)
// to the kernel during boot.
//
// Features:
//   - DTB header validation
//   - Structure block parsing
//   - Property extraction
//   - Node traversal
//   - Phandle resolution
//
// Reference: Devicetree Specification (devicetree.org)
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_DEVICE_TREE_H
#define KERNEL_DEVICE_TREE_H

#include "kernel/types.h"

namespace kernel {
namespace fdt {

// ================================================================
// FDT Constants
// ================================================================

static const uint32_t FDT_MAGIC = 0xD00DFEED;  // Big-endian magic

// FDT structure tokens (big-endian in blob)
static const uint32_t FDT_BEGIN_NODE = 0x00000001;
static const uint32_t FDT_END_NODE   = 0x00000002;
static const uint32_t FDT_PROP       = 0x00000003;
static const uint32_t FDT_NOP        = 0x00000004;
static const uint32_t FDT_END        = 0x00000009;

// Maximum limits
static const size_t FDT_MAX_PATH_LEN     = 256;
static const size_t FDT_MAX_NAME_LEN     = 64;
static const size_t FDT_MAX_PHANDLE_ARGS = 16;
static const size_t FDT_MAX_DEPTH        = 16;

// ================================================================
// FDT Status Codes
// ================================================================

enum FdtStatus : int8_t {
    FDT_OK              =  0,
    FDT_ERR_INVALID     = -1,   // Invalid parameter
    FDT_ERR_BAD_MAGIC   = -2,   // Not a valid DTB
    FDT_ERR_BAD_VERSION = -3,   // Unsupported version
    FDT_ERR_CORRUPT     = -4,   // Corrupted structure
    FDT_ERR_NOT_FOUND   = -5,   // Node/property not found
    FDT_ERR_TRUNCATED   = -6,   // Unexpected end of data
    FDT_ERR_NO_MEMORY   = -7,   // Buffer too small
};

// ================================================================
// FDT On-Disk Structures (all big-endian)
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define FDT_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define FDT_PACKED
#endif

// FDT Header
struct FdtHeader {
    uint32_t magic;              // Magic number (0xD00DFEED)
    uint32_t totalsize;          // Total size of DTB
    uint32_t off_dt_struct;      // Offset to structure block
    uint32_t off_dt_strings;     // Offset to strings block
    uint32_t off_mem_rsvmap;     // Offset to memory reservation block
    uint32_t version;            // Format version
    uint32_t last_comp_version;  // Last compatible version
    uint32_t boot_cpuid_phys;    // Physical CPU ID of boot CPU
    uint32_t size_dt_strings;    // Size of strings block
    uint32_t size_dt_struct;     // Size of structure block
} FDT_PACKED;

// Memory Reservation Entry
struct FdtReserveEntry {
    uint64_t address;            // Physical address
    uint64_t size;               // Size in bytes
} FDT_PACKED;

// Property header (in structure block)
struct FdtProperty {
    uint32_t len;                // Length of property value
    uint32_t nameoff;            // Offset into strings block
} FDT_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef FDT_PACKED

// ================================================================
// Byte-swap Helpers (DTB is big-endian)
// ================================================================

static inline uint32_t fdt32_to_cpu(uint32_t x) {
    return ((x >> 24) & 0xFF) |
           ((x >> 8)  & 0xFF00) |
           ((x << 8)  & 0xFF0000) |
           ((x << 24) & 0xFF000000);
}

static inline uint64_t fdt64_to_cpu(uint64_t x) {
    return ((x >> 56) & 0xFFULL) |
           ((x >> 40) & 0xFF00ULL) |
           ((x >> 24) & 0xFF0000ULL) |
           ((x >> 8)  & 0xFF000000ULL) |
           ((x << 8)  & 0xFF00000000ULL) |
           ((x << 24) & 0xFF0000000000ULL) |
           ((x << 40) & 0xFF000000000000ULL) |
           ((x << 56) & 0xFF00000000000000ULL);
}

// ================================================================
// FDT Context
// ================================================================

struct FdtContext {
    const uint8_t* dtb;          // Pointer to DTB in memory
    uint32_t       totalSize;    // Total DTB size
    uint32_t       structOffset; // Offset to structure block
    uint32_t       stringsOffset;// Offset to strings block
    uint32_t       structSize;   // Size of structure block
    uint32_t       stringsSize;  // Size of strings block
    uint32_t       version;      // FDT version
    bool           valid;        // Is context valid
};

// ================================================================
// Node Reference (for traversal)
// ================================================================

struct FdtNode {
    const FdtContext* ctx;       // Parent context
    uint32_t          offset;    // Offset within structure block
    const char*       name;      // Node name (points into DTB)
    int               depth;     // Nesting depth
};

// ================================================================
// Property Reference
// ================================================================

struct FdtPropRef {
    const char*    name;         // Property name (points into DTB)
    const uint8_t* value;        // Property value (points into DTB)
    uint32_t       length;       // Value length in bytes
};

// ================================================================
// Memory Region (for memory/reserved-memory parsing)
// ================================================================

struct FdtMemoryRegion {
    uint64_t base;               // Physical base address
    uint64_t size;               // Size in bytes
};

// ================================================================
// Interrupt Specifier
// ================================================================

struct FdtInterrupt {
    uint32_t cells[FDT_MAX_PHANDLE_ARGS];
    int      numCells;
};

// ================================================================
// Address/Size Cell Info
// ================================================================

struct FdtCellInfo {
    int addressCells;            // #address-cells (default 2)
    int sizeCells;               // #size-cells (default 1)
};

// ================================================================
// Core API
// ================================================================

// Initialize FDT context from DTB blob
// dtb: pointer to DTB in memory
// Returns FDT_OK on success
FdtStatus init(const void* dtb, FdtContext* ctx);

// Validate DTB structure (more thorough than init)
FdtStatus validate(const FdtContext* ctx);

// Get total DTB size
uint32_t get_size(const FdtContext* ctx);

// Get FDT version
uint32_t get_version(const FdtContext* ctx);

// ================================================================
// Node Traversal
// ================================================================

// Get root node
FdtStatus get_root(const FdtContext* ctx, FdtNode* nodeOut);

// Find node by path (e.g., "/soc/serial@10000000")
FdtStatus find_node(const FdtContext* ctx, const char* path, FdtNode* nodeOut);

// Find node by compatible string
// Searches entire tree for node with matching "compatible" property
FdtStatus find_compatible(const FdtContext* ctx, const char* compatible, FdtNode* nodeOut);

// Find node by phandle value
FdtStatus find_phandle(const FdtContext* ctx, uint32_t phandle, FdtNode* nodeOut);

// Get first child of a node
FdtStatus get_first_child(const FdtNode* node, FdtNode* childOut);

// Get next sibling of a node
FdtStatus get_next_sibling(const FdtNode* node, FdtNode* siblingOut);

// Get parent of a node
FdtStatus get_parent(const FdtNode* node, FdtNode* parentOut);

// Get node name (unit-address excluded)
const char* get_node_name(const FdtNode* node);

// Get node unit address (e.g., "10000000" from "serial@10000000")
// Returns nullptr if no unit address
const char* get_unit_address(const FdtNode* node);

// Get full path of node into buffer
FdtStatus get_path(const FdtNode* node, char* buffer, size_t bufferSize);

// ================================================================
// Property Access
// ================================================================

// Get property by name
FdtStatus get_property(const FdtNode* node, const char* name, FdtPropRef* propOut);

// Check if node has a property
bool has_property(const FdtNode* node, const char* name);

// Get first property of node
FdtStatus get_first_property(const FdtNode* node, FdtPropRef* propOut);

// Get next property (iteration)
FdtStatus get_next_property(const FdtNode* node, const FdtPropRef* current, 
                            FdtPropRef* nextOut);

// ================================================================
// Property Value Helpers
// ================================================================

// Read property as single u32
FdtStatus read_u32(const FdtPropRef* prop, uint32_t* valueOut);

// Read property as single u64
FdtStatus read_u64(const FdtPropRef* prop, uint64_t* valueOut);

// Read property as array of u32
// Returns number of elements read
int read_u32_array(const FdtPropRef* prop, uint32_t* arrayOut, int maxCount);

// Read property as string
// Returns pointer to string (null-terminated)
const char* read_string(const FdtPropRef* prop);

// Read property as string list
// Returns number of strings
int read_string_list(const FdtPropRef* prop, const char** stringsOut, int maxCount);

// Check if property value matches a string
bool string_equals(const FdtPropRef* prop, const char* str);

// Check if property string list contains a string
bool string_list_contains(const FdtPropRef* prop, const char* str);

// ================================================================
// Reg Property Parsing
// ================================================================

// Read "reg" property as address/size pairs
// cellInfo provides #address-cells and #size-cells from parent
// Returns number of regions read
int read_reg(const FdtNode* node, const FdtCellInfo* cellInfo,
             FdtMemoryRegion* regionsOut, int maxCount);

// Get #address-cells and #size-cells for a node's children
FdtStatus get_cell_info(const FdtNode* node, FdtCellInfo* infoOut);

// ================================================================
// Interrupt Parsing
// ================================================================

// Read "interrupts" property
// Returns number of interrupt specifiers
int read_interrupts(const FdtNode* node, FdtInterrupt* interruptsOut, int maxCount);

// Get interrupt parent node
FdtStatus get_interrupt_parent(const FdtNode* node, FdtNode* parentOut);

// ================================================================
// Common Node Types
// ================================================================

// Find /memory node and extract regions
int get_memory_regions(const FdtContext* ctx, FdtMemoryRegion* regionsOut, int maxCount);

// Find /reserved-memory regions
int get_reserved_memory(const FdtContext* ctx, FdtMemoryRegion* regionsOut, int maxCount);

// Find /chosen node
FdtStatus get_chosen(const FdtContext* ctx, FdtNode* nodeOut);

// Get bootargs from /chosen
const char* get_bootargs(const FdtContext* ctx);

// Get stdout-path from /chosen
const char* get_stdout_path(const FdtContext* ctx);

// Find /cpus node
FdtStatus get_cpus(const FdtContext* ctx, FdtNode* nodeOut);

// Count CPU nodes
int count_cpus(const FdtContext* ctx);

// ================================================================
// Bus/Device Discovery
// ================================================================

// Callback for device enumeration
// Return false to stop enumeration
typedef bool (*FdtDeviceCallback)(const FdtNode* node, void* context);

// Enumerate all nodes with given compatible string
// Calls callback for each matching node
FdtStatus for_each_compatible(const FdtContext* ctx, const char* compatible,
                              FdtDeviceCallback callback, void* context);

// Enumerate all child nodes of a node
FdtStatus for_each_child(const FdtNode* parent, FdtDeviceCallback callback, void* context);

// ================================================================
// Address Translation
// ================================================================

// Translate child address to parent address space
// Uses "ranges" property of bus nodes
FdtStatus translate_address(const FdtNode* node, uint64_t childAddr, uint64_t* parentAddr);

// Translate address to CPU physical address (full translation)
FdtStatus translate_to_physical(const FdtNode* node, uint64_t localAddr, uint64_t* physAddr);

// ================================================================
// Debug/Status
// ================================================================

// Print FDT summary to serial debug
void print_summary(const FdtContext* ctx);

// Print node and its properties
void print_node(const FdtNode* node);

// Walk entire tree and print (verbose debug)
void dump_tree(const FdtContext* ctx);

// Get status string for error code
const char* status_string(FdtStatus status);

} // namespace fdt
} // namespace kernel

#endif // KERNEL_DEVICE_TREE_H

