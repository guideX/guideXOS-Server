//
// AMD64 lowering for the bounded bootstrap compiler IR.
//

#include "compiler_backend.h"

#include "../../../sdk/include/guidexos/app.h"

namespace kernel {
namespace compiler {
namespace amd64 {
namespace {

static bool is_gx_main(const char* name)
{
    if (!name) return false;
    const char expected[] = "gx_main";
    for (uint32_t i = 0; expected[i] || name[i]; ++i) if (expected[i] != name[i]) return false;
    return true;
}

static bool align16(uint32_t value, uint32_t* output)
{
    if (!output || value > 0xFFFFFFF0U) return false;
    *output = (value + 15U) & ~15U;
    return true;
}

static int32_t slot_displacement(uint32_t slot)
{
    return -static_cast<int32_t>(4U * (slot + 1U));
}

static int32_t local_displacement(uint16_t slot)
{
    return slot_displacement(slot);
}

static int32_t temporary_displacement(const FrameLayout& frame, uint16_t slot)
{
    const uint32_t base = frame.variableBytes + (frame.contextDisplacement != 0 ? 8U : 0U) + 8U;
    return -static_cast<int32_t>(base + static_cast<uint32_t>(slot) * 4U);
}

static int32_t pointer_temporary_displacement(const FrameLayout& frame, uint16_t slot)
{
    const uint32_t base = frame.variableBytes + (frame.contextDisplacement != 0 ? 8U : 0U) + 8U +
        frame.temporaryBytes;
    return -static_cast<int32_t>(base +
        (static_cast<uint32_t>(slot) + 1U) * COMPILER_POINTER_DESCRIPTOR_BYTES);
}

class Emitter {
public:
    Emitter(uint8_t* output, uint32_t capacity,
            RelocationRecord* relocations = nullptr,
            uint32_t relocationCapacity = 0,
            uint32_t* relocationCount = nullptr)
        : m_output(output), m_capacity(capacity), m_offset(0), m_labelCount(0), m_fixupCount(0),
          m_loopDepth(0), m_rspMod16(8), m_temporaryDepth(0), m_maxTemporaryDepth(0),
          m_pointerTemporaryDepth(0), m_maxPointerTemporaryDepth(0),
          m_transientBytes(0), m_maxTransientBytes(0), m_relocations(relocations),
          m_relocationCapacity(relocationCapacity), m_relocationCount(relocationCount)
    {
        for (uint32_t i = 0; i < COMPILER_MAX_BRANCH_LABELS; ++i) m_labels[i] = {};
        for (uint32_t i = 0; i < COMPILER_MAX_BRANCH_FIXUPS; ++i) m_fixups[i] = {};
    }

