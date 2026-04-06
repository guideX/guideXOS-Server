// Device Tree (FDT/DTB) Parser Implementation
//
// Parses Flattened Device Tree blobs for hardware discovery
// on ARM, ARM64, RISC-V, and other DT-using architectures.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/device_tree.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace fdt {

// ================================================================
// Helper Functions
// ================================================================

static int strlen(const char* s)
{
    int len = 0;
    while (s[len]) ++len;
    return len;
}

static int strcmp(const char* a, const char* b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return static_cast<uint8_t>(*a) - static_cast<uint8_t>(*b);
}

static int strncmp(const char* a, const char* b, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            return static_cast<uint8_t>(a[i]) - static_cast<uint8_t>(b[i]);
        }
        if (a[i] == '\0') return 0;
    }
    return 0;
}

static void memcopy(void* dst, const void* src, size_t len)
{
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < len; ++i) {
        d[i] = s[i];
    }
}

// Align offset to 4-byte boundary
static inline uint32_t align4(uint32_t offset) {
    return (offset + 3) & ~3;
}

// Read u32 from structure block (big-endian)
static inline uint32_t read_struct_u32(const FdtContext* ctx, uint32_t offset) {
    if (offset + 4 > ctx->structSize) return 0;
    const uint8_t* p = ctx->dtb + ctx->structOffset + offset;
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

// Get string from strings block
static inline const char* get_string(const FdtContext* ctx, uint32_t offset) {
    if (offset >= ctx->stringsSize) return "";
    return reinterpret_cast<const char*>(ctx->dtb + ctx->stringsOffset + offset);
}

// ================================================================
// Initialization
// ================================================================

FdtStatus init(const void* dtb, FdtContext* ctx)
{
    if (dtb == nullptr || ctx == nullptr) {
        return FDT_ERR_INVALID;
    }
    
    const FdtHeader* header = static_cast<const FdtHeader*>(dtb);
    
    // Check magic (big-endian)
    uint32_t magic = fdt32_to_cpu(header->magic);
    if (magic != FDT_MAGIC) {
        return FDT_ERR_BAD_MAGIC;
    }
    
    // Get version
    uint32_t version = fdt32_to_cpu(header->version);
    uint32_t lastCompat = fdt32_to_cpu(header->last_comp_version);
    
    // We support version 17 (standard) and up
    // Must be compatible with version 16
    if (lastCompat > 17) {
        return FDT_ERR_BAD_VERSION;
    }
    
    // Initialize context
    ctx->dtb = static_cast<const uint8_t*>(dtb);
    ctx->totalSize = fdt32_to_cpu(header->totalsize);
    ctx->structOffset = fdt32_to_cpu(header->off_dt_struct);
    ctx->stringsOffset = fdt32_to_cpu(header->off_dt_strings);
    ctx->structSize = fdt32_to_cpu(header->size_dt_struct);
    ctx->stringsSize = fdt32_to_cpu(header->size_dt_strings);
    ctx->version = version;
    ctx->valid = true;
    
    return FDT_OK;
}

FdtStatus validate(const FdtContext* ctx)
{
    if (ctx == nullptr || !ctx->valid) {
        return FDT_ERR_INVALID;
    }
    
    // Verify offsets are within bounds
    if (ctx->structOffset + ctx->structSize > ctx->totalSize ||
        ctx->stringsOffset + ctx->stringsSize > ctx->totalSize) {
        return FDT_ERR_CORRUPT;
    }
    
    // Walk structure to verify integrity
    uint32_t offset = 0;
    int depth = 0;
    bool foundEnd = false;
    
    while (offset < ctx->structSize) {
        uint32_t token = read_struct_u32(ctx, offset);
        offset += 4;
        
        switch (token) {
            case FDT_BEGIN_NODE: {
                ++depth;
                // Skip name string (null-terminated)
                while (offset < ctx->structSize && 
                       ctx->dtb[ctx->structOffset + offset] != '\0') {
                    ++offset;
                }
                ++offset;  // Skip null
                offset = align4(offset);
                break;
            }
            
            case FDT_END_NODE:
                --depth;
                if (depth < 0) return FDT_ERR_CORRUPT;
                break;
                
            case FDT_PROP: {
                if (offset + 8 > ctx->structSize) return FDT_ERR_TRUNCATED;
                uint32_t len = read_struct_u32(ctx, offset);
                offset += 8;  // Skip len and nameoff
                offset += len;
                offset = align4(offset);
                break;
            }
            
            case FDT_NOP:
                break;
                
            case FDT_END:
                if (depth != 0) return FDT_ERR_CORRUPT;
                foundEnd = true;
                break;
                
            default:
                return FDT_ERR_CORRUPT;
        }
        
        if (foundEnd) break;
    }
    
    if (!foundEnd) return FDT_ERR_TRUNCATED;
    
    return FDT_OK;
}

uint32_t get_size(const FdtContext* ctx)
{
    return ctx ? ctx->totalSize : 0;
}

uint32_t get_version(const FdtContext* ctx)
{
    return ctx ? ctx->version : 0;
}

// ================================================================
// Node Traversal
// ================================================================

FdtStatus get_root(const FdtContext* ctx, FdtNode* nodeOut)
{
    if (ctx == nullptr || !ctx->valid || nodeOut == nullptr) {
        return FDT_ERR_INVALID;
    }
    
    // First token should be FDT_BEGIN_NODE for root
    uint32_t token = read_struct_u32(ctx, 0);
    if (token != FDT_BEGIN_NODE) {
        return FDT_ERR_CORRUPT;
    }
    
    nodeOut->ctx = ctx;
    nodeOut->offset = 0;
    nodeOut->name = "";  // Root has empty name
    nodeOut->depth = 0;
    
    return FDT_OK;
}

// Internal: skip to end of current node (including children)
static uint32_t skip_node(const FdtContext* ctx, uint32_t offset)
{
    int depth = 1;
    
    while (offset < ctx->structSize && depth > 0) {
        uint32_t token = read_struct_u32(ctx, offset);
        offset += 4;
        
        switch (token) {
            case FDT_BEGIN_NODE:
                ++depth;
                while (offset < ctx->structSize && 
                       ctx->dtb[ctx->structOffset + offset] != '\0') {
                    ++offset;
                }
                ++offset;
                offset = align4(offset);
                break;
                
            case FDT_END_NODE:
                --depth;
                break;
                
            case FDT_PROP: {
                uint32_t len = read_struct_u32(ctx, offset);
                offset += 8;
                offset += len;
                offset = align4(offset);
                break;
            }
            
            case FDT_NOP:
                break;
                
            case FDT_END:
                return offset;
                
            default:
                break;
        }
    }
    
    return offset;
}

FdtStatus find_node(const FdtContext* ctx, const char* path, FdtNode* nodeOut)
{
    if (ctx == nullptr || !ctx->valid || path == nullptr || nodeOut == nullptr) {
        return FDT_ERR_INVALID;
    }
    
    // Start from root
    FdtNode current;
    FdtStatus status = get_root(ctx, &current);
    if (status != FDT_OK) return status;
    
    // Skip leading slash
    if (*path == '/') ++path;
    
    // Empty path means root
    if (*path == '\0') {
        *nodeOut = current;
        return FDT_OK;
    }
    
    // Parse path components
    while (*path) {
        // Extract component
        const char* compEnd = path;
        while (*compEnd && *compEnd != '/') ++compEnd;
        int compLen = compEnd - path;
        
        // Search children for matching name
        FdtNode child;
        status = get_first_child(&current, &child);
        bool found = false;
        
        while (status == FDT_OK) {
            const char* childName = get_node_name(&child);
            int nameLen = 0;
            while (childName[nameLen] && childName[nameLen] != '@') ++nameLen;
            
            // Compare including unit-address if present in path
            if (strncmp(path, childName, compLen) == 0 &&
                (childName[compLen] == '\0' || childName[compLen] == '@' ||
                 compLen == nameLen)) {
                found = true;
                current = child;
                break;
            }
            
            status = get_next_sibling(&child, &child);
        }
        
        if (!found) {
            return FDT_ERR_NOT_FOUND;
        }
        
        // Move to next component
        path = compEnd;
        if (*path == '/') ++path;
    }
    
    *nodeOut = current;
    return FDT_OK;
}

FdtStatus find_compatible(const FdtContext* ctx, const char* compatible, FdtNode* nodeOut)
{
    if (ctx == nullptr || !ctx->valid || compatible == nullptr || nodeOut == nullptr) {
        return FDT_ERR_INVALID;
    }
    
    // Walk entire tree looking for matching compatible
    uint32_t offset = 0;
    int depth = 0;
    
    while (offset < ctx->structSize) {
        uint32_t token = read_struct_u32(ctx, offset);
        
        if (token == FDT_BEGIN_NODE) {
            uint32_t nodeOffset = offset;
            offset += 4;
            
            // Get node name
            const char* name = reinterpret_cast<const char*>(
                ctx->dtb + ctx->structOffset + offset);
            
            while (offset < ctx->structSize && 
                   ctx->dtb[ctx->structOffset + offset] != '\0') {
                ++offset;
            }
            ++offset;
            offset = align4(offset);
            
            // Check properties for compatible
            uint32_t propOffset = offset;
            while (propOffset < ctx->structSize) {
                uint32_t propToken = read_struct_u32(ctx, propOffset);
                if (propToken != FDT_PROP) break;
                
                propOffset += 4;
                uint32_t len = read_struct_u32(ctx, propOffset);
                uint32_t nameoff = read_struct_u32(ctx, propOffset + 4);
                propOffset += 8;
                
                const char* propName = get_string(ctx, nameoff);
                if (strcmp(propName, "compatible") == 0) {
                    // Check if compatible string matches
                    const char* val = reinterpret_cast<const char*>(
                        ctx->dtb + ctx->structOffset + propOffset);
                    const char* end = val + len;
                    
                    while (val < end) {
                        if (strcmp(val, compatible) == 0) {
                            nodeOut->ctx = ctx;
                            nodeOut->offset = nodeOffset;
                            nodeOut->name = name;
                            nodeOut->depth = depth;
                            return FDT_OK;
                        }
                        while (val < end && *val) ++val;
                        ++val;
                    }
                }
                
                propOffset += len;
                propOffset = align4(propOffset);
            }
            
            ++depth;
        }
        else if (token == FDT_END_NODE) {
            offset += 4;
            --depth;
        }
        else if (token == FDT_PROP) {
            offset += 4;
            uint32_t len = read_struct_u32(ctx, offset);
            offset += 8;
            offset += len;
            offset = align4(offset);
        }
        else if (token == FDT_NOP) {
            offset += 4;
        }
        else if (token == FDT_END) {
            break;
        }
        else {
            offset += 4;
        }
    }
    
    return FDT_ERR_NOT_FOUND;
}

FdtStatus find_phandle(const FdtContext* ctx, uint32_t phandle, FdtNode* nodeOut)
{
    /*
     * STUB: Find node by phandle
     * 
     * Full implementation would walk tree and check each node's
     * "phandle" or "linux,phandle" property.
     */
    
    if (ctx == nullptr || !ctx->valid || nodeOut == nullptr) {
        return FDT_ERR_INVALID;
    }
    
    (void)phandle;
    kernel::serial::puts("[FDT] find_phandle: stub implementation\n");
    return FDT_ERR_NOT_FOUND;
}

FdtStatus get_first_child(const FdtNode* node, FdtNode* childOut)
{
    if (node == nullptr || node->ctx == nullptr || childOut == nullptr) {
        return FDT_ERR_INVALID;
    }
    
    const FdtContext* ctx = node->ctx;
    uint32_t offset = node->offset + 4;  // Skip BEGIN_NODE token
    
    // Skip node name
    while (offset < ctx->structSize && 
           ctx->dtb[ctx->structOffset + offset] != '\0') {
        ++offset;
    }
    ++offset;
    offset = align4(offset);
    
    // Skip properties
    while (offset < ctx->structSize) {
        uint32_t token = read_struct_u32(ctx, offset);
        if (token == FDT_PROP) {
            offset += 4;
            uint32_t len = read_struct_u32(ctx, offset);
            offset += 8;
            offset += len;
            offset = align4(offset);
        }
        else if (token == FDT_NOP) {
            offset += 4;
        }
        else {
            break;
        }
    }
    
    // Check if next token is BEGIN_NODE (child)
    uint32_t token = read_struct_u32(ctx, offset);
    if (token != FDT_BEGIN_NODE) {
        return FDT_ERR_NOT_FOUND;
    }
    
    childOut->ctx = ctx;
    childOut->offset = offset;
    childOut->name = reinterpret_cast<const char*>(
        ctx->dtb + ctx->structOffset + offset + 4);
    childOut->depth = node->depth + 1;
    
    return FDT_OK;
}

FdtStatus get_next_sibling(const FdtNode* node, FdtNode* siblingOut)
{
    if (node == nullptr || node->ctx == nullptr || siblingOut == nullptr) {
        return FDT_ERR_INVALID;
    }
    
    const FdtContext* ctx = node->ctx;
    
    // Skip to end of current node
    uint32_t offset = skip_node(ctx, node->offset + 4);
    
    // Check if next token is BEGIN_NODE (sibling)
    uint32_t token = read_struct_u32(ctx, offset);
    if (token != FDT_BEGIN_NODE) {
        return FDT_ERR_NOT_FOUND;
    }
    
    siblingOut->ctx = ctx;
    siblingOut->offset = offset;
    siblingOut->name = reinterpret_cast<const char*>(
        ctx->dtb + ctx->structOffset + offset + 4);
    siblingOut->depth = node->depth;
    
    return FDT_OK;
}

FdtStatus get_parent(const FdtNode* node, FdtNode* parentOut)
{
    /*
     * STUB: Get parent node
     * 
     * Full implementation would track parent offsets during traversal
     * or re-walk from root to find parent.
     */
    
    if (node == nullptr || parentOut == nullptr) {
        return FDT_ERR_INVALID;
    }
    
    kernel::serial::puts("[FDT] get_parent: stub implementation\n");
    return FDT_ERR_NOT_FOUND;
}

const char* get_node_name(const FdtNode* node)
{
    if (node == nullptr) return "";
    return node->name;
}

const char* get_unit_address(const FdtNode* node)
{
    if (node == nullptr || node->name == nullptr) return nullptr;
    
    const char* at = node->name;
    while (*at && *at != '@') ++at;
    
    if (*at == '@') return at + 1;
    return nullptr;
}

FdtStatus get_path(const FdtNode* node, char* buffer, size_t bufferSize)
{
    /*
     * STUB: Get full path
     * 
     * Full implementation would walk up to root and build path.
     */
    
    if (node == nullptr || buffer == nullptr || bufferSize == 0) {
        return FDT_ERR_INVALID;
    }
    
    buffer[0] = '/';
    buffer[1] = '\0';
    
    return FDT_OK;
}

// ================================================================
// Property Access
// ================================================================

FdtStatus get_property(const FdtNode* node, const char* name, FdtPropRef* propOut)
{
    if (node == nullptr || node->ctx == nullptr || name == nullptr || propOut == nullptr) {
        return FDT_ERR_INVALID;
    }
    
    const FdtContext* ctx = node->ctx;
    uint32_t offset = node->offset + 4;  // Skip BEGIN_NODE
    
    // Skip node name
    while (offset < ctx->structSize && 
           ctx->dtb[ctx->structOffset + offset] != '\0') {
        ++offset;
    }
    ++offset;
    offset = align4(offset);
    
    // Search properties
    while (offset < ctx->structSize) {
        uint32_t token = read_struct_u32(ctx, offset);
        if (token != FDT_PROP) break;
        
        offset += 4;
        uint32_t len = read_struct_u32(ctx, offset);
        uint32_t nameoff = read_struct_u32(ctx, offset + 4);
        offset += 8;
        
        const char* propName = get_string(ctx, nameoff);
        if (strcmp(propName, name) == 0) {
            propOut->name = propName;
            propOut->value = ctx->dtb + ctx->structOffset + offset;
            propOut->length = len;
            return FDT_OK;
        }
        
        offset += len;
        offset = align4(offset);
    }
    
    return FDT_ERR_NOT_FOUND;
}

bool has_property(const FdtNode* node, const char* name)
{
    FdtPropRef prop;
    return get_property(node, name, &prop) == FDT_OK;
}

FdtStatus get_first_property(const FdtNode* node, FdtPropRef* propOut)
{
    if (node == nullptr || node->ctx == nullptr || propOut == nullptr) {
        return FDT_ERR_INVALID;
    }
    
    const FdtContext* ctx = node->ctx;
    uint32_t offset = node->offset + 4;
    
    // Skip node name
    while (offset < ctx->structSize && 
           ctx->dtb[ctx->structOffset + offset] != '\0') {
        ++offset;
    }
    ++offset;
    offset = align4(offset);
    
    uint32_t token = read_struct_u32(ctx, offset);
    if (token != FDT_PROP) {
        return FDT_ERR_NOT_FOUND;
    }
    
    offset += 4;
    uint32_t len = read_struct_u32(ctx, offset);
    uint32_t nameoff = read_struct_u32(ctx, offset + 4);
    offset += 8;
    
    propOut->name = get_string(ctx, nameoff);
    propOut->value = ctx->dtb + ctx->structOffset + offset;
    propOut->length = len;
    
    return FDT_OK;
}

FdtStatus get_next_property(const FdtNode* node, const FdtPropRef* current, 
                            FdtPropRef* nextOut)
{
    /*
     * STUB: Get next property (iteration)
     * 
     * Full implementation would use offset stored in property.
     */
    
    (void)node;
    (void)current;
    (void)nextOut;
    
    return FDT_ERR_NOT_FOUND;
}

// ================================================================
// Property Value Helpers
// ================================================================

FdtStatus read_u32(const FdtPropRef* prop, uint32_t* valueOut)
{
    if (prop == nullptr || valueOut == nullptr || prop->length < 4) {
        return FDT_ERR_INVALID;
    }
    
    const uint8_t* p = prop->value;
    *valueOut = (static_cast<uint32_t>(p[0]) << 24) |
                (static_cast<uint32_t>(p[1]) << 16) |
                (static_cast<uint32_t>(p[2]) << 8) |
                static_cast<uint32_t>(p[3]);
    
    return FDT_OK;
}

FdtStatus read_u64(const FdtPropRef* prop, uint64_t* valueOut)
{
    if (prop == nullptr || valueOut == nullptr || prop->length < 8) {
        return FDT_ERR_INVALID;
    }
    
    const uint8_t* p = prop->value;
    *valueOut = (static_cast<uint64_t>(p[0]) << 56) |
                (static_cast<uint64_t>(p[1]) << 48) |
                (static_cast<uint64_t>(p[2]) << 40) |
                (static_cast<uint64_t>(p[3]) << 32) |
                (static_cast<uint64_t>(p[4]) << 24) |
                (static_cast<uint64_t>(p[5]) << 16) |
                (static_cast<uint64_t>(p[6]) << 8) |
                static_cast<uint64_t>(p[7]);
    
    return FDT_OK;
}

int read_u32_array(const FdtPropRef* prop, uint32_t* arrayOut, int maxCount)
{
    if (prop == nullptr || arrayOut == nullptr) return 0;
    
    int count = prop->length / 4;
    if (count > maxCount) count = maxCount;
    
    for (int i = 0; i < count; ++i) {
        const uint8_t* p = prop->value + i * 4;
        arrayOut[i] = (static_cast<uint32_t>(p[0]) << 24) |
                      (static_cast<uint32_t>(p[1]) << 16) |
                      (static_cast<uint32_t>(p[2]) << 8) |
                      static_cast<uint32_t>(p[3]);
    }
    
    return count;
}

const char* read_string(const FdtPropRef* prop)
{
    if (prop == nullptr || prop->length == 0) return "";
    return reinterpret_cast<const char*>(prop->value);
}

int read_string_list(const FdtPropRef* prop, const char** stringsOut, int maxCount)
{
    if (prop == nullptr || stringsOut == nullptr) return 0;
    
    int count = 0;
    const char* p = reinterpret_cast<const char*>(prop->value);
    const char* end = p + prop->length;
    
    while (p < end && count < maxCount) {
        stringsOut[count++] = p;
        while (p < end && *p) ++p;
        ++p;
    }
    
    return count;
}

bool string_equals(const FdtPropRef* prop, const char* str)
{
    if (prop == nullptr || str == nullptr) return false;
    return strcmp(read_string(prop), str) == 0;
}

bool string_list_contains(const FdtPropRef* prop, const char* str)
{
    if (prop == nullptr || str == nullptr) return false;
    
    const char* p = reinterpret_cast<const char*>(prop->value);
    const char* end = p + prop->length;
    
    while (p < end) {
        if (strcmp(p, str) == 0) return true;
        while (p < end && *p) ++p;
        ++p;
    }
    
    return false;
}

// ================================================================
// Reg Property Parsing
// ================================================================

int read_reg(const FdtNode* node, const FdtCellInfo* cellInfo,
             FdtMemoryRegion* regionsOut, int maxCount)
{
    if (node == nullptr || cellInfo == nullptr || regionsOut == nullptr) return 0;
    
    FdtPropRef prop;
    if (get_property(node, "reg", &prop) != FDT_OK) return 0;
    
    int addrCells = cellInfo->addressCells;
    int sizeCells = cellInfo->sizeCells;
    int cellsPerEntry = addrCells + sizeCells;
    
    if (cellsPerEntry == 0) return 0;
    
    int numEntries = (prop.length / 4) / cellsPerEntry;
    if (numEntries > maxCount) numEntries = maxCount;
    
    const uint8_t* p = prop.value;
    
    for (int i = 0; i < numEntries; ++i) {
        uint64_t addr = 0;
        for (int j = 0; j < addrCells; ++j) {
            addr = (addr << 32) | fdt32_to_cpu(*reinterpret_cast<const uint32_t*>(p));
            p += 4;
        }
        
        uint64_t size = 0;
        for (int j = 0; j < sizeCells; ++j) {
            size = (size << 32) | fdt32_to_cpu(*reinterpret_cast<const uint32_t*>(p));
            p += 4;
        }
        
        regionsOut[i].base = addr;
        regionsOut[i].size = size;
    }
    
    return numEntries;
}

FdtStatus get_cell_info(const FdtNode* node, FdtCellInfo* infoOut)
{
    if (node == nullptr || infoOut == nullptr) {
        return FDT_ERR_INVALID;
    }
    
    // Defaults
    infoOut->addressCells = 2;
    infoOut->sizeCells = 1;
    
    FdtPropRef prop;
    if (get_property(node, "#address-cells", &prop) == FDT_OK) {
        read_u32(&prop, reinterpret_cast<uint32_t*>(&infoOut->addressCells));
    }
    if (get_property(node, "#size-cells", &prop) == FDT_OK) {
        read_u32(&prop, reinterpret_cast<uint32_t*>(&infoOut->sizeCells));
    }
    
    return FDT_OK;
}

// ================================================================
// Common Node Types
// ================================================================

int get_memory_regions(const FdtContext* ctx, FdtMemoryRegion* regionsOut, int maxCount)
{
    if (ctx == nullptr || regionsOut == nullptr) return 0;
    
    FdtNode memNode;
    if (find_node(ctx, "/memory", &memNode) != FDT_OK) {
        // Try memory@...
        if (find_compatible(ctx, "memory", &memNode) != FDT_OK) {
            return 0;
        }
    }
    
    // Get cell info from root
    FdtNode root;
    get_root(ctx, &root);
    FdtCellInfo cellInfo;
    get_cell_info(&root, &cellInfo);
    
    return read_reg(&memNode, &cellInfo, regionsOut, maxCount);
}

int get_reserved_memory(const FdtContext* ctx, FdtMemoryRegion* regionsOut, int maxCount)
{
    /*
     * STUB: Get reserved memory regions
     * 
     * Full implementation would parse /reserved-memory node
     * and its children.
     */
    
    (void)ctx;
    (void)regionsOut;
    (void)maxCount;
    
    return 0;
}

FdtStatus get_chosen(const FdtContext* ctx, FdtNode* nodeOut)
{
    return find_node(ctx, "/chosen", nodeOut);
}

const char* get_bootargs(const FdtContext* ctx)
{
    FdtNode chosen;
    if (get_chosen(ctx, &chosen) != FDT_OK) return "";
    
    FdtPropRef prop;
    if (get_property(&chosen, "bootargs", &prop) != FDT_OK) return "";
    
    return read_string(&prop);
}

const char* get_stdout_path(const FdtContext* ctx)
{
    FdtNode chosen;
    if (get_chosen(ctx, &chosen) != FDT_OK) return "";
    
    FdtPropRef prop;
    if (get_property(&chosen, "stdout-path", &prop) != FDT_OK) return "";
    
    return read_string(&prop);
}

FdtStatus get_cpus(const FdtContext* ctx, FdtNode* nodeOut)
{
    return find_node(ctx, "/cpus", nodeOut);
}

int count_cpus(const FdtContext* ctx)
{
    FdtNode cpus;
    if (get_cpus(ctx, &cpus) != FDT_OK) return 0;
    
    int count = 0;
    FdtNode cpu;
    if (get_first_child(&cpus, &cpu) == FDT_OK) {
        do {
            // Check if it's a CPU node (has "device_type" = "cpu")
            FdtPropRef prop;
            if (get_property(&cpu, "device_type", &prop) == FDT_OK) {
                if (string_equals(&prop, "cpu")) {
                    ++count;
                }
            }
        } while (get_next_sibling(&cpu, &cpu) == FDT_OK);
    }
    
    return count;
}

// ================================================================
// Bus/Device Discovery
// ================================================================

FdtStatus for_each_compatible(const FdtContext* ctx, const char* compatible,
                              FdtDeviceCallback callback, void* context)
{
    /*
     * STUB: Enumerate all matching nodes
     * 
     * Full implementation would walk tree and call callback
     * for each node with matching compatible string.
     */
    
    if (ctx == nullptr || compatible == nullptr || callback == nullptr) {
        return FDT_ERR_INVALID;
    }
    
    // For now, just find first match
    FdtNode node;
    if (find_compatible(ctx, compatible, &node) == FDT_OK) {
        callback(&node, context);
    }
    
    return FDT_OK;
}

FdtStatus for_each_child(const FdtNode* parent, FdtDeviceCallback callback, void* context)
{
    if (parent == nullptr || callback == nullptr) {
        return FDT_ERR_INVALID;
    }
    
    FdtNode child;
    if (get_first_child(parent, &child) != FDT_OK) {
        return FDT_OK;  // No children is not an error
    }
    
    do {
        if (!callback(&child, context)) {
            break;  // Callback requested stop
        }
    } while (get_next_sibling(&child, &child) == FDT_OK);
    
    return FDT_OK;
}

// ================================================================
// Address Translation
// ================================================================

FdtStatus translate_address(const FdtNode* node, uint64_t childAddr, uint64_t* parentAddr)
{
    /*
     * STUB: Translate address using ranges property
     * 
     * Full implementation would parse "ranges" property and
     * perform address translation.
     */
    
    if (node == nullptr || parentAddr == nullptr) {
        return FDT_ERR_INVALID;
    }
    
    // For now, assume 1:1 mapping
    *parentAddr = childAddr;
    return FDT_OK;
}

FdtStatus translate_to_physical(const FdtNode* node, uint64_t localAddr, uint64_t* physAddr)
{
    // Same as translate_address for now
    return translate_address(node, localAddr, physAddr);
}

// ================================================================
// Debug/Status
// ================================================================

void print_summary(const FdtContext* ctx)
{
    if (ctx == nullptr || !ctx->valid) {
        kernel::serial::puts("[FDT] No valid device tree\n");
        return;
    }
    
    kernel::serial::puts("[FDT] Device Tree Summary:\n");
    kernel::serial::puts("  Version:     ");
    kernel::serial::put_hex32(ctx->version);
    kernel::serial::puts("\n  Total size:  ");
    kernel::serial::put_hex32(ctx->totalSize);
    kernel::serial::puts(" bytes\n");
    kernel::serial::puts("  Struct size: ");
    kernel::serial::put_hex32(ctx->structSize);
    kernel::serial::puts("\n  String size: ");
    kernel::serial::put_hex32(ctx->stringsSize);
    kernel::serial::putc('\n');
    
    // Print boot args if available
    const char* bootargs = get_bootargs(ctx);
    if (bootargs && bootargs[0]) {
        kernel::serial::puts("  Boot args:   ");
        kernel::serial::puts(bootargs);
        kernel::serial::putc('\n');
    }
    
    // Print CPU count
    int cpuCount = count_cpus(ctx);
    kernel::serial::puts("  CPUs:        ");
    kernel::serial::put_hex32(cpuCount);
    kernel::serial::putc('\n');
    
    // Print memory
    FdtMemoryRegion mem[4];
    int memCount = get_memory_regions(ctx, mem, 4);
    for (int i = 0; i < memCount; ++i) {
        kernel::serial::puts("  Memory:      ");
        kernel::serial::put_hex64(mem[i].base);
        kernel::serial::puts(" - ");
        kernel::serial::put_hex64(mem[i].base + mem[i].size);
        kernel::serial::putc('\n');
    }
}

void print_node(const FdtNode* node)
{
    if (node == nullptr) return;
    
    kernel::serial::puts("[FDT] Node: ");
    kernel::serial::puts(node->name[0] ? node->name : "/");
    kernel::serial::putc('\n');
    
    FdtPropRef prop;
    if (get_first_property(node, &prop) == FDT_OK) {
        do {
            kernel::serial::puts("  ");
            kernel::serial::puts(prop.name);
            kernel::serial::puts(" (");
            kernel::serial::put_hex32(prop.length);
            kernel::serial::puts(" bytes)\n");
        } while (get_next_property(node, &prop, &prop) == FDT_OK);
    }
}

void dump_tree(const FdtContext* ctx)
{
    if (ctx == nullptr || !ctx->valid) {
        kernel::serial::puts("[FDT] No valid device tree to dump\n");
        return;
    }
    
    kernel::serial::puts("[FDT] Tree dump:\n");
    
    // Walk structure block
    uint32_t offset = 0;
    int depth = 0;
    
    while (offset < ctx->structSize) {
        uint32_t token = read_struct_u32(ctx, offset);
        
        if (token == FDT_BEGIN_NODE) {
            offset += 4;
            const char* name = reinterpret_cast<const char*>(
                ctx->dtb + ctx->structOffset + offset);
            
            // Indent
            for (int i = 0; i < depth; ++i) kernel::serial::puts("  ");
            kernel::serial::puts(name[0] ? name : "/");
            kernel::serial::puts(" {\n");
            
            while (offset < ctx->structSize && 
                   ctx->dtb[ctx->structOffset + offset] != '\0') {
                ++offset;
            }
            ++offset;
            offset = align4(offset);
            ++depth;
        }
        else if (token == FDT_END_NODE) {
            offset += 4;
            --depth;
            for (int i = 0; i < depth; ++i) kernel::serial::puts("  ");
            kernel::serial::puts("}\n");
        }
        else if (token == FDT_PROP) {
            offset += 4;
            uint32_t len = read_struct_u32(ctx, offset);
            uint32_t nameoff = read_struct_u32(ctx, offset + 4);
            offset += 8;
            
            for (int i = 0; i < depth; ++i) kernel::serial::puts("  ");
            kernel::serial::puts("  ");
            kernel::serial::puts(get_string(ctx, nameoff));
            kernel::serial::puts(";\n");
            
            offset += len;
            offset = align4(offset);
        }
        else if (token == FDT_NOP) {
            offset += 4;
        }
        else if (token == FDT_END) {
            break;
        }
        else {
            offset += 4;
        }
    }
}

const char* status_string(FdtStatus status)
{
    switch (status) {
        case FDT_OK:              return "OK";
        case FDT_ERR_INVALID:     return "Invalid parameter";
        case FDT_ERR_BAD_MAGIC:   return "Bad magic number";
        case FDT_ERR_BAD_VERSION: return "Unsupported version";
        case FDT_ERR_CORRUPT:     return "Corrupted structure";
        case FDT_ERR_NOT_FOUND:   return "Not found";
        case FDT_ERR_TRUNCATED:   return "Truncated";
        case FDT_ERR_NO_MEMORY:   return "Buffer too small";
        default:                  return "Unknown error";
    }
}

} // namespace fdt
} // namespace kernel