    bool bytes(const uint8_t* values, uint32_t count)
    {
        if (!values || m_offset > m_capacity || count > m_capacity - m_offset) return false;
        for (uint32_t i = 0; i < count; ++i) m_output[m_offset++] = values[i];
        return true;
    }
    bool byte(uint8_t value) { return bytes(&value, 1); }
    bool u32(uint32_t value)
    {
        uint8_t bytesValue[4] = {
            static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
            static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
        return bytes(bytesValue, sizeof(bytesValue));
    }
    bool u64(uint64_t value)
    {
        uint8_t bytesValue[8] = {};
        for (uint32_t i = 0; i < 8; ++i) bytesValue[i] = static_cast<uint8_t>(value >> (i * 8));
        return bytes(bytesValue, sizeof(bytesValue));
    }
    uint32_t size() const { return m_offset; }

    void begin_function()
    {
        m_loopDepth = 0;
        m_rspMod16 = 8;
        m_temporaryDepth = 0;
        m_maxTemporaryDepth = 0;
        m_pointerTemporaryDepth = 0;
        m_maxPointerTemporaryDepth = 0;
        m_transientBytes = 0;
        m_maxTransientBytes = 0;
    }

    bool mov_eax_imm32(uint32_t value) { return byte(0xB8) && u32(value); }
    bool mov_reg_mem32(uint8_t reg, int32_t displacement)
    {
        if (reg < 8) {
            return byte(0x8B) && byte(static_cast<uint8_t>(0x85U | (reg << 3))) &&
                   u32(static_cast<uint32_t>(displacement));
        }
        return byte(0x44) && byte(0x8B) && byte(static_cast<uint8_t>(0x85U | ((reg - 8U) << 3))) &&
               u32(static_cast<uint32_t>(displacement));
    }
    bool mov_eax_local(int32_t displacement) { return mov_reg_mem32(0, displacement); }
    bool mov_eax_rax() { static const uint8_t v[] = {0x8B, 0x02}; return bytes(v, sizeof(v)); }
    bool mov_eax_ptr_rax() { static const uint8_t v[] = {0x8B, 0x00}; return bytes(v, sizeof(v)); }
    bool mov_rax_eax() { static const uint8_t v[] = {0x89, 0x02}; return bytes(v, sizeof(v)); }
    bool mov_eax_ecx() { static const uint8_t v[] = {0x89, 0xC8}; return bytes(v, sizeof(v)); }
    bool mov_local_eax(int32_t displacement)
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x89\x85"), 2) && u32(static_cast<uint32_t>(displacement));
    }
    bool mov_local_rax(int32_t displacement)
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x48\x89\x85"), 3) &&
               u32(static_cast<uint32_t>(displacement));
    }
    bool mov_rax_local(int32_t displacement)
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x48\x8B\x85"), 3) &&
               u32(static_cast<uint32_t>(displacement));
    }
    bool mov_reg_mem64(uint8_t reg, int32_t displacement)
    {
        if (reg < 8) {
            return bytes(reinterpret_cast<const uint8_t*>("\x48\x8B"), 2) &&
                   byte(static_cast<uint8_t>(0x85U | (reg << 3))) &&
                   u32(static_cast<uint32_t>(displacement));
        }
        return bytes(reinterpret_cast<const uint8_t*>("\x4C\x8B"), 2) &&
               byte(static_cast<uint8_t>(0x85U | ((reg - 8U) << 3))) &&
               u32(static_cast<uint32_t>(displacement));
    }
    bool mov_local_reg64(uint8_t reg, int32_t displacement)
    {
        if (reg < 8) {
            return bytes(reinterpret_cast<const uint8_t*>("\x48\x89"), 2) &&
                   byte(static_cast<uint8_t>(0x85U | (reg << 3))) &&
                   u32(static_cast<uint32_t>(displacement));
        }
        return bytes(reinterpret_cast<const uint8_t*>("\x4C\x89"), 2) &&
               byte(static_cast<uint8_t>(0x85U | ((reg - 8U) << 3))) &&
               u32(static_cast<uint32_t>(displacement));
    }
    bool mov_rax_reg64(uint8_t reg)
    {
        if (reg < 8) return byte(0x48) && byte(0x89) && byte(static_cast<uint8_t>(0xC0U | (reg << 3)));
        return byte(0x4C) && byte(0x89) && byte(static_cast<uint8_t>(0xC0U | ((reg - 8U) << 3)));
    }
    bool lea_rax_local(int32_t displacement)
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x48\x8D\x85"), 3) &&
               u32(static_cast<uint32_t>(displacement));
    }
    bool lea_reg_local64(uint8_t reg, int32_t displacement)
    {
        if (reg < 8) {
            return bytes(reinterpret_cast<const uint8_t*>("\x48\x8D"), 2) &&
                   byte(static_cast<uint8_t>(0x85U | (reg << 3))) &&
                   u32(static_cast<uint32_t>(displacement));
        }
        return bytes(reinterpret_cast<const uint8_t*>("\x4C\x8D"), 2) &&
               byte(static_cast<uint8_t>(0x85U | ((reg - 8U) << 3))) &&
               u32(static_cast<uint32_t>(displacement));
    }
    bool mov_reg_ptr64(uint8_t reg, uint8_t base, uint8_t displacement)
    {
        if (base >= 8) return false;
        const uint8_t rex = static_cast<uint8_t>(reg >= 8 ? 0x4CU : 0x48U);
        return byte(rex) && byte(0x8B) &&
               byte(static_cast<uint8_t>(0x40U | ((reg & 7U) << 3) | (base & 7U))) &&
               byte(displacement);
    }
    bool mov_reg_ptr32(uint8_t reg, uint8_t base, uint8_t displacement)
    {
        if (base >= 8) return false;
        const uint8_t rex = static_cast<uint8_t>(reg >= 8 ? 0x44U : 0x40U);
        return byte(rex) && byte(0x8B) &&
               byte(static_cast<uint8_t>(0x40U | ((reg & 7U) << 3) | (base & 7U))) &&
               byte(displacement);
    }
    bool mov_reg_local32(uint8_t reg, int32_t displacement)
    {
        if (reg < 8) {
            return byte(0x89) && byte(static_cast<uint8_t>(0x85U | (reg << 3))) &&
                   u32(static_cast<uint32_t>(displacement));
        }
        return byte(0x44) && byte(0x89) && byte(static_cast<uint8_t>(0x85U | ((reg - 8U) << 3))) &&
               u32(static_cast<uint32_t>(displacement));
    }
    bool mov_context_local(int32_t displacement)
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x48\x89\x8D"), 3) && u32(static_cast<uint32_t>(displacement));
    }
    bool mov_rcx_context_local(int32_t displacement)
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x48\x8B\x8D"), 3) && u32(static_cast<uint32_t>(displacement));
    }
    bool mov_rdx_imm64(uint64_t address)
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x48\xBA"), 2) && u64(address);
    }
    bool movsxd_rax_eax() { static const uint8_t v[] = {0x48, 0x63, 0xC0}; return bytes(v, sizeof(v)); }
    bool lea_rax_rax_disp32(uint32_t displacement)
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x48\x8D\x80"), 3) && u32(displacement);
    }
    bool lea_rax_rdx_disp32(uint32_t displacement)
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x48\x8D\x82"), 3) && u32(displacement);
    }
    bool lea_rdx_local_rax(int32_t displacement)
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x48\x8D\x94\x85"), 4) &&
               u32(static_cast<uint32_t>(displacement));
    }
    bool lea_rdx_global_rax()
    {
        static const uint8_t v[] = {0x48, 0x8D, 0x14, 0x82};
        return bytes(v, sizeof(v));
    }
    bool load_host_log()
    {
        static const uint8_t first[] = {0x48, 0x8B, 0x41, static_cast<uint8_t>(offsetof(gx_app_context, host))};
        static const uint8_t second[] = {0x48, 0x8B, 0x40, static_cast<uint8_t>(offsetof(gx_host_calls, log))};
        return bytes(first, sizeof(first)) && bytes(second, sizeof(second));
    }
    bool add_eax_ecx() { static const uint8_t v[] = {0x01, 0xC8}; return bytes(v, sizeof(v)); }
    bool sub_eax_ecx() { static const uint8_t v[] = {0x29, 0xC8}; return bytes(v, sizeof(v)); }
    bool imul_eax_ecx() { static const uint8_t v[] = {0x0F, 0xAF, 0xC1}; return bytes(v, sizeof(v)); }
    bool neg_eax() { static const uint8_t v[] = {0xF7, 0xD8}; return bytes(v, sizeof(v)); }
    bool cmp_ecx_eax() { static const uint8_t v[] = {0x39, 0xC1}; return bytes(v, sizeof(v)); }
    bool setcc(uint8_t condition) { return byte(0x0F) && byte(condition) && byte(0xC0); }
    bool movzx_eax_al() { static const uint8_t v[] = {0x0F, 0xB6, 0xC0}; return bytes(v, sizeof(v)); }
    bool test_eax_eax() { static const uint8_t v[] = {0x85, 0xC0}; return bytes(v, sizeof(v)); }
    bool test_rax_rax() { static const uint8_t v[] = {0x48, 0x85, 0xC0}; return bytes(v, sizeof(v)); }
    bool test_edx_imm32(uint32_t value)
    {
        return bytes(reinterpret_cast<const uint8_t*>("\xF7\xC2"), 2) && u32(value);
    }
    bool cmp_rdx_rcx() { static const uint8_t v[] = {0x48, 0x39, 0xCA}; return bytes(v, sizeof(v)); }
    bool cmp_rax_rcx() { static const uint8_t v[] = {0x48, 0x39, 0xC8}; return bytes(v, sizeof(v)); }
    bool cmp_rdx_r9() { static const uint8_t v[] = {0x4C, 0x39, 0xCA}; return bytes(v, sizeof(v)); }
    bool cmp_rdx_r8() { static const uint8_t v[] = {0x4C, 0x39, 0xC2}; return bytes(v, sizeof(v)); }
    bool sub_rdx_rcx() { static const uint8_t v[] = {0x48, 0x29, 0xCA}; return bytes(v, sizeof(v)); }
    bool cmp_rax_r9() { static const uint8_t v[] = {0x4C, 0x39, 0xC8}; return bytes(v, sizeof(v)); }
    bool cmp_rax_r10() { static const uint8_t v[] = {0x4C, 0x39, 0xD0}; return bytes(v, sizeof(v)); }
    bool cmp_rcx_ptr64(uint8_t displacement)
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x48\x3B"), 2) &&
               byte(static_cast<uint8_t>(0x40U | (1U << 3))) && byte(displacement);
    }
    bool cmp_r8d_ptr32(uint8_t displacement)
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x44\x3B"), 2) &&
               byte(0x40) && byte(displacement);
    }
    bool cmp_r8d_local32(int32_t displacement)
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x44\x3B\x85"), 3) &&
               u32(static_cast<uint32_t>(displacement));
    }
    bool add_rax_r10() { static const uint8_t v[] = {0x4C, 0x01, 0xD0}; return bytes(v, sizeof(v)); }
    bool sub_rax_r10() { static const uint8_t v[] = {0x4C, 0x29, 0xD0}; return bytes(v, sizeof(v)); }
    bool neg_rdx() { static const uint8_t v[] = {0x48, 0xF7, 0xDA}; return bytes(v, sizeof(v)); }
    bool neg_r10() { static const uint8_t v[] = {0x49, 0xF7, 0xDA}; return bytes(v, sizeof(v)); }
    bool test_r10_r10() { static const uint8_t v[] = {0x4D, 0x85, 0xD2}; return bytes(v, sizeof(v)); }
    bool test_rcx_imm32(uint32_t value)
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x48\xF7\xC1"), 3) && u32(value);
    }
    bool test_r8d_imm32(uint32_t value)
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x41\xF7\xC0"), 3) && u32(value);
    }
    bool movsxd_rdx_eax() { static const uint8_t v[] = {0x48, 0x63, 0xD0}; return bytes(v, sizeof(v)); }
    bool shl_rdx_2() { static const uint8_t v[] = {0x48, 0xC1, 0xE2, 0x02}; return bytes(v, sizeof(v)); }
    bool mov_r9_rax() { static const uint8_t v[] = {0x49, 0x89, 0xC1}; return bytes(v, sizeof(v)); }
    bool add_r9_r8() { static const uint8_t v[] = {0x4D, 0x01, 0xC1}; return bytes(v, sizeof(v)); }
    bool cmp_r8d_imm32(uint32_t value)
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x41\x81\xF8"), 3) && u32(value);
    }
    bool sub_r8d_imm32(uint32_t value)
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x41\x81\xE8"), 3) && u32(value);
    }
    bool mov_local_rax64(int32_t displacement) { return mov_local_reg64(0, displacement); }
    bool mov_local_rdx64(int32_t displacement) { return mov_local_reg64(2, displacement); }
    bool mov_local_r10_64(int32_t displacement) { return mov_local_reg64(10, displacement); }
    bool mov_rax_local64(int32_t displacement) { return mov_reg_mem64(0, displacement); }
    bool mov_rdx_ptr64(uint8_t displacement) { return mov_reg_ptr64(2, 0, displacement); }
    bool mov_rcx_ptr64(uint8_t displacement) { return mov_reg_ptr64(1, 0, displacement); }
    bool mov_rax_ptr64(uint8_t displacement) { return mov_reg_ptr64(0, 0, displacement); }
    bool mov_r10_ptr64(uint8_t displacement) { return mov_reg_ptr64(10, 0, displacement); }
    bool mov_r9_rcx() { static const uint8_t v[] = {0x49, 0x89, 0xC9}; return bytes(v, sizeof(v)); }
    bool mov_r10_rdx() { static const uint8_t v[] = {0x49, 0x89, 0xD2}; return bytes(v, sizeof(v)); }
    bool mov_r8d_ptr32(uint8_t displacement) { return mov_reg_ptr32(8, 0, displacement); }
    bool mov_ecx_ptr32(uint8_t displacement) { return mov_reg_ptr32(1, 0, displacement); }
    bool push_rax()
    {
        if (!byte(0x50) || m_transientBytes > COMPILER_MAX_TRANSIENT_STACK_BYTES - 8U) return false;
        m_transientBytes += 8U;
        if (m_transientBytes > m_maxTransientBytes) m_maxTransientBytes = m_transientBytes;
        m_rspMod16 = static_cast<uint8_t>((m_rspMod16 + 8U) & 0xFU);
        return true;
    }
    bool pop_rcx()
    {
        if (m_transientBytes < 8U || !byte(0x59)) return false;
        m_transientBytes -= 8U;
        m_rspMod16 = static_cast<uint8_t>((m_rspMod16 + 8U) & 0xFU);
        return true;
    }
    bool sub_rsp(uint32_t value)
    {
        const bool result = value <= 127U
            ? bytes(reinterpret_cast<const uint8_t*>("\x48\x83\xEC"), 3) && byte(static_cast<uint8_t>(value))
            : bytes(reinterpret_cast<const uint8_t*>("\x48\x81\xEC"), 3) && u32(value);
        if (result) m_rspMod16 = static_cast<uint8_t>((m_rspMod16 + 16U - (value & 0xFU)) & 0xFU);
        return result;
    }
    bool add_rsp(uint32_t value)
    {
        const bool result = value <= 127U
            ? bytes(reinterpret_cast<const uint8_t*>("\x48\x83\xC4"), 3) && byte(static_cast<uint8_t>(value))
            : bytes(reinterpret_cast<const uint8_t*>("\x48\x81\xC4"), 3) && u32(value);
        if (result) m_rspMod16 = static_cast<uint8_t>((m_rspMod16 + (value & 0xFU)) & 0xFU);
        return result;
    }
    bool prologue(uint32_t frameBytes)
    {
        if (!byte(0x55) || !bytes(reinterpret_cast<const uint8_t*>("\x48\x89\xE5"), 3)) return false;
        m_rspMod16 = static_cast<uint8_t>((m_rspMod16 + 8U) & 0xFU);
        return sub_rsp(frameBytes);
    }
    bool epilogue() { return bytes(reinterpret_cast<const uint8_t*>("\x48\x89\xEC\x5D\xC3"), 5); }
    bool call_rax() { return bytes(reinterpret_cast<const uint8_t*>("\xFF\xD0"), 2); }
    uint32_t call_stack_reserve() const { return 32U + static_cast<uint32_t>(m_rspMod16); }
    uint32_t max_transient_bytes() const { return m_maxTransientBytes; }
    bool initialize_runtime_state()
    {
        // R14D is the current generated activation depth.  R15D is zero on
        // success and becomes the bounded failure depth when the guard trips;
        // this keeps the failure status sticky while preserving the exact
        // depth observed by the trampoline after normal unwinding.  gx_main
        // is depth one.
        return bytes(reinterpret_cast<const uint8_t*>("\x41\xBE\x01\x00\x00\x00"), 6) &&
               bytes(reinterpret_cast<const uint8_t*>("\x45\x31\xFF"), 3);
    }
    bool cmp_r14d_imm32(uint32_t value)
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x41\x81\xFE"), 3) && u32(value);
    }
    bool cmp_eax_imm32(uint32_t value)
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x3D"), 1) && u32(value);
    }
    bool cmp_ecx_imm32(uint32_t value)
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x81\xF9"), 2) && u32(value);
    }
    bool inc_r14d() { return bytes(reinterpret_cast<const uint8_t*>("\x41\xFF\xC6"), 3); }
    bool dec_r14d() { return bytes(reinterpret_cast<const uint8_t*>("\x41\xFF\xCE"), 3); }
    bool set_runtime_failure(uint32_t status)
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x41\xBF"), 2) &&
               u32((COMPILER_MAX_RUNTIME_CALL_DEPTH << 8U) | status);
    }
    bool set_array_bounds_failure()
    {
        // Preserve the current activation depth in the high bits while the
        // low byte carries the distinct bounds status.
        return bytes(reinterpret_cast<const uint8_t*>("\x45\x89\xF7"), 3) &&
               bytes(reinterpret_cast<const uint8_t*>("\x41\xC1\xE7\x08"), 4) &&
               bytes(reinterpret_cast<const uint8_t*>("\x41\x83\xCF\x02"), 4);
    }
    bool set_invalid_pointer_failure()
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x45\x89\xF7"), 3) &&
               bytes(reinterpret_cast<const uint8_t*>("\x41\xC1\xE7\x08"), 4) &&
               bytes(reinterpret_cast<const uint8_t*>("\x41\x83\xCF\x03"), 4);
    }
    bool set_pointer_bounds_failure()
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x45\x89\xF7"), 3) &&
               bytes(reinterpret_cast<const uint8_t*>("\x41\xC1\xE7\x08"), 4) &&
               bytes(reinterpret_cast<const uint8_t*>("\x41\x83\xCF\x04"), 4);
    }
    bool test_r15d() { return bytes(reinterpret_cast<const uint8_t*>("\x45\x85\xFF"), 3); }

    bool acquire_temporary_slots(uint16_t count, uint16_t* base)
    {
        if (!base || count > COMPILER_MAX_TEMPORARY_SLOTS ||
            m_temporaryDepth > COMPILER_MAX_TEMPORARY_SLOTS - count) return false;
        *base = static_cast<uint16_t>(m_temporaryDepth);
        m_temporaryDepth += count;
        if (m_temporaryDepth > m_maxTemporaryDepth) m_maxTemporaryDepth = m_temporaryDepth;
        return true;
    }
    bool release_temporary_slots(uint16_t count)
    {
        if (count > m_temporaryDepth) return false;
        m_temporaryDepth -= count;
        return true;
    }
    bool acquire_pointer_temporary_slots(uint16_t count, uint16_t* base)
    {
        if (!base || count > COMPILER_MAX_POINTER_TEMPORARY_SLOTS ||
            m_pointerTemporaryDepth > COMPILER_MAX_POINTER_TEMPORARY_SLOTS - count) return false;
        *base = static_cast<uint16_t>(m_pointerTemporaryDepth);
        m_pointerTemporaryDepth += count;
        if (m_pointerTemporaryDepth > m_maxPointerTemporaryDepth)
            m_maxPointerTemporaryDepth = m_pointerTemporaryDepth;
        return true;
    }
    bool release_pointer_temporary_slots(uint16_t count)
    {
        if (count > m_pointerTemporaryDepth) return false;
        m_pointerTemporaryDepth -= count;
        return true;
    }

    bool push_loop_target(uint16_t continueLabel, uint16_t breakLabel)
    {
        if (continueLabel >= m_labelCount || breakLabel >= m_labelCount ||
            m_loopDepth >= COMPILER_MAX_LOOP_TARGET_DEPTH) return false;
        m_loopStack[m_loopDepth].continueLabel = continueLabel;
        m_loopStack[m_loopDepth].breakLabel = breakLabel;
        ++m_loopDepth;
        return true;
    }
    bool pop_loop_target()
    {
        if (m_loopDepth == 0) return false;
        --m_loopDepth;
        m_loopStack[m_loopDepth] = {};
        return true;
    }
    bool current_break_target(uint16_t* label) const
    {
        if (!label || m_loopDepth == 0) return false;
        *label = m_loopStack[m_loopDepth - 1U].breakLabel;
        return true;
    }
    bool current_continue_target(uint16_t* label) const
    {
        if (!label || m_loopDepth == 0) return false;
        *label = m_loopStack[m_loopDepth - 1U].continueLabel;
        return true;
    }
    uint32_t loop_depth() const { return m_loopDepth; }

    bool create_label(uint16_t* label)
    {
        if (!label || m_labelCount >= COMPILER_MAX_BRANCH_LABELS) return false;
        *label = static_cast<uint16_t>(m_labelCount++);
        m_labels[*label].defined = false;
        m_labels[*label].offset = 0;
        return true;
    }
    bool define_label(uint16_t label)
    {
        if (label >= m_labelCount || m_labels[label].defined) return false;
        m_labels[label].defined = true;
        m_labels[label].offset = m_offset;
        return true;
    }
    bool emit_branch(uint8_t secondOpcode, uint16_t label)
    {
        if (label >= m_labelCount || m_fixupCount >= COMPILER_MAX_BRANCH_FIXUPS) return false;
        if (!byte(0x0F) || !byte(secondOpcode)) return false;
        const uint32_t patchOffset = m_offset;
        if (!u32(0)) return false;
        BranchFixup& fixup = m_fixups[m_fixupCount++];
        fixup.patchOffset = patchOffset;
        fixup.label = label;
        return true;
    }
    bool emit_jz(uint16_t label) { return emit_branch(0x84, label); }
    bool emit_jnz(uint16_t label) { return emit_branch(0x85, label); }
    bool emit_jl(uint16_t label) { return emit_branch(0x8C, label); }
    bool emit_jo(uint16_t label) { return emit_branch(0x80, label); }
    bool emit_jge(uint16_t label) { return emit_branch(0x8D, label); }
    bool emit_jae(uint16_t label) { return emit_branch(0x83, label); }
    bool emit_jb(uint16_t label) { return emit_branch(0x82, label); }
    bool emit_ja(uint16_t label) { return emit_branch(0x87, label); }
    bool emit_jmp(uint16_t label)
    {
        if (label >= m_labelCount || m_fixupCount >= COMPILER_MAX_BRANCH_FIXUPS) return false;
        if (!byte(0xE9)) return false;
        const uint32_t patchOffset = m_offset;
        if (!u32(0)) return false;
        BranchFixup& fixup = m_fixups[m_fixupCount++];
        fixup.patchOffset = patchOffset;
        fixup.label = label;
        return true;
    }
    bool emit_call(uint16_t label)
    {
        if (label >= m_labelCount || m_fixupCount >= COMPILER_MAX_BRANCH_FIXUPS) return false;
        if (!byte(0xE8)) return false;
        const uint32_t patchOffset = m_offset;
        if (!u32(0)) return false;
        BranchFixup& fixup = m_fixups[m_fixupCount++];
        fixup.patchOffset = patchOffset;
        fixup.label = label;
        return true;
    }
    bool emit_call_external(const char* name, SourceLocation location)
    {
        if (!name || !m_relocations || !m_relocationCount ||
            *m_relocationCount >= m_relocationCapacity || !byte(0xE8)) return false;
        const uint32_t patchOffset = m_offset;
        if (!u32(0)) return false;
        RelocationRecord& relocation = m_relocations[(*m_relocationCount)++];
        relocation = {};
        relocation.kind = RelocationKind::CallRel32;
        relocation.width = 4;
        relocation.patchOffset = patchOffset;
        relocation.location = location;
        uint32_t i = 0;
        while (i + 1 < sizeof(relocation.targetSymbolName) && name[i] != '\0') {
            relocation.targetSymbolName[i] = name[i];
            ++i;
        }
        relocation.targetSymbolName[i] = '\0';
        return name[i] == '\0';
    }
    bool emit_data_address(uint32_t dataOffset, SourceLocation location)
    {
        if (!m_relocations || !m_relocationCount ||
            *m_relocationCount >= m_relocationCapacity ||
            !bytes(reinterpret_cast<const uint8_t*>("\x48\xBA"), 2)) return false;
        const uint32_t patchOffset = m_offset;
        if (!u64(0)) return false;
        RelocationRecord& relocation = m_relocations[(*m_relocationCount)++];
        relocation = {};
        relocation.kind = RelocationKind::DataAddress64;
        relocation.width = 8;
        relocation.patchOffset = patchOffset;
        relocation.dataOffset = dataOffset;
        relocation.location = location;
        return true;
    }
    bool emit_global_data_address(const char* name, SourceLocation location)
    {
        if (!name || !m_relocations || !m_relocationCount ||
            *m_relocationCount >= m_relocationCapacity ||
            !bytes(reinterpret_cast<const uint8_t*>("\x48\xBA"), 2)) return false;
        const uint32_t patchOffset = m_offset;
        if (!u64(0)) return false;
        RelocationRecord& relocation = m_relocations[(*m_relocationCount)++];
        relocation = {};
        relocation.kind = RelocationKind::GlobalDataAddress64;
        relocation.width = 8;
        relocation.patchOffset = patchOffset;
        relocation.location = location;
        uint32_t i = 0;
        while (i + 1 < sizeof(relocation.targetSymbolName) && name[i] != '\0') {
            relocation.targetSymbolName[i] = name[i];
            ++i;
        }
        relocation.targetSymbolName[i] = '\0';
        return name[i] == '\0';
    }
    bool patch_branches()
    {
        for (uint32_t i = 0; i < m_fixupCount; ++i) {
            const BranchFixup& fixup = m_fixups[i];
            if (fixup.label >= m_labelCount || !m_labels[fixup.label].defined ||
                fixup.patchOffset > m_offset || m_offset - fixup.patchOffset < 4U) return false;
            int32_t displacement = 0;
            if (!calculate_signed_rel32(m_labels[fixup.label].offset,
                                         static_cast<uint64_t>(fixup.patchOffset) + 4U,
                                         &displacement)) return false;
            const uint32_t value = static_cast<uint32_t>(displacement);
            for (uint32_t j = 0; j < 4; ++j) m_output[fixup.patchOffset + j] = static_cast<uint8_t>(value >> (j * 8));
        }
        return true;
    }

private:
    struct BranchLabel { bool defined; uint32_t offset; };
    struct BranchFixup { uint32_t patchOffset; uint16_t label; };
    struct LoopTarget { uint16_t continueLabel; uint16_t breakLabel; };
    uint8_t* m_output;
    uint32_t m_capacity;
    uint32_t m_offset;
    uint32_t m_labelCount;
    uint32_t m_fixupCount;
    uint32_t m_loopDepth;
    uint8_t m_rspMod16;
    uint16_t m_temporaryDepth;
    uint16_t m_maxTemporaryDepth;
    uint16_t m_pointerTemporaryDepth;
    uint16_t m_maxPointerTemporaryDepth;
    uint32_t m_transientBytes;
    uint32_t m_maxTransientBytes;
    RelocationRecord* m_relocations;
    uint32_t m_relocationCapacity;
    uint32_t* m_relocationCount;
    BranchLabel m_labels[COMPILER_MAX_BRANCH_LABELS];
    BranchFixup m_fixups[COMPILER_MAX_BRANCH_FIXUPS];
    LoopTarget m_loopStack[COMPILER_MAX_LOOP_TARGET_DEPTH] = {};
};

static uint32_t host_log_count(const FunctionIR& function)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < function.statementCount; ++i)
        if (function.statements[i].kind == StatementKind::HostLog) ++count;
    return count;
}

static bool is_legacy_single_log(const FunctionIR& function)
{
    return function.blockCount == 1 && function.returnCount == 1 && function.localCount == 0 &&
           function.integerParameterCount == 0 && function.callCount == 0 &&
           host_log_count(function) == 1 && function.returnExpression < function.expressionCount &&
           function.expressions[function.returnExpression].kind == ExpressionKind::Constant;
}

static bool has_indexed_access(const FunctionIR& function)
{
    for (uint32_t i = 0; i < function.expressionCount; ++i)
        if (function.expressions[i].kind == ExpressionKind::LoadIndexed ||
            function.expressions[i].kind == ExpressionKind::AddressOfIndexed ||
            function.expressions[i].kind == ExpressionKind::LoadField ||
            function.expressions[i].kind == ExpressionKind::AddressOfField) return true;
    for (uint32_t i = 0; i < function.statementCount; ++i)
        if (function.statements[i].kind == StatementKind::StoreIndexed ||
            function.statements[i].kind == StatementKind::StoreField) return true;
    return false;
}

static bool emit_log(Emitter& emitter, const FunctionIR& function, const Statement& statement,
                     uint64_t dataAddress, bool framed, int32_t contextDisplacement)
{
    if (statement.stringIndex >= function.stringCount) return false;
    if (framed && !emitter.mov_rcx_context_local(contextDisplacement)) return false;
    const uint32_t stringOffset = function.dataOffset + function.stringOffsets[statement.stringIndex];
    if (dataAddress != 0) {
        if (!emitter.mov_rdx_imm64(dataAddress + stringOffset)) return false;
    } else if (!emitter.emit_data_address(stringOffset, statement.location)) {
        return false;
    }
    if (!emitter.load_host_log()) return false;
    const uint32_t reserve = emitter.call_stack_reserve();
    if (!emitter.sub_rsp(reserve) || !emitter.call_rax() || !emitter.add_rsp(reserve)) return false;
    return true;
}

static bool required_temporary_slots(const FunctionIR& function, uint16_t index,
                                     uint32_t depth, uint16_t* output)
{
    if (!output || index == COMPILER_INVALID_INDEX || index >= function.expressionCount ||
        depth > COMPILER_MAX_EXPRESSION_NODES) return false;
    const Expression& expression = function.expressions[index];
    uint16_t left = 0, right = 0;
    if (expression.kind == ExpressionKind::Call) {
        if (!function.calls || !function.callArguments || expression.callIndex >= function.callCount) return false;
        const CallSite& call = function.calls[expression.callIndex];
        if (call.argumentCount > COMPILER_MAX_PARAMETERS ||
            call.argumentStart + call.argumentCount > function.callArgumentCount) return false;
        uint16_t nestedMax = 0;
        for (uint32_t i = 0; i < call.argumentCount; ++i) {
            uint16_t child = 0;
            if (!required_temporary_slots(function,
                    function.callArguments[call.argumentStart + i], depth + 1U, &child)) return false;
            if (child > nestedMax) nestedMax = child;
        }
        const uint32_t needed = static_cast<uint32_t>(call.argumentCount) + nestedMax;
        if (needed > COMPILER_MAX_TEMPORARY_SLOTS) return false;
        *output = static_cast<uint16_t>(needed);
        return true;
    }
    if (expression.kind == ExpressionKind::Constant ||
        expression.kind == ExpressionKind::LoadLocal ||
        expression.kind == ExpressionKind::LoadGlobal ||
        expression.kind == ExpressionKind::LoadPointer ||
        expression.kind == ExpressionKind::LoadStructPointer ||
        expression.kind == ExpressionKind::LoadStructAddressLocal ||
        expression.kind == ExpressionKind::LoadStructAddressGlobal ||
        expression.kind == ExpressionKind::AddressOfLocal ||
        expression.kind == ExpressionKind::AddressOfGlobal ||
        expression.kind == ExpressionKind::AddressOfStructLocal ||
        expression.kind == ExpressionKind::AddressOfStructGlobal) {
        *output = 0;
        return true;
    }
    if (expression.kind == ExpressionKind::LoadIndexed || expression.kind == ExpressionKind::AddressOfIndexed ||
        expression.kind == ExpressionKind::LoadField || expression.kind == ExpressionKind::AddressOfField)
        return required_temporary_slots(function, expression.left, depth + 1U, output);
    if (expression.kind == ExpressionKind::Negate || expression.kind == ExpressionKind::LoadIndirectInt32)
        return required_temporary_slots(function, expression.left, depth + 1U, output);
    if (!required_temporary_slots(function, expression.left, depth + 1U, &left)) return false;
    if (!required_temporary_slots(function, expression.right, depth + 1U, &right)) return false;
    *output = left > right ? left : right;
    return true;
}

static bool required_transient_stack_bytes(const FunctionIR& function, uint16_t index,
                                           uint32_t depth, uint32_t* output)
{
    if (!output || index == COMPILER_INVALID_INDEX || index >= function.expressionCount ||
        depth > COMPILER_MAX_EXPRESSION_NODES) return false;
    const Expression& expression = function.expressions[index];
    if (expression.kind == ExpressionKind::Constant ||
        expression.kind == ExpressionKind::LoadLocal ||
        expression.kind == ExpressionKind::LoadGlobal ||
        expression.kind == ExpressionKind::LoadPointer ||
        expression.kind == ExpressionKind::LoadStructPointer ||
        expression.kind == ExpressionKind::LoadStructAddressLocal ||
        expression.kind == ExpressionKind::LoadStructAddressGlobal ||
        expression.kind == ExpressionKind::AddressOfLocal ||
        expression.kind == ExpressionKind::AddressOfGlobal ||
        expression.kind == ExpressionKind::AddressOfStructLocal ||
        expression.kind == ExpressionKind::AddressOfStructGlobal) {
        *output = 0;
        return true;
    }
    if (expression.kind == ExpressionKind::LoadIndexed || expression.kind == ExpressionKind::AddressOfIndexed ||
        expression.kind == ExpressionKind::LoadField || expression.kind == ExpressionKind::AddressOfField)
        return required_transient_stack_bytes(function, expression.left, depth + 1U, output);
    if (expression.kind == ExpressionKind::Call) {
        if (!function.calls || !function.callArguments || expression.callIndex >= function.callCount) return false;
        const CallSite& call = function.calls[expression.callIndex];
        if (call.argumentCount > COMPILER_MAX_PARAMETERS ||
            call.argumentStart + call.argumentCount > function.callArgumentCount) return false;
        uint32_t maximum = 0;
        for (uint32_t i = 0; i < call.argumentCount; ++i) {
            uint32_t child = 0;
            if (!required_transient_stack_bytes(function,
                    function.callArguments[call.argumentStart + i], depth + 1U, &child)) return false;
            if (child > maximum) maximum = child;
        }
        *output = maximum;
        return *output <= COMPILER_MAX_TRANSIENT_STACK_BYTES;
    }
    if (expression.kind == ExpressionKind::Negate || expression.kind == ExpressionKind::LoadIndirectInt32)
        return required_transient_stack_bytes(function, expression.left, depth + 1U, output);

    uint32_t left = 0, right = 0;
    if (!required_transient_stack_bytes(function, expression.left, depth + 1U, &left) ||
        !required_transient_stack_bytes(function, expression.right, depth + 1U, &right)) return false;
    const uint32_t needed = expression.kind == ExpressionKind::LogicalAnd ||
        expression.kind == ExpressionKind::LogicalOr ? (left > right ? left : right) :
        (left > 8U + right ? left : 8U + right);
    *output = needed;
    return needed <= COMPILER_MAX_TRANSIENT_STACK_BYTES;
}

static bool required_pointer_temporary_slots(const FunctionIR& function, uint16_t index,
                                             uint32_t depth, uint16_t* output)
{
    if (!output || index == COMPILER_INVALID_INDEX || index >= function.expressionCount ||
        depth > COMPILER_MAX_EXPRESSION_NODES) return false;
    const Expression& expression = function.expressions[index];
    *output = 0;
    if (expression.kind == ExpressionKind::Constant ||
        expression.kind == ExpressionKind::LoadLocal ||
        expression.kind == ExpressionKind::LoadGlobal ||
        expression.kind == ExpressionKind::LoadPointer ||
        expression.kind == ExpressionKind::LoadStructPointer ||
        expression.kind == ExpressionKind::LoadStructAddressLocal ||
        expression.kind == ExpressionKind::LoadStructAddressGlobal ||
        expression.kind == ExpressionKind::AddressOfLocal ||
        expression.kind == ExpressionKind::AddressOfGlobal ||
        expression.kind == ExpressionKind::AddressOfStructLocal ||
        expression.kind == ExpressionKind::AddressOfStructGlobal) return true;
    if (expression.kind == ExpressionKind::AddressOfIndexed ||
        expression.kind == ExpressionKind::LoadIndexed ||
        expression.kind == ExpressionKind::LoadField ||
        expression.kind == ExpressionKind::AddressOfField ||
        expression.kind == ExpressionKind::Negate ||
        expression.kind == ExpressionKind::LoadIndirectInt32)
        return required_pointer_temporary_slots(function, expression.left, depth + 1U, output);
    if (expression.kind == ExpressionKind::PointerAdd ||
        expression.kind == ExpressionKind::PointerSubtractInteger) {
        const bool pointerLeft = function.expressions[expression.left].type == ValueType::Int32Pointer;
        const uint16_t pointerChild = pointerLeft ? expression.left : expression.right;
        const uint16_t integerChild = pointerLeft ? expression.right : expression.left;
        uint16_t pointerNeeded = 0;
        uint16_t integerNeeded = 0;
        if (!required_pointer_temporary_slots(function, pointerChild, depth + 1U, &pointerNeeded) ||
            !required_pointer_temporary_slots(function, integerChild, depth + 1U, &integerNeeded)) return false;
        const uint32_t needed = 1U + integerNeeded;
        *output = static_cast<uint16_t>(needed > pointerNeeded ? needed : pointerNeeded);
        return needed <= COMPILER_MAX_POINTER_TEMPORARY_SLOTS &&
               *output <= COMPILER_MAX_POINTER_TEMPORARY_SLOTS;
    }
    if ((expression.kind == ExpressionKind::Equal || expression.kind == ExpressionKind::NotEqual) &&
        expression.left < function.expressionCount &&
        expression.right < function.expressionCount &&
        function.expressions[expression.left].type == ValueType::Int32Pointer &&
        function.expressions[expression.right].type == ValueType::Int32Pointer) {
        uint16_t leftNeeded = 0;
        uint16_t rightNeeded = 0;
        if (!required_pointer_temporary_slots(function, expression.left, depth + 1U, &leftNeeded) ||
            !required_pointer_temporary_slots(function, expression.right, depth + 1U, &rightNeeded)) return false;
        const uint32_t needed = 1U + rightNeeded;
        *output = static_cast<uint16_t>(needed > leftNeeded ? needed : leftNeeded);
        return *output <= COMPILER_MAX_POINTER_TEMPORARY_SLOTS;
    }
    if (expression.kind == ExpressionKind::Call) {
        if (!function.calls || !function.callArguments || expression.callIndex >= function.callCount) return false;
        const CallSite& call = function.calls[expression.callIndex];
        if (call.argumentCount > COMPILER_MAX_PARAMETERS ||
            call.argumentStart + call.argumentCount > function.callArgumentCount) return false;
        uint16_t maximum = 0;
        uint16_t child = 0;
        for (uint32_t i = 0; i < call.argumentCount; ++i) {
            if (!required_pointer_temporary_slots(function,
                    function.callArguments[call.argumentStart + i], depth + 1U, &child)) return false;
            if (child > maximum) maximum = child;
        }
        uint16_t pointerArguments = 0;
        for (uint32_t i = 0; i < call.argumentCount; ++i)
            if (parameter_kind_is_pointer(call.expectedParameterKinds[i])) ++pointerArguments;
        const uint32_t needed = static_cast<uint32_t>(maximum) + pointerArguments;
        if (needed > COMPILER_MAX_POINTER_TEMPORARY_SLOTS) return false;
        *output = static_cast<uint16_t>(needed);
        return true;
    }
    uint16_t left = 0, right = 0;
    if (!required_pointer_temporary_slots(function, expression.left, depth + 1U, &left) ||
        !required_pointer_temporary_slots(function, expression.right, depth + 1U, &right)) return false;
    *output = left > right ? left : right;
    return true;
}

static bool required_statement_temporary_slots(const FunctionIR& function, uint16_t* output,
                                               uint16_t* pointerOutput)
{
    if (!output || !pointerOutput) return false;
    *output = 0;
    *pointerOutput = 0;
    for (uint32_t i = 0; i < function.statementCount; ++i) {
        const Statement& statement = function.statements[i];
        if (statement.kind != StatementKind::StoreIndexed &&
            statement.kind != StatementKind::StoreIndirectInt32) continue;
        uint16_t right = 0;
        uint16_t index = 0;
        if (!required_temporary_slots(function, statement.expression, 0, &right)) return false;
        if (statement.kind == StatementKind::StoreIndexed) {
            if (!required_temporary_slots(function, statement.indexExpression, 0, &index)) return false;
        } else if (!required_temporary_slots(function, statement.targetExpression, 0, &index)) return false;
        const uint32_t needed = 1U + (right > index ? right : index);
        if (needed > COMPILER_MAX_TEMPORARY_SLOTS) return false;
        if (needed > *output) *output = static_cast<uint16_t>(needed);
        uint16_t rightPointers = 0;
        uint16_t indexPointers = 0;
        if (!required_pointer_temporary_slots(function, statement.expression, 0, &rightPointers) ||
            !required_pointer_temporary_slots(function,
                statement.kind == StatementKind::StoreIndexed ? statement.indexExpression : statement.targetExpression,
                0, &indexPointers)) return false;
        const uint16_t pointerNeeded = rightPointers > indexPointers ? rightPointers : indexPointers;
        if (pointerNeeded > *pointerOutput) *pointerOutput = pointerNeeded;
    }
    return true;
}

static bool emit_indexed_address(Emitter& emitter, const TranslationUnitIR& unit,
                                 const FunctionIR& function, const FrameLayout& frame,
                                 const uint16_t* functionLabels, uint16_t indexExpression,
                                 IndexedBaseKind baseKind, uint16_t baseIndex,
                                 uint16_t elementCount, SourceLocation location,
                                 uint16_t boundsFailureLabel, uint16_t epilogueLabel,
                                 uint16_t callFailureLabel);

static bool emit_array_bounds_failure(Emitter& emitter, uint16_t epilogueLabel);

static bool emit_zero_pointer_descriptor(Emitter& emitter, int32_t destination)
{
    return emitter.mov_eax_imm32(0) &&
           emitter.mov_local_rax64(destination) &&
           emitter.mov_local_rax64(destination + 8) &&
           emitter.mov_local_eax(destination + 16) &&
           emitter.mov_local_eax(destination + 20) &&
           emitter.mov_local_eax(destination + 24) &&
           emitter.mov_local_eax(destination + 28);
}

static bool emit_copy_pointer_descriptor(Emitter& emitter, int32_t destination)
{
    // RDX is the second source argument register. Descriptor copies run in
    // function prologues as well as call preparation, so keep it intact.
    return emitter.mov_r10_ptr64(0) && emitter.mov_local_r10_64(destination) &&
           emitter.mov_r10_ptr64(8) && emitter.mov_local_r10_64(destination + 8) &&
           emitter.mov_r10_ptr64(16) && emitter.mov_local_r10_64(destination + 16) &&
           emitter.mov_r10_ptr64(24) && emitter.mov_local_r10_64(destination + 24);
}

static bool emit_pointer_descriptor_metadata(Emitter& emitter, int32_t destination,
                                              uint32_t extent, uint32_t elementSize,
                                              PointerProvenanceKind provenance)
{
    return extent != 0 && elementSize != 0 &&
           emitter.mov_eax_imm32(extent) && emitter.mov_local_eax(destination + 16) &&
           emitter.mov_eax_imm32(elementSize) && emitter.mov_local_eax(destination + 20) &&
           emitter.mov_eax_imm32(static_cast<uint32_t>(provenance)) &&
           emitter.mov_local_eax(destination + 24) &&
           emitter.mov_eax_imm32(1) && emitter.mov_local_eax(destination + 28);
}

static bool emit_pointer_failure(Emitter& emitter, uint16_t epilogueLabel)
{
    return emitter.mov_eax_imm32(0) && emitter.set_invalid_pointer_failure() &&
           emitter.emit_jmp(epilogueLabel);
}

static bool emit_pointer_validation(Emitter& emitter, uint16_t failureLabel)
{
    // RAX points at the descriptor.  Validate the descriptor fields before
    // loading the user storage address in RDX.
    return emitter.test_rax_rax() && emitter.emit_jz(failureLabel) &&
           emitter.mov_ecx_ptr32(28) && emitter.cmp_ecx_imm32(1) && emitter.emit_jnz(failureLabel) &&
           emitter.mov_ecx_ptr32(20) && emitter.cmp_ecx_imm32(4) && emitter.emit_jnz(failureLabel) &&
           emitter.mov_r8d_ptr32(16) && emitter.cmp_r8d_imm32(4) && emitter.emit_jb(failureLabel) &&
           emitter.mov_rdx_ptr64(0) && emitter.mov_rcx_ptr64(8) &&
           emitter.cmp_rdx_rcx() && emitter.emit_jb(failureLabel) &&
           emitter.test_edx_imm32(3) && emitter.emit_jnz(failureLabel) &&
           emitter.sub_rdx_rcx() && emitter.sub_r8d_imm32(4) &&
           emitter.cmp_rdx_r8() && emitter.emit_ja(failureLabel);
}

static bool emit_pointer_position_validation(Emitter& emitter, uint16_t failureLabel)
{
    // Arithmetic accepts the one-past position, unlike dereference.  The
    // descriptor still has to be a well-formed int* object descriptor.
    return emitter.test_rax_rax() && emitter.emit_jz(failureLabel) &&
           emitter.mov_ecx_ptr32(28) && emitter.cmp_ecx_imm32(1) && emitter.emit_jnz(failureLabel) &&
           emitter.mov_ecx_ptr32(20) && emitter.cmp_ecx_imm32(4) && emitter.emit_jnz(failureLabel) &&
           emitter.mov_r8d_ptr32(16) && emitter.cmp_r8d_imm32(4) && emitter.emit_jb(failureLabel) &&
           emitter.test_r8d_imm32(3) && emitter.emit_jnz(failureLabel) &&
           emitter.mov_rdx_ptr64(0) && emitter.mov_rcx_ptr64(8) &&
           emitter.cmp_rdx_rcx() && emitter.emit_jb(failureLabel) &&
           emitter.test_rcx_imm32(3) && emitter.emit_jnz(failureLabel) &&
           emitter.test_edx_imm32(3) && emitter.emit_jnz(failureLabel) &&
           emitter.mov_r9_rcx() && emitter.add_r9_r8() && emitter.emit_jb(failureLabel) &&
           emitter.cmp_rdx_r9() && emitter.emit_ja(failureLabel);
}

static bool emit_struct_pointer_validation(Emitter& emitter, uint32_t structBytes,
                                           uint16_t failureLabel)
{
    if (structBytes == 0) return false;
    // Struct pointers are non-traversable in Phase 27T: current == base and
    // the descriptor extent/stride must exactly match the named layout.
    return emitter.test_rax_rax() && emitter.emit_jz(failureLabel) &&
           emitter.mov_ecx_ptr32(28) && emitter.cmp_ecx_imm32(1) && emitter.emit_jnz(failureLabel) &&
           emitter.mov_ecx_ptr32(20) && emitter.cmp_ecx_imm32(structBytes) && emitter.emit_jnz(failureLabel) &&
           emitter.mov_r8d_ptr32(16) && emitter.cmp_r8d_imm32(structBytes) && emitter.emit_jnz(failureLabel) &&
           emitter.mov_rdx_ptr64(0) && emitter.mov_rcx_ptr64(8) &&
           emitter.cmp_rdx_rcx() && emitter.emit_jnz(failureLabel) &&
           emitter.test_edx_imm32(3) && emitter.emit_jnz(failureLabel);
}

static bool emit_pointer_arithmetic_failure(Emitter& emitter, uint16_t epilogueLabel)
{
    return emitter.mov_eax_imm32(0) && emitter.set_pointer_bounds_failure() &&
           emitter.emit_jmp(epilogueLabel);
}

static bool emit_expression_value(Emitter& emitter, const TranslationUnitIR& unit,
                                  const FunctionIR& function, const FrameLayout& frame,
                                  const uint16_t* functionLabels, uint16_t index,
                                  uint16_t epilogueLabel, uint16_t callFailureLabel,
                                  uint16_t* pointerResultSlot);

static bool emit_expression(Emitter& emitter, const TranslationUnitIR& unit,
                            const FunctionIR& function, const FrameLayout& frame,
                            const uint16_t* functionLabels, uint16_t index,
                            uint16_t epilogueLabel, uint16_t callFailureLabel);

static bool emit_field_address_raw(Emitter& emitter, const TranslationUnitIR& unit,
                                   const FunctionIR& function, const FrameLayout& frame,
                                   const uint16_t* functionLabels, const Expression& expression,
                                   uint16_t epilogueLabel, uint16_t callFailureLabel)
{
    if (expression.left == COMPILER_INVALID_INDEX || expression.left >= function.expressionCount ||
        expression.value < 0 || !functionLabels) return false;
    const Expression& base = function.expressions[expression.left];
    if (base.type == ValueType::StructValue) {
        return emit_expression(emitter, unit, function, frame, functionLabels, expression.left,
                               epilogueLabel, callFailureLabel) &&
               emitter.lea_rax_rax_disp32(static_cast<uint32_t>(expression.value));
    }
    if (base.type != ValueType::StructPointer || base.elementCount >= unit.structTypeCount ||
        unit.structTypes[base.elementCount].sizeBytes == 0) return false;
    uint16_t failureLabel = COMPILER_INVALID_INDEX;
    uint16_t endLabel = COMPILER_INVALID_INDEX;
    uint16_t pointerSlot = COMPILER_INVALID_INDEX;
    if (!emitter.create_label(&failureLabel) || !emitter.create_label(&endLabel) ||
        !emit_expression_value(emitter, unit, function, frame, functionLabels, expression.left,
                               epilogueLabel, callFailureLabel, &pointerSlot) ||
        !emit_struct_pointer_validation(emitter, unit.structTypes[base.elementCount].sizeBytes,
                                        failureLabel) ||
        !emitter.mov_rdx_ptr64(0) ||
        !emitter.lea_rax_rdx_disp32(static_cast<uint32_t>(expression.value)) ||
        !emitter.emit_jmp(endLabel) ||
        !emitter.define_label(failureLabel) || !emit_pointer_failure(emitter, epilogueLabel) ||
        !emitter.define_label(endLabel)) return false;
    if (pointerSlot != COMPILER_INVALID_INDEX && !emitter.release_pointer_temporary_slots(1)) return false;
    return true;
}

static bool emit_address_of(Emitter& emitter, const TranslationUnitIR& unit,
                            const FunctionIR& function, const FrameLayout& frame,
                            const uint16_t* functionLabels, const Expression& expression,
                            uint16_t epilogueLabel, uint16_t callFailureLabel)
{
    if (frame.pointerScratchDisplacement == 0) return false;
    const int32_t scratch = frame.pointerScratchDisplacement;
    if (expression.kind == ExpressionKind::AddressOfStructLocal ||
        expression.kind == ExpressionKind::AddressOfStructGlobal) {
        if (expression.elementCount >= unit.structTypeCount ||
            unit.structTypes[expression.elementCount].sizeBytes == 0) return false;
        const uint32_t structBytes = unit.structTypes[expression.elementCount].sizeBytes;
        if (expression.kind == ExpressionKind::AddressOfStructLocal) {
            bool valid = false;
            for (uint32_t i = 0; i < function.localCount; ++i)
                if (function.locals[i].slot == expression.localIndex &&
                    function.locals[i].kind == StorageKind::Struct &&
                    function.locals[i].structTypeIndex == expression.elementCount) valid = true;
            if (!valid || !emitter.lea_rax_local(local_displacement(expression.localIndex)) ||
                !emitter.mov_local_rax64(scratch) || !emitter.mov_local_rax64(scratch + 8)) return false;
            if (!emit_pointer_descriptor_metadata(emitter, scratch, structBytes, structBytes,
                                                  PointerProvenanceKind::LocalStruct)) return false;
        } else {
            if (expression.globalIndex >= unit.globalCount ||
                unit.globals[expression.globalIndex].kind != StorageKind::Struct ||
                unit.globals[expression.globalIndex].structTypeIndex != expression.elementCount ||
                !emitter.emit_global_data_address(unit.globals[expression.globalIndex].name, expression.location) ||
                !emitter.mov_local_rdx64(scratch) || !emitter.mov_local_rdx64(scratch + 8)) return false;
            if (!emit_pointer_descriptor_metadata(emitter, scratch, structBytes, structBytes,
                                                  PointerProvenanceKind::GlobalStruct)) return false;
        }
        return emitter.lea_rax_local(scratch);
    }
    if (expression.kind == ExpressionKind::AddressOfField) {
        if (!emit_field_address_raw(emitter, unit, function, frame, functionLabels, expression,
                                    epilogueLabel, callFailureLabel) ||
            !emitter.mov_local_rax64(scratch) || !emitter.mov_local_rax64(scratch + 8) ||
            !emit_pointer_descriptor_metadata(emitter, scratch, 4, 4,
                                               PointerProvenanceKind::FieldSubobject) ||
            !emitter.lea_rax_local(scratch)) return false;
        return true;
    }
    if (expression.kind == ExpressionKind::AddressOfLocal) {
        bool scalar = false;
        for (uint32_t i = 0; i < function.localCount; ++i)
            if (function.locals[i].slot == expression.localIndex)
                scalar = function.locals[i].kind == StorageKind::ScalarInt;
        if (!scalar || !emitter.lea_rax_local(local_displacement(expression.localIndex)) ||
            !emitter.mov_local_rax64(scratch) || !emitter.mov_local_rax64(scratch + 8) ||
            !emit_pointer_descriptor_metadata(emitter, scratch, 4, 4,
                                               PointerProvenanceKind::LocalScalar) ||
            !emitter.lea_rax_local(scratch)) return false;
        return true;
    }
    if (expression.kind == ExpressionKind::AddressOfGlobal) {
        if (expression.globalIndex >= unit.globalCount ||
            unit.globals[expression.globalIndex].kind != StorageKind::ScalarInt ||
            !emitter.emit_global_data_address(unit.globals[expression.globalIndex].name, expression.location) ||
            !emitter.mov_local_rdx64(scratch) || !emitter.mov_local_rdx64(scratch + 8) ||
            !emit_pointer_descriptor_metadata(emitter, scratch, 4, 4,
                                               PointerProvenanceKind::GlobalScalar) ||
            !emitter.lea_rax_local(scratch)) return false;
        return true;
    }
    if (expression.kind != ExpressionKind::AddressOfIndexed || expression.elementCount == 0 ||
        expression.elementSize != 4 || !functionLabels) return false;
    if (expression.indexedBaseKind == IndexedBaseKind::Local) {
        if (expression.localIndex >= function.parameterStorageBytes / 4U + function.localStorageBytes / 4U) return false;
    } else if (expression.globalIndex >= unit.globalCount ||
               unit.globals[expression.globalIndex].kind != StorageKind::ArrayInt) {
        return false;
    }
    uint16_t boundsFailureLabel = COMPILER_INVALID_INDEX;
    uint16_t endLabel = COMPILER_INVALID_INDEX;
    bool ok = emitter.create_label(&boundsFailureLabel) && emitter.create_label(&endLabel);
    if (ok) ok = emit_indexed_address(emitter, unit, function, frame, functionLabels,
                                      expression.left, expression.indexedBaseKind,
                                      expression.indexedBaseKind == IndexedBaseKind::Local
                                          ? expression.localIndex : expression.globalIndex,
                                      expression.elementCount, expression.location,
                                      boundsFailureLabel, epilogueLabel, callFailureLabel);
    if (ok) ok = emitter.mov_local_rdx64(scratch);
    if (ok) {
        ok = expression.indexedBaseKind == IndexedBaseKind::Local
            ? (emitter.lea_rax_local(local_displacement(expression.localIndex)) &&
               emitter.mov_local_rax64(scratch + 8))
            : (emitter.emit_global_data_address(unit.globals[expression.globalIndex].name,
                                                expression.location) &&
               emitter.mov_local_rdx64(scratch + 8));
    }
    if (ok) ok = emit_pointer_descriptor_metadata(
        emitter, scratch,
        static_cast<uint32_t>(expression.elementCount) * expression.elementSize,
        expression.elementSize,
        expression.indexedBaseKind == IndexedBaseKind::Local
            ? PointerProvenanceKind::LocalArrayElement : PointerProvenanceKind::GlobalArrayElement);
    if (ok) ok = emitter.emit_jmp(endLabel);
    if (ok) ok = emitter.define_label(boundsFailureLabel) && emit_array_bounds_failure(emitter, epilogueLabel);
    if (ok) ok = emitter.define_label(endLabel) && emitter.lea_rax_local(scratch);
    if (!ok) {
        return false;
    }
    return true;
}

static bool emit_expression(Emitter& emitter, const TranslationUnitIR& unit,
                            const FunctionIR& function, const FrameLayout& frame,
                            const uint16_t* functionLabels, uint16_t index,
                            uint16_t epilogueLabel, uint16_t callFailureLabel)
{
    uint16_t pointerResultSlot = COMPILER_INVALID_INDEX;
    const bool result = emit_expression_value(emitter, unit, function, frame, functionLabels,
                                              index, epilogueLabel, callFailureLabel,
                                              &pointerResultSlot);
    if (pointerResultSlot != COMPILER_INVALID_INDEX &&
        !emitter.release_pointer_temporary_slots(1)) return false;
    return result;
}

static bool emit_indexed_address(Emitter& emitter, const TranslationUnitIR& unit,
                                 const FunctionIR& function, const FrameLayout& frame,
                                 const uint16_t* functionLabels, uint16_t indexExpression,
                                 IndexedBaseKind baseKind, uint16_t baseIndex,
                                 uint16_t elementCount, SourceLocation location,
                                 uint16_t boundsFailureLabel, uint16_t epilogueLabel,
                                 uint16_t callFailureLabel)
{
    if (elementCount == 0 || elementCount > COMPILER_MAX_ARRAY_ELEMENTS ||
        !emit_expression(emitter, unit, function, frame, functionLabels, indexExpression,
                         epilogueLabel, callFailureLabel) ||
        !emitter.cmp_eax_imm32(0) || !emitter.emit_jl(boundsFailureLabel) ||
        !emitter.cmp_eax_imm32(elementCount) || !emitter.emit_jge(boundsFailureLabel) ||
        !emitter.movsxd_rax_eax()) return false;
    if (baseKind == IndexedBaseKind::Local) {
        if (baseIndex >= function.parameterStorageBytes / 4U + function.localStorageBytes / 4U)
            return false;
        return emitter.lea_rdx_local_rax(local_displacement(baseIndex));
    }
    if (baseIndex >= unit.globalCount) return false;
    return emitter.emit_global_data_address(unit.globals[baseIndex].name, location) &&
           emitter.lea_rdx_global_rax();
}

static bool emit_array_bounds_failure(Emitter& emitter, uint16_t epilogueLabel)
{
    return emitter.mov_eax_imm32(0) &&
           emitter.set_array_bounds_failure() &&
           emitter.emit_jmp(epilogueLabel);
}

static bool emit_pointer_arithmetic(Emitter& emitter, const TranslationUnitIR& unit,
                                    const FunctionIR& function, const FrameLayout& frame,
                                    const uint16_t* functionLabels, const Expression& expression,
                                    uint16_t epilogueLabel, uint16_t callFailureLabel,
                                    uint16_t* pointerResultSlot)
{
    if (!pointerResultSlot) return false;
    *pointerResultSlot = COMPILER_INVALID_INDEX;
    const bool pointerLeft = function.expressions[expression.left].type == ValueType::Int32Pointer;
    const uint16_t pointerExpression = pointerLeft ? expression.left : expression.right;
    const uint16_t integerExpression = pointerLeft ? expression.right : expression.left;
    uint16_t childSlot = COMPILER_INVALID_INDEX;
    if (pointerLeft) {
        if (!emit_expression_value(emitter, unit, function, frame, functionLabels,
                                    pointerExpression, epilogueLabel, callFailureLabel,
                                    &childSlot)) return false;
    } else {
        if (!emit_expression(emitter, unit, function, frame, functionLabels,
                              integerExpression, epilogueLabel, callFailureLabel) ||
            !emitter.push_rax() ||
            !emit_expression_value(emitter, unit, function, frame, functionLabels,
                                   pointerExpression, epilogueLabel, callFailureLabel,
                                   &childSlot)) return false;
    }
    // The child descriptor is either borrowed from a local/address-of scratch
    // or owned by the child expression.  Release the latter before acquiring
    // the result slot so the bounded allocator remains stack-like.
    if (childSlot != COMPILER_INVALID_INDEX &&
        !emitter.release_pointer_temporary_slots(1)) return false;
    uint16_t resultSlot = COMPILER_INVALID_INDEX;
    if (!emitter.acquire_pointer_temporary_slots(1, &resultSlot) ||
        !emit_copy_pointer_descriptor(emitter, pointer_temporary_displacement(frame, resultSlot))) return false;
    if (!pointerLeft && !emitter.pop_rcx()) return false;
    if (pointerLeft &&
        !emit_expression(emitter, unit, function, frame, functionLabels,
                          integerExpression, epilogueLabel, callFailureLabel)) return false;

    uint16_t failureLabel = COMPILER_INVALID_INDEX;
    uint16_t negativeLabel = COMPILER_INVALID_INDEX;
    uint16_t successLabel = COMPILER_INVALID_INDEX;
    uint16_t doneLabel = COMPILER_INVALID_INDEX;
    if (!emitter.create_label(&failureLabel) || !emitter.create_label(&negativeLabel) ||
        !emitter.create_label(&successLabel) || !emitter.create_label(&doneLabel) ||
        (!pointerLeft && !emitter.mov_eax_ecx()) ||
        !emitter.movsxd_rdx_eax() || !emitter.shl_rdx_2() ||
        !emitter.emit_jo(failureLabel) ||
        (expression.kind == ExpressionKind::PointerSubtractInteger && !emitter.neg_rdx()) ||
        (expression.kind == ExpressionKind::PointerSubtractInteger && !emitter.emit_jo(failureLabel)) ||
        !emitter.mov_r10_rdx() ||
        !emitter.lea_rax_local(pointer_temporary_displacement(frame, resultSlot)) ||
        !emit_pointer_position_validation(emitter, failureLabel) ||
        !emitter.mov_rax_ptr64(0) || !emitter.test_r10_r10() ||
        !emitter.emit_jl(negativeLabel) || !emitter.add_rax_r10() ||
        !emitter.emit_jb(failureLabel) || !emitter.cmp_rax_r9() ||
        !emitter.emit_ja(failureLabel) || !emitter.emit_jmp(successLabel) ||
        !emitter.define_label(negativeLabel) || !emitter.neg_r10() ||
        !emitter.emit_jo(failureLabel) || !emitter.cmp_rax_r10() ||
        !emitter.emit_jb(failureLabel) || !emitter.sub_rax_r10() ||
        !emitter.cmp_rax_rcx() || !emitter.emit_jb(failureLabel) ||
        !emitter.define_label(successLabel) ||
        !emitter.mov_local_rax64(pointer_temporary_displacement(frame, resultSlot)) ||
        !emitter.lea_rax_local(pointer_temporary_displacement(frame, resultSlot)) ||
        !emitter.emit_jmp(doneLabel) || !emitter.define_label(failureLabel) ||
        !emit_pointer_arithmetic_failure(emitter, epilogueLabel) ||
        !emitter.define_label(doneLabel)) return false;
    *pointerResultSlot = resultSlot;
    return true;
}

static bool emit_pointer_equality(Emitter& emitter, const TranslationUnitIR& unit,
                                  const FunctionIR& function, const FrameLayout& frame,
                                  const uint16_t* functionLabels, const Expression& expression,
                                  uint16_t epilogueLabel, uint16_t callFailureLabel)
{
    uint16_t leftSlot = COMPILER_INVALID_INDEX;
    if (!emit_expression_value(emitter, unit, function, frame, functionLabels,
                                expression.left, epilogueLabel, callFailureLabel,
                                &leftSlot)) return false;
    if (leftSlot == COMPILER_INVALID_INDEX) {
        if (!emitter.acquire_pointer_temporary_slots(1, &leftSlot) ||
            !emit_copy_pointer_descriptor(emitter, pointer_temporary_displacement(frame, leftSlot))) return false;
    }
    uint16_t rightSlot = COMPILER_INVALID_INDEX;
    if (!emit_expression_value(emitter, unit, function, frame, functionLabels,
                                expression.right, epilogueLabel, callFailureLabel,
                                &rightSlot)) return false;
    uint16_t mismatchLabel = COMPILER_INVALID_INDEX;
    uint16_t doneLabel = COMPILER_INVALID_INDEX;
    if (!emitter.create_label(&mismatchLabel) || !emitter.create_label(&doneLabel) ||
        !emitter.mov_reg_mem64(1, pointer_temporary_displacement(frame, leftSlot)) ||
        !emitter.cmp_rcx_ptr64(0) || !emitter.emit_jnz(mismatchLabel) ||
        !emitter.mov_reg_mem64(1, pointer_temporary_displacement(frame, leftSlot) + 8) ||
        !emitter.cmp_rcx_ptr64(8) || !emitter.emit_jnz(mismatchLabel) ||
        !emitter.mov_r8d_ptr32(16) ||
        !emitter.cmp_r8d_local32(pointer_temporary_displacement(frame, leftSlot) + 16) ||
        !emitter.emit_jnz(mismatchLabel) ||
        !emitter.mov_r8d_ptr32(20) ||
        !emitter.cmp_r8d_local32(pointer_temporary_displacement(frame, leftSlot) + 20) ||
        !emitter.emit_jnz(mismatchLabel) ||
        !emitter.mov_r8d_ptr32(24) ||
        !emitter.cmp_r8d_local32(pointer_temporary_displacement(frame, leftSlot) + 24) ||
        !emitter.emit_jnz(mismatchLabel) ||
        !emitter.mov_r8d_ptr32(28) ||
        !emitter.cmp_r8d_local32(pointer_temporary_displacement(frame, leftSlot) + 28) ||
        !emitter.emit_jnz(mismatchLabel) ||
        !emitter.mov_eax_imm32(expression.kind == ExpressionKind::Equal ? 1U : 0U) ||
        !emitter.emit_jmp(doneLabel) || !emitter.define_label(mismatchLabel) ||
        !emitter.mov_eax_imm32(expression.kind == ExpressionKind::Equal ? 0U : 1U) ||
        !emitter.define_label(doneLabel)) return false;
    if (rightSlot != COMPILER_INVALID_INDEX &&
        !emitter.release_pointer_temporary_slots(1)) return false;
    return emitter.release_pointer_temporary_slots(1);
}

static bool emit_expression_value(Emitter& emitter, const TranslationUnitIR& unit,
                                  const FunctionIR& function, const FrameLayout& frame,
                                  const uint16_t* functionLabels, uint16_t index,
                                  uint16_t epilogueLabel, uint16_t callFailureLabel,
                                  uint16_t* pointerResultSlot)
{
    if (pointerResultSlot) *pointerResultSlot = COMPILER_INVALID_INDEX;
    if (index == COMPILER_INVALID_INDEX || index >= function.expressionCount || !functionLabels) return false;
    const Expression& expression = function.expressions[index];
    if (expression.kind == ExpressionKind::LogicalAnd || expression.kind == ExpressionKind::LogicalOr) {
        uint16_t shortCircuitLabel = COMPILER_INVALID_INDEX;
        uint16_t endLabel = COMPILER_INVALID_INDEX;
        if (!emitter.create_label(&shortCircuitLabel) || !emitter.create_label(&endLabel) ||
            !emit_expression(emitter, unit, function, frame, functionLabels, expression.left,
                             epilogueLabel, callFailureLabel) || !emitter.test_eax_eax()) return false;
        if (expression.kind == ExpressionKind::LogicalAnd) {
            if (!emitter.emit_jz(shortCircuitLabel) ||
                !emit_expression(emitter, unit, function, frame, functionLabels, expression.right,
                                 epilogueLabel, callFailureLabel) || !emitter.test_eax_eax() ||
                !emitter.emit_jz(shortCircuitLabel) || !emitter.mov_eax_imm32(1) ||
                !emitter.emit_jmp(endLabel) || !emitter.define_label(shortCircuitLabel) ||
                !emitter.mov_eax_imm32(0) || !emitter.define_label(endLabel)) return false;
        } else {
            if (!emitter.emit_jnz(shortCircuitLabel) ||
                !emit_expression(emitter, unit, function, frame, functionLabels, expression.right,
                                 epilogueLabel, callFailureLabel) || !emitter.test_eax_eax() ||
                !emitter.emit_jnz(shortCircuitLabel) || !emitter.mov_eax_imm32(0) ||
                !emitter.emit_jmp(endLabel) || !emitter.define_label(shortCircuitLabel) ||
                !emitter.mov_eax_imm32(1) || !emitter.define_label(endLabel)) return false;
        }
        return true;
    }
    if (expression.kind == ExpressionKind::Call) {
        if (!function.calls || !function.callArguments || expression.callIndex >= function.callCount) return false;
        const CallSite& call = function.calls[expression.callIndex];
        if ((!call.external && call.calleeFunction >= unit.functionCount) ||
            call.argumentCount > COMPILER_MAX_PARAMETERS ||
            call.argumentStart + call.argumentCount > function.callArgumentCount ||
            (!call.external && functionLabels[call.calleeFunction] == COMPILER_INVALID_INDEX)) return false;
        uint16_t base = 0;
        uint16_t pointerBase = 0;
        uint16_t pointerArgumentCount = 0;
        for (uint32_t i = 0; i < call.argumentCount; ++i)
            if (parameter_kind_is_pointer(call.expectedParameterKinds[i])) ++pointerArgumentCount;
        if (!emitter.acquire_temporary_slots(call.argumentCount, &base) ||
            !emitter.acquire_pointer_temporary_slots(pointerArgumentCount, &pointerBase)) return false;
        uint16_t pointerArgumentIndex = 0;
        for (uint32_t i = 0; i < call.argumentCount; ++i) {
            uint16_t argumentPointerSlot = COMPILER_INVALID_INDEX;
            if (!emit_expression_value(emitter, unit, function, frame, functionLabels,
                                       function.callArguments[call.argumentStart + i],
                                       epilogueLabel, callFailureLabel,
                                       &argumentPointerSlot)) return false;
            if (parameter_kind_is_pointer(call.expectedParameterKinds[i])) {
                if (!emit_copy_pointer_descriptor(emitter,
                        pointer_temporary_displacement(frame, pointerBase + pointerArgumentIndex++))) return false;
            } else if (!emitter.mov_local_eax(temporary_displacement(frame, static_cast<uint16_t>(base + i)))) {
                return false;
            }
            if (argumentPointerSlot != COMPILER_INVALID_INDEX &&
                !emitter.release_pointer_temporary_slots(1)) return false;
        }
        static const uint8_t argumentRegisters[] = {1, 2, 8, 9}; // ECX, EDX, R8D, R9D
        pointerArgumentIndex = 0;
        for (uint32_t i = 0; i < call.argumentCount; ++i) {
            if (parameter_kind_is_pointer(call.expectedParameterKinds[i])) {
                if (!emitter.lea_reg_local64(argumentRegisters[i],
                        pointer_temporary_displacement(frame, pointerBase + pointerArgumentIndex++))) return false;
            } else if (!emitter.mov_reg_mem32(argumentRegisters[i],
                       temporary_displacement(frame, static_cast<uint16_t>(base + i)))) return false;
        }
        if (callFailureLabel == COMPILER_INVALID_INDEX || epilogueLabel == COMPILER_INVALID_INDEX) return false;
        const uint32_t reserve = emitter.call_stack_reserve();
        if (!emitter.cmp_r14d_imm32(COMPILER_MAX_RUNTIME_CALL_DEPTH) ||
            !emitter.emit_jae(callFailureLabel) || !emitter.inc_r14d() ||
            !emitter.sub_rsp(reserve)) return false;
        if (call.external) {
            if (!emitter.emit_call_external(call.calleeName, call.location)) return false;
        } else if (!emitter.emit_call(functionLabels[call.calleeFunction])) {
            return false;
        }
        return
            emitter.add_rsp(reserve) && emitter.test_r15d() && emitter.emit_jnz(epilogueLabel) &&
            emitter.release_pointer_temporary_slots(pointerArgumentCount) &&
            emitter.release_temporary_slots(call.argumentCount);
    }
    switch (expression.kind) {
        case ExpressionKind::Constant:
            return emitter.mov_eax_imm32(static_cast<uint32_t>(expression.value));
        case ExpressionKind::LoadLocal:
            if (expression.localIndex >= function.parameterStorageBytes / 4U + function.localStorageBytes / 4U) return false;
            return emitter.mov_eax_local(local_displacement(expression.localIndex));
        case ExpressionKind::LoadPointer: {
            bool pointerSlot = false;
            for (uint32_t p = 0; p < function.parameterCount; ++p)
                if (function.parameters[p].slot == expression.localIndex &&
                    parameter_kind_is_pointer(function.parameters[p].kind)) pointerSlot = true;
            for (uint32_t l = 0; l < function.localCount; ++l)
                if (function.locals[l].slot == expression.localIndex &&
                    (function.locals[l].kind == StorageKind::PointerInt ||
                     function.locals[l].kind == StorageKind::PointerStruct)) pointerSlot = true;
            return pointerSlot && emitter.lea_rax_local(local_displacement(expression.localIndex));
        }
        case ExpressionKind::LoadStructPointer: {
            bool pointerSlot = false;
            for (uint32_t p = 0; p < function.parameterCount; ++p)
                if (function.parameters[p].slot == expression.localIndex &&
                    function.parameters[p].kind == ParameterKind::StructPointer) pointerSlot = true;
            for (uint32_t l = 0; l < function.localCount; ++l)
                if (function.locals[l].slot == expression.localIndex &&
                    function.locals[l].kind == StorageKind::PointerStruct &&
                    function.locals[l].structTypeIndex == expression.elementCount) pointerSlot = true;
            return pointerSlot && emitter.lea_rax_local(local_displacement(expression.localIndex));
        }
        case ExpressionKind::LoadStructAddressLocal: {
            bool valid = false;
            for (uint32_t l = 0; l < function.localCount; ++l)
                if (function.locals[l].slot == expression.localIndex &&
                    function.locals[l].kind == StorageKind::Struct &&
                    function.locals[l].structTypeIndex == expression.elementCount) valid = true;
            return valid && emitter.lea_rax_local(local_displacement(expression.localIndex));
        }
        case ExpressionKind::LoadStructAddressGlobal:
            return expression.globalIndex < unit.globalCount &&
                unit.globals[expression.globalIndex].kind == StorageKind::Struct &&
                unit.globals[expression.globalIndex].structTypeIndex == expression.elementCount &&
                emitter.emit_global_data_address(unit.globals[expression.globalIndex].name, expression.location) &&
                emitter.mov_rax_reg64(2);
        case ExpressionKind::LoadGlobal:
            if (expression.globalIndex >= unit.globalCount) return false;
            return emitter.emit_global_data_address(unit.globals[expression.globalIndex].name,
                                                    expression.location) && emitter.mov_eax_rax();
        case ExpressionKind::LoadIndexed: {
            uint16_t boundsFailureLabel = COMPILER_INVALID_INDEX;
            uint16_t endLabel = COMPILER_INVALID_INDEX;
            const uint16_t baseIndex = expression.indexedBaseKind == IndexedBaseKind::Local
                ? expression.localIndex : expression.globalIndex;
            if (!emitter.create_label(&boundsFailureLabel) || !emitter.create_label(&endLabel) ||
                !emit_indexed_address(emitter, unit, function, frame, functionLabels,
                                      expression.left, expression.indexedBaseKind, baseIndex,
                                      expression.elementCount, expression.location,
                                      boundsFailureLabel, epilogueLabel, callFailureLabel) ||
                !emitter.mov_eax_rax() || !emitter.emit_jmp(endLabel) ||
                !emitter.define_label(boundsFailureLabel) ||
                !emit_array_bounds_failure(emitter, epilogueLabel) ||
                !emitter.define_label(endLabel)) return false;
            return true;
        }
        case ExpressionKind::AddressOfLocal:
        case ExpressionKind::AddressOfGlobal:
        case ExpressionKind::AddressOfIndexed:
        case ExpressionKind::AddressOfStructLocal:
        case ExpressionKind::AddressOfStructGlobal:
        case ExpressionKind::AddressOfField:
            return emit_address_of(emitter, unit, function, frame, functionLabels, expression,
                                   epilogueLabel, callFailureLabel);
        case ExpressionKind::LoadField:
            return emit_field_address_raw(emitter, unit, function, frame, functionLabels, expression,
                                          epilogueLabel, callFailureLabel) && emitter.mov_eax_ptr_rax();
        case ExpressionKind::PointerAdd:
        case ExpressionKind::PointerSubtractInteger:
            return emit_pointer_arithmetic(emitter, unit, function, frame, functionLabels, expression,
                                           epilogueLabel, callFailureLabel, pointerResultSlot);
        case ExpressionKind::LoadIndirectInt32: {
            uint16_t failureLabel = COMPILER_INVALID_INDEX;
            uint16_t endLabel = COMPILER_INVALID_INDEX;
            uint16_t pointerChildSlot = COMPILER_INVALID_INDEX;
            if (!emitter.create_label(&failureLabel) || !emitter.create_label(&endLabel) ||
                !emit_expression_value(emitter, unit, function, frame, functionLabels, expression.left,
                                       epilogueLabel, callFailureLabel, &pointerChildSlot) ||
                !emit_pointer_validation(emitter, failureLabel) ||
                !emitter.mov_rdx_ptr64(0) || !emitter.mov_eax_rax() || !emitter.emit_jmp(endLabel) ||
                !emitter.define_label(failureLabel) || !emit_pointer_failure(emitter, epilogueLabel) ||
                !emitter.define_label(endLabel)) return false;
            if (pointerChildSlot != COMPILER_INVALID_INDEX &&
                !emitter.release_pointer_temporary_slots(1)) return false;
            return true;
        }
        case ExpressionKind::Negate:
            return emit_expression(emitter, unit, function, frame, functionLabels, expression.left,
                                   epilogueLabel, callFailureLabel) && emitter.neg_eax();
        case ExpressionKind::Add:
        case ExpressionKind::Subtract:
        case ExpressionKind::Multiply:
        case ExpressionKind::Equal:
        case ExpressionKind::NotEqual:
        case ExpressionKind::Less:
        case ExpressionKind::LessEqual:
        case ExpressionKind::Greater:
        case ExpressionKind::GreaterEqual:
            if ((expression.kind == ExpressionKind::Equal || expression.kind == ExpressionKind::NotEqual) &&
                function.expressions[expression.left].type == ValueType::Int32Pointer &&
                function.expressions[expression.right].type == ValueType::Int32Pointer)
                return emit_pointer_equality(emitter, unit, function, frame, functionLabels, expression,
                                             epilogueLabel, callFailureLabel);
            if (!emit_expression(emitter, unit, function, frame, functionLabels, expression.left,
                                 epilogueLabel, callFailureLabel) || !emitter.push_rax() ||
                !emit_expression(emitter, unit, function, frame, functionLabels, expression.right,
                                 epilogueLabel, callFailureLabel) || !emitter.pop_rcx()) return false;
            if (expression.kind == ExpressionKind::Add) return emitter.add_eax_ecx();
            if (expression.kind == ExpressionKind::Subtract)
                return emitter.bytes(reinterpret_cast<const uint8_t*>("\x91"), 1) && emitter.sub_eax_ecx();
            if (expression.kind == ExpressionKind::Multiply) return emitter.imul_eax_ecx();
            if (!emitter.cmp_ecx_eax()) return false;
            switch (expression.kind) {
                case ExpressionKind::Equal: return emitter.setcc(0x94) && emitter.movzx_eax_al();
                case ExpressionKind::NotEqual: return emitter.setcc(0x95) && emitter.movzx_eax_al();
                case ExpressionKind::Less: return emitter.setcc(0x9C) && emitter.movzx_eax_al();
                case ExpressionKind::LessEqual: return emitter.setcc(0x9E) && emitter.movzx_eax_al();
                case ExpressionKind::Greater: return emitter.setcc(0x9F) && emitter.movzx_eax_al();
                case ExpressionKind::GreaterEqual: return emitter.setcc(0x9D) && emitter.movzx_eax_al();
                default: return false;
            }
        default: return false;
    }
}

static bool emit_block(Emitter& emitter, const TranslationUnitIR& unit, const FunctionIR& function,
                       uint16_t blockIndex, uint64_t dataAddress, const FrameLayout& frame,
                       const uint16_t* functionLabels, uint16_t epilogueLabel,
                       uint16_t callFailureLabel,
                       uint32_t depth, uint32_t loopDepth);

static bool emit_statement(Emitter& emitter, const TranslationUnitIR& unit, const FunctionIR& function,
                           const Statement& statement, uint64_t dataAddress, const FrameLayout& frame,
                           const uint16_t* functionLabels, uint16_t epilogueLabel,
                           uint16_t callFailureLabel,
                           uint32_t depth, uint32_t loopDepth)
{
    switch (statement.kind) {
        case StatementKind::DeclareLocal: {
            const LocalSymbol* local = nullptr;
            for (uint32_t i = 0; i < function.localCount; ++i)
                if (function.locals[i].slot == statement.localIndex) local = &function.locals[i];
            if (!local) return false;
            if (local->kind == StorageKind::PointerInt) {
                if (statement.expression == COMPILER_INVALID_INDEX)
                    return emit_zero_pointer_descriptor(emitter,
                                                        local_displacement(statement.localIndex));
                uint16_t pointerSlot = COMPILER_INVALID_INDEX;
                if (!emit_expression_value(emitter, unit, function, frame, functionLabels,
                                           statement.expression, epilogueLabel, callFailureLabel,
                                           &pointerSlot) ||
                    !emit_copy_pointer_descriptor(emitter, local_displacement(statement.localIndex))) return false;
                return pointerSlot == COMPILER_INVALID_INDEX ||
                       emitter.release_pointer_temporary_slots(1);
            }
            if (local->kind == StorageKind::PointerStruct) {
                if (statement.expression == COMPILER_INVALID_INDEX)
                    return emit_zero_pointer_descriptor(emitter,
                                                        local_displacement(statement.localIndex));
                uint16_t pointerSlot = COMPILER_INVALID_INDEX;
                if (!emit_expression_value(emitter, unit, function, frame, functionLabels,
                                           statement.expression, epilogueLabel, callFailureLabel,
                                           &pointerSlot) ||
                    !emit_copy_pointer_descriptor(emitter, local_displacement(statement.localIndex))) return false;
                return pointerSlot == COMPILER_INVALID_INDEX ||
                       emitter.release_pointer_temporary_slots(1);
            }
            if (local->kind == StorageKind::Struct) {
                if (statement.expression == COMPILER_INVALID_INDEX || local->sizeBytes == 0 ||
                    (local->sizeBytes & 3U) != 0 ||
                    !emit_expression(emitter, unit, function, frame, functionLabels, statement.expression,
                                     epilogueLabel, callFailureLabel)) return false;
                for (uint32_t word = 0; word < local->sizeBytes / 4U; ++word)
                    if (!emitter.mov_local_eax(local_displacement(statement.localIndex) +
                                                static_cast<int32_t>(word * 4U))) return false;
                return true;
            }
            return statement.expression != COMPILER_INVALID_INDEX &&
                emit_expression(emitter, unit, function, frame, functionLabels, statement.expression,
                                epilogueLabel, callFailureLabel) &&
                emitter.mov_local_eax(local_displacement(statement.localIndex));
        }
        case StatementKind::StoreLocal:
            return statement.localIndex < function.parameterStorageBytes / 4U + function.localStorageBytes / 4U &&
                emit_expression(emitter, unit, function, frame, functionLabels, statement.expression,
                                epilogueLabel, callFailureLabel) &&
                emitter.mov_local_eax(local_displacement(statement.localIndex));
        case StatementKind::StorePointer:
            if (statement.localIndex >= function.parameterStorageBytes / 4U + function.localStorageBytes / 4U)
                return false;
            {
                uint16_t pointerSlot = COMPILER_INVALID_INDEX;
                if (!emit_expression_value(emitter, unit, function, frame, functionLabels,
                                            statement.expression, epilogueLabel, callFailureLabel,
                                            &pointerSlot) ||
                    !emit_copy_pointer_descriptor(emitter, local_displacement(statement.localIndex))) return false;
                return pointerSlot == COMPILER_INVALID_INDEX ||
                       emitter.release_pointer_temporary_slots(1);
            }
        case StatementKind::StoreGlobal:
            if (statement.globalIndex >= unit.globalCount) return false;
            return emit_expression(emitter, unit, function, frame, functionLabels, statement.expression,
                                   epilogueLabel, callFailureLabel) &&
                emitter.emit_global_data_address(unit.globals[statement.globalIndex].name,
                                                 statement.location) && emitter.mov_rax_eax();
        case StatementKind::StoreIndexed: {
            const uint16_t baseIndex = statement.indexedBaseKind == IndexedBaseKind::Local
                ? statement.localIndex : statement.globalIndex;
            uint16_t temporaryBase = 0;
            if (!emitter.acquire_temporary_slots(1, &temporaryBase) ||
                !emit_expression(emitter, unit, function, frame, functionLabels, statement.expression,
                                 epilogueLabel, callFailureLabel) ||
                !emitter.mov_local_eax(temporary_displacement(frame, temporaryBase))) return false;
            uint16_t boundsFailureLabel = COMPILER_INVALID_INDEX;
            uint16_t endLabel = COMPILER_INVALID_INDEX;
            if (!emitter.create_label(&boundsFailureLabel) || !emitter.create_label(&endLabel) ||
                !emit_indexed_address(emitter, unit, function, frame, functionLabels,
                                      statement.indexExpression, statement.indexedBaseKind,
                                      baseIndex, statement.elementCount, statement.location,
                                      boundsFailureLabel, epilogueLabel, callFailureLabel) ||
                !emitter.mov_eax_local(temporary_displacement(frame, temporaryBase)) ||
                !emitter.mov_rax_eax() || !emitter.emit_jmp(endLabel) ||
                !emitter.define_label(boundsFailureLabel) ||
                !emit_array_bounds_failure(emitter, epilogueLabel) ||
                !emitter.define_label(endLabel) ||
                !emitter.release_temporary_slots(1)) return false;
            return true;
        }
        case StatementKind::StoreIndirectInt32:
        case StatementKind::StoreField: {
            uint16_t temporaryBase = 0;
            if (!emitter.acquire_temporary_slots(1, &temporaryBase) ||
                !emit_expression(emitter, unit, function, frame, functionLabels, statement.expression,
                                 epilogueLabel, callFailureLabel) ||
                !emitter.mov_local_eax(temporary_displacement(frame, temporaryBase))) return false;
            uint16_t failureLabel = COMPILER_INVALID_INDEX;
            uint16_t endLabel = COMPILER_INVALID_INDEX;
            uint16_t pointerTargetSlot = COMPILER_INVALID_INDEX;
            if (!emitter.create_label(&failureLabel) || !emitter.create_label(&endLabel) ||
                !emit_expression_value(emitter, unit, function, frame, functionLabels,
                                       statement.targetExpression, epilogueLabel, callFailureLabel,
                                       &pointerTargetSlot) ||
                !emit_pointer_validation(emitter, failureLabel) ||
                !emitter.mov_rdx_ptr64(0) ||
                !emitter.mov_eax_local(temporary_displacement(frame, temporaryBase)) ||
                !emitter.mov_rax_eax() || !emitter.emit_jmp(endLabel) ||
                !emitter.define_label(failureLabel) || !emit_pointer_failure(emitter, epilogueLabel) ||
                !emitter.define_label(endLabel) || !emitter.release_temporary_slots(1)) return false;
            if (pointerTargetSlot != COMPILER_INVALID_INDEX &&
                !emitter.release_pointer_temporary_slots(1)) return false;
            return true;
        }
        case StatementKind::EvaluateExpression:
            return emit_expression(emitter, unit, function, frame, functionLabels, statement.expression,
                                   epilogueLabel, callFailureLabel);
        case StatementKind::HostLog:
            return function.usesAppContext && emit_log(emitter, function, statement, dataAddress, true,
                                                       frame.contextDisplacement);
        case StatementKind::Return:
            return emit_expression(emitter, unit, function, frame, functionLabels, statement.expression,
                                   epilogueLabel, callFailureLabel) &&
                emitter.emit_jmp(epilogueLabel);
        case StatementKind::Break: {
            uint16_t target = COMPILER_INVALID_INDEX;
            return emitter.current_break_target(&target) && emitter.emit_jmp(target);
        }
        case StatementKind::Continue: {
            uint16_t target = COMPILER_INVALID_INDEX;
            return emitter.current_continue_target(&target) && emitter.emit_jmp(target);
        }
        case StatementKind::Block:
            return emit_block(emitter, unit, function, statement.thenBlock, dataAddress, frame,
                              functionLabels, epilogueLabel, callFailureLabel, depth + 1U, loopDepth);
        case StatementKind::If: {
            if (depth > COMPILER_MAX_CONDITIONAL_NESTING || statement.thenBlock >= function.blockCount ||
                (statement.elseBlock != COMPILER_INVALID_INDEX && statement.elseBlock >= function.blockCount) ||
                !emit_expression(emitter, unit, function, frame, functionLabels, statement.expression,
                                 epilogueLabel, callFailureLabel) || !emitter.test_eax_eax()) return false;
            uint16_t endLabel = COMPILER_INVALID_INDEX;
            if (!emitter.create_label(&endLabel)) return false;
            if (statement.elseBlock == COMPILER_INVALID_INDEX) {
                if (!emitter.emit_jz(endLabel) ||
                    !emit_block(emitter, unit, function, statement.thenBlock, dataAddress, frame, functionLabels,
                                epilogueLabel, callFailureLabel, depth + 1U, loopDepth) || !emitter.define_label(endLabel)) return false;
                return true;
            }
            uint16_t elseLabel = COMPILER_INVALID_INDEX;
            if (!emitter.create_label(&elseLabel) || !emitter.emit_jz(elseLabel) ||
                !emit_block(emitter, unit, function, statement.thenBlock, dataAddress, frame, functionLabels,
                            epilogueLabel, callFailureLabel, depth + 1U, loopDepth) ||
                !emitter.emit_jmp(endLabel) || !emitter.define_label(elseLabel) ||
                !emit_block(emitter, unit, function, statement.elseBlock, dataAddress, frame, functionLabels,
                            epilogueLabel, callFailureLabel, depth + 1U, loopDepth) ||
                !emitter.define_label(endLabel)) return false;
            return true;
        }
        case StatementKind::While: {
            uint16_t conditionLabel = COMPILER_INVALID_INDEX;
            uint16_t endLabel = COMPILER_INVALID_INDEX;
            if (loopDepth >= COMPILER_MAX_LOOP_NESTING || statement.thenBlock >= function.blockCount ||
                !emitter.create_label(&conditionLabel) || !emitter.create_label(&endLabel) ||
                !emitter.define_label(conditionLabel) ||
                !emit_expression(emitter, unit, function, frame, functionLabels, statement.expression,
                                 epilogueLabel, callFailureLabel) ||
                !emitter.test_eax_eax() || !emitter.emit_jz(endLabel) ||
                !emitter.push_loop_target(conditionLabel, endLabel)) return false;
            const bool body = emit_block(emitter, unit, function, statement.thenBlock, dataAddress, frame,
                                         functionLabels, epilogueLabel, callFailureLabel,
                                         depth + 1U, loopDepth + 1U);
            const bool popped = emitter.pop_loop_target();
            if (!body || !popped || !emitter.emit_jmp(conditionLabel) || !emitter.define_label(endLabel)) return false;
            return true;
        }
        default: return false;
    }
}

static bool emit_block(Emitter& emitter, const TranslationUnitIR& unit, const FunctionIR& function,
                       uint16_t blockIndex, uint64_t dataAddress, const FrameLayout& frame,
                       const uint16_t* functionLabels, uint16_t epilogueLabel,
                       uint16_t callFailureLabel,
                       uint32_t depth, uint32_t loopDepth)
{
    if (blockIndex >= function.blockCount || depth > COMPILER_MAX_BLOCK_NESTING) return false;
    const Block& block = function.blocks[blockIndex];
    uint16_t statementIndex = block.firstStatement;
    uint32_t visited = 0;
    while (statementIndex != COMPILER_INVALID_INDEX && visited++ < COMPILER_MAX_STATEMENTS) {
        if (statementIndex >= function.statementCount ||
            !emit_statement(emitter, unit, function, function.statements[statementIndex], dataAddress, frame,
                             functionLabels, epilogueLabel, callFailureLabel, depth, loopDepth)) return false;
        statementIndex = function.statements[statementIndex].nextStatement;
    }
    return statementIndex == COMPILER_INVALID_INDEX;
}

static bool emit_legacy_log(const FunctionIR& function, uint64_t dataAddress, Emitter& emitter)
{
    const Statement* log = nullptr;
    for (uint32_t i = 0; i < function.statementCount; ++i)
        if (function.statements[i].kind == StatementKind::HostLog) log = &function.statements[i];
    if (!log) return false;
    return emit_log(emitter, function, *log, dataAddress, false, 0) &&
        emitter.mov_eax_imm32(static_cast<uint32_t>(function.expressions[function.returnExpression].value));
}

} // namespace

bool calculate_signed_rel32(uint64_t targetAddress, uint64_t addressAfterBranch,
                            int32_t* displacement)
{
    if (!displacement) return false;
    if (targetAddress >= addressAfterBranch) {
        const uint64_t distance = targetAddress - addressAfterBranch;
        if (distance > 2147483647ULL) return false;
        *displacement = static_cast<int32_t>(distance);
        return true;
    }
    const uint64_t distance = addressAfterBranch - targetAddress;
    if (distance > 2147483648ULL) return false;
    if (distance == 2147483648ULL) *displacement = static_cast<int32_t>(0x80000000U);
    else *displacement = -static_cast<int32_t>(distance);
    return true;
}

bool calculate_frame_layout(uint32_t localCount, FrameLayout* output)
{
    if (!output || localCount > COMPILER_MAX_LOCAL_STORAGE_BYTES / 4U) return false;
    const uint32_t localBytes = localCount * 4U;
    const uint32_t requested = 40U + localBytes;
    uint32_t frameBytes = 0;
    if (!align16(requested, &frameBytes)) return false;
    output->frameBytes = frameBytes;
    output->contextDisplacement = -static_cast<int32_t>(localBytes + 8U);
    output->localBytes = localBytes;
    output->parameterBytes = 0;
    output->temporaryBytes = 0;
    output->pointerTemporaryBytes = 0;
    output->variableBytes = localBytes;
    output->transientBytes = 0;
    output->activationBytes = native_elf::NATIVE_ELF_RETURN_ADDRESS_BYTES +
        native_elf::NATIVE_ELF_SAVED_RBP_BYTES + frameBytes +
        native_elf::NATIVE_ELF_MAX_OUTGOING_CALL_RESERVE_BYTES;
    output->temporarySlots = 0;
    output->pointerTemporarySlots = 0;
    output->pointerScratchDisplacement = 0;
    return true;
}

bool calculate_frame_layout(uint32_t parameterCount, uint32_t localCount,
                            uint32_t temporarySlots, bool hasContext,
                            FrameLayout* output)
{
    return calculate_frame_layout(parameterCount, localCount, temporarySlots, hasContext,
                                   0, output);
}

bool calculate_frame_layout(uint32_t parameterCount, uint32_t localCount,
                            uint32_t temporarySlots, bool hasContext,
                            uint32_t transientBytes, FrameLayout* output)
{
    if (parameterCount > COMPILER_MAX_PARAMETERS) return false;
    return calculate_pointer_frame_layout(parameterCount * 4U, localCount * 4U,
                                          temporarySlots, 0, hasContext,
                                          transientBytes, false, output);
}

bool calculate_pointer_frame_layout(uint32_t parameterBytes, uint32_t localBytes,
                                    uint32_t temporarySlots, uint32_t pointerTemporarySlots,
                                    bool hasContext, uint32_t transientBytes,
                                    bool pointerScratch, FrameLayout* output)
{
    if (!output || parameterBytes > COMPILER_MAX_PARAMETERS * COMPILER_POINTER_DESCRIPTOR_BYTES ||
        localBytes > COMPILER_MAX_LOCAL_STORAGE_BYTES ||
        temporarySlots > COMPILER_MAX_TEMPORARY_SLOTS ||
        pointerTemporarySlots > COMPILER_MAX_POINTER_TEMPORARY_SLOTS ||
        transientBytes > COMPILER_MAX_TRANSIENT_STACK_BYTES) return false;
    const uint32_t temporaryBytes = temporarySlots * 4U;
    const uint32_t pointerTemporaryBytes = pointerTemporarySlots * COMPILER_POINTER_DESCRIPTOR_BYTES;
    const uint32_t scratchBytes = pointerScratch ? COMPILER_POINTER_DESCRIPTOR_BYTES : 0U;
    const uint32_t requested = 40U + parameterBytes + localBytes + temporaryBytes +
        pointerTemporaryBytes + scratchBytes;
    uint32_t frameBytes = 0;
    if (!align16(requested, &frameBytes)) return false;
    output->frameBytes = frameBytes;
    output->contextDisplacement = hasContext
        ? -static_cast<int32_t>(parameterBytes + localBytes + 8U) : 0;
    output->localBytes = localBytes;
    output->parameterBytes = parameterBytes;
    output->temporaryBytes = temporaryBytes;
    output->pointerTemporaryBytes = pointerTemporaryBytes;
    output->variableBytes = parameterBytes + localBytes;
    output->transientBytes = transientBytes;
    output->activationBytes = native_elf::NATIVE_ELF_RETURN_ADDRESS_BYTES +
        native_elf::NATIVE_ELF_SAVED_RBP_BYTES + frameBytes + transientBytes +
        native_elf::NATIVE_ELF_MAX_OUTGOING_CALL_RESERVE_BYTES;
    output->temporarySlots = static_cast<uint16_t>(temporarySlots);
    output->pointerTemporarySlots = static_cast<uint16_t>(pointerTemporarySlots);
    output->pointerScratchDisplacement = pointerScratch
        ? -static_cast<int32_t>(parameterBytes + localBytes + (hasContext ? 8U : 0U) +
                                8U + temporaryBytes + pointerTemporaryBytes + scratchBytes)
        : 0;
    return output->activationBytes <= COMPILER_MAX_GENERATED_ACTIVATION_STACK_COST;
}

static bool emit_translation_unit_impl(const TranslationUnitIR& unit, uint64_t readOnlyDataAddress,
                                       uint8_t* output, uint32_t outputCapacity, uint32_t* outputSize,
                                       uint32_t* entryCodeOffset,
                                       RelocationRecord* relocations,
                                       uint32_t relocationCapacity,
                                       uint32_t* relocationCount)
{
    static_assert(offsetof(gx_app_context, host) == 8, "generated gx_app_context host offset changed");
    static_assert(offsetof(gx_host_calls, log) == 8, "generated gx_host_calls log offset changed");
    if (outputSize) *outputSize = 0;
    if (entryCodeOffset) *entryCodeOffset = 0;
    if (relocationCount) *relocationCount = 0;
    if (!output || !outputSize || !entryCodeOffset || outputCapacity == 0 ||
         unit.functionCount > COMPILER_MAX_FUNCTIONS ||
         unit.globalCount > COMPILER_MAX_GLOBALS ||
        (unit.entryFunction != COMPILER_INVALID_INDEX && unit.entryFunction >= unit.functionCount)) return false;
    if (unit.functionCount == 0) {
        *outputSize = 0;
        *entryCodeOffset = 0;
        return true;
    }
    for (uint32_t i = 0; i < unit.functionCount; ++i) {
        const FunctionIR& function = unit.functions[i];
        if (function.rootBlock == COMPILER_INVALID_INDEX || function.rootBlock >= function.blockCount ||
            function.returnExpression == COMPILER_INVALID_INDEX || function.returnExpression >= function.expressionCount ||
            function.returnCount == 0 || function.parameterCount > COMPILER_MAX_PARAMETERS ||
            function.integerParameterCount > function.parameterCount || function.localCount > COMPILER_MAX_LOCALS ||
            function.callCount > COMPILER_MAX_CALL_EXPRESSIONS ||
            function.callArgumentCount > COMPILER_MAX_CALL_ARGUMENT_NODES ||
            function.statementCount > COMPILER_MAX_STATEMENTS || function.blockCount > COMPILER_MAX_BLOCKS ||
            function.expressionCount > COMPILER_MAX_EXPRESSION_NODES ||
            function.localStorageBytes > COMPILER_MAX_LOCAL_STORAGE_BYTES ||
            function.parameterStorageBytes > COMPILER_MAX_PARAMETERS * COMPILER_POINTER_DESCRIPTOR_BYTES ||
            function.stringCount > COMPILER_MAX_STRING_LITERALS ||
            function.usesAppContext != (function.parameterCount == 1 &&
                                        function.parameters[0].kind == ParameterKind::AppContextPointer) ||
            (function.callCount != 0 && !function.calls) ||
            (function.callArgumentCount != 0 && !function.callArguments)) return false;
        const uint32_t logs = host_log_count(function);
        const bool relocatableData = relocations != nullptr && relocationCount != nullptr;
        if (function.hasHostLog && (!function.usesAppContext ||
                                    (!relocatableData && readOnlyDataAddress == 0) ||
                                    function.stringCount == 0 || logs == 0)) return false;
    }

    Emitter emitter(output, outputCapacity, relocations, relocationCapacity, relocationCount);
    uint16_t functionLabels[COMPILER_MAX_FUNCTIONS] = {};
    FrameLayout frames[COMPILER_MAX_FUNCTIONS] = {};
    for (uint32_t i = 0; i < unit.functionCount; ++i) {
        if (!emitter.create_label(&functionLabels[i])) return false;
        uint16_t needed = 0;
        uint32_t transientBytes = 0;
        for (uint32_t e = 0; e < unit.functions[i].expressionCount; ++e) {
            uint16_t expressionNeeded = 0;
            uint32_t expressionTransient = 0;
            if (!required_temporary_slots(unit.functions[i], static_cast<uint16_t>(e), 0, &expressionNeeded) ||
                !required_transient_stack_bytes(unit.functions[i], static_cast<uint16_t>(e), 0,
                                                &expressionTransient)) return false;
            if (expressionNeeded > needed) needed = expressionNeeded;
            if (expressionTransient > transientBytes) transientBytes = expressionTransient;
        }
        uint16_t statementNeeded = 0;
        uint16_t statementPointerNeeded = 0;
        if (!required_statement_temporary_slots(unit.functions[i], &statementNeeded,
                                                 &statementPointerNeeded)) return false;
        if (statementNeeded > needed) needed = statementNeeded;
        uint16_t pointerNeeded = 0;
        for (uint32_t e = 0; e < unit.functions[i].expressionCount; ++e) {
            uint16_t expressionPointerNeeded = 0;
            if (!required_pointer_temporary_slots(unit.functions[i], static_cast<uint16_t>(e), 0,
                                                   &expressionPointerNeeded)) return false;
            if (expressionPointerNeeded > pointerNeeded) pointerNeeded = expressionPointerNeeded;
        }
        if (statementPointerNeeded > pointerNeeded) pointerNeeded = statementPointerNeeded;
        bool pointerScratch = false;
        for (uint32_t e = 0; e < unit.functions[i].expressionCount; ++e) {
            const ExpressionKind kind = unit.functions[i].expressions[e].kind;
            if (kind == ExpressionKind::AddressOfLocal || kind == ExpressionKind::AddressOfGlobal ||
                kind == ExpressionKind::AddressOfIndexed || kind == ExpressionKind::AddressOfStructLocal ||
                kind == ExpressionKind::AddressOfStructGlobal || kind == ExpressionKind::AddressOfField) pointerScratch = true;
        }
        if (needed > COMPILER_MAX_TEMPORARY_SLOTS ||
            !calculate_pointer_frame_layout(unit.functions[i].parameterStorageBytes,
                                            unit.functions[i].localStorageBytes,
                                            needed, pointerNeeded, unit.functions[i].usesAppContext,
                                            transientBytes, pointerScratch, &frames[i])) return false;
    }
    // The old one-function fast paths are retained exactly for regression
    // coverage.  All multi-function units use the framed path below.
    if (unit.functionCount == 1 && is_gx_main(unit.functions[0].name)) {
        const FunctionIR& function = unit.functions[0];
        if (is_legacy_single_log(function)) {
            emitter = Emitter(output, outputCapacity, relocations, relocationCapacity, relocationCount);
            if (!emit_legacy_log(function, readOnlyDataAddress, emitter) || !emitter.byte(0xC3) ||
                !emitter.patch_branches()) return false;
            *outputSize = emitter.size();
            *entryCodeOffset = 0;
            return true;
        }
        if (function.localCount == 0 && function.integerParameterCount == 0 && function.callCount == 0 &&
            !function.hasHostLog && !has_indexed_access(function) &&
            function.blockCount == 1 && function.statementCount == 1 &&
            function.returnCount == 1) {
            emitter = Emitter(output, outputCapacity, relocations, relocationCapacity, relocationCount);
            if (!emit_expression(emitter, unit, function, frames[0], functionLabels, function.returnExpression,
                                 COMPILER_INVALID_INDEX, COMPILER_INVALID_INDEX) ||
                !emitter.byte(0xC3) || !emitter.patch_branches()) return false;
            *outputSize = emitter.size();
            *entryCodeOffset = 0;
            return true;
        }
    }

    for (uint32_t i = 0; i < unit.functionCount; ++i) {
        const FunctionIR& function = unit.functions[i];
        const uint32_t start = emitter.size();
        const uint32_t functionStart = emitter.size();
        if (!emitter.define_label(functionLabels[i])) return false;
        const_cast<FunctionIR&>(unit.functions[i]).codeOffset = functionStart;
        if (i == unit.entryFunction) *entryCodeOffset = start;
        emitter.begin_function();
        const FrameLayout& frame = frames[i];
        if (!emitter.prologue(frame.frameBytes)) return false;
        if (i == unit.entryFunction && !emitter.initialize_runtime_state()) return false;
        if (function.usesAppContext && !emitter.mov_context_local(frame.contextDisplacement)) return false;
        static const uint8_t argumentRegisters[] = {1, 2, 8, 9};
        for (uint32_t p = 0; p < function.parameterCount; ++p) {
            if (function.parameters[p].kind == ParameterKind::Integer) {
                if (!emitter.mov_reg_local32(argumentRegisters[p],
                                             local_displacement(function.parameters[p].slot))) return false;
            } else if (parameter_kind_is_pointer(function.parameters[p].kind)) {
                if (!emitter.mov_rax_reg64(argumentRegisters[p]) ||
                    !emit_copy_pointer_descriptor(emitter,
                                                  local_displacement(function.parameters[p].slot))) return false;
            }
        }
        uint16_t epilogueLabel = COMPILER_INVALID_INDEX;
        uint16_t callFailureLabel = COMPILER_INVALID_INDEX;
        if (!emitter.create_label(&epilogueLabel) ||
            (function.callCount != 0 && !emitter.create_label(&callFailureLabel)) ||
            !emit_block(emitter, unit, function, function.rootBlock, readOnlyDataAddress, frame,
                        functionLabels, epilogueLabel, callFailureLabel, 0, 0) || emitter.loop_depth() != 0 ||
            (function.callCount != 0 && !emitter.emit_jmp(epilogueLabel))) return false;
        if (function.callCount != 0 &&
            (!emitter.define_label(callFailureLabel) || !emitter.mov_eax_imm32(0) ||
             !emitter.set_runtime_failure(COMPILER_RUNTIME_STATUS_CALL_DEPTH) ||
             !emitter.emit_jmp(epilogueLabel))) return false;
        if (!emitter.define_label(epilogueLabel) || !emitter.dec_r14d() || !emitter.epilogue()) return false;
        if (emitter.max_transient_bytes() > frame.transientBytes) return false;
    }
    if (!emitter.patch_branches() ||
        (unit.entryFunction != COMPILER_INVALID_INDEX && *entryCodeOffset >= emitter.size())) return false;
    *outputSize = emitter.size();
    return *outputSize != 0;
}

bool emit_translation_unit(const TranslationUnitIR& unit, uint64_t readOnlyDataAddress,
                           uint8_t* output, uint32_t outputCapacity, uint32_t* outputSize,
                           uint32_t* entryCodeOffset)
{
    return emit_translation_unit_impl(unit, readOnlyDataAddress, output, outputCapacity,
                                      outputSize, entryCodeOffset, nullptr, 0, nullptr);
}

bool emit_translation_unit_module(const TranslationUnitIR& unit,
                                  uint8_t* output, uint32_t outputCapacity,
                                  uint32_t* outputSize, uint32_t* entryCodeOffset,
                                  RelocationRecord* relocations,
                                  uint32_t relocationCapacity,
                                  uint32_t* relocationCount)
{
    if (!relocations || !relocationCount) return false;
    return emit_translation_unit_impl(unit, 0, output, outputCapacity, outputSize,
                                      entryCodeOffset, relocations, relocationCapacity,
                                      relocationCount);
}

bool emit_function(const FunctionIR& function, uint64_t readOnlyDataAddress,
                   uint8_t* output, uint32_t outputCapacity, uint32_t* outputSize)
{
    static TranslationUnitIR unit = {};
    unit = {};
    unit.functionCount = 1;
    unit.entryFunction = 0;
    unit.functions[0] = function;
    unit.functions[0].functionIndex = 0;
    unit.functionSymbols[0].functionIndex = 0;
    for (uint32_t i = 0; i < COMPILER_FUNCTION_NAME_CAPACITY; ++i) unit.functionSymbols[0].name[i] = function.name[i];
    if (function.callCount != 0) return false;
    uint32_t entryOffset = 0;
    return emit_translation_unit(unit, readOnlyDataAddress, output, outputCapacity, outputSize, &entryOffset);
}

bool emit_function(const FunctionIR& function, uint8_t* output,
                   uint32_t outputCapacity, uint32_t* outputSize)
{
    return emit_function(function, 0, output, outputCapacity, outputSize);
}

} // namespace amd64
} // namespace compiler
} // namespace kernel
