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
    return slot_displacement(frame.variableBytes / 4U + 2U + slot);
}

class Emitter {
public:
    Emitter(uint8_t* output, uint32_t capacity,
            RelocationRecord* relocations = nullptr,
            uint32_t relocationCapacity = 0,
            uint32_t* relocationCount = nullptr)
        : m_output(output), m_capacity(capacity), m_offset(0), m_labelCount(0), m_fixupCount(0),
          m_loopDepth(0), m_rspMod16(8), m_temporaryDepth(0), m_maxTemporaryDepth(0),
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
    bool mov_rax_eax() { static const uint8_t v[] = {0x89, 0x02}; return bytes(v, sizeof(v)); }
    bool mov_local_eax(int32_t displacement)
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x89\x85"), 2) && u32(static_cast<uint32_t>(displacement));
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
    bool inc_r14d() { return bytes(reinterpret_cast<const uint8_t*>("\x41\xFF\xC6"), 3); }
    bool dec_r14d() { return bytes(reinterpret_cast<const uint8_t*>("\x41\xFF\xCE"), 3); }
    bool set_runtime_failure()
    {
        return bytes(reinterpret_cast<const uint8_t*>("\x41\xBF"), 2) &&
               u32(COMPILER_MAX_RUNTIME_CALL_DEPTH);
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
    bool emit_jae(uint16_t label) { return emit_branch(0x83, label); }
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
        expression.kind == ExpressionKind::LoadGlobal) {
        *output = 0;
        return true;
    }
    if (expression.kind == ExpressionKind::Negate)
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
        expression.kind == ExpressionKind::LoadGlobal) {
        *output = 0;
        return true;
    }
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
    if (expression.kind == ExpressionKind::Negate)
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

static bool emit_expression(Emitter& emitter, const TranslationUnitIR& unit,
                            const FunctionIR& function, const FrameLayout& frame,
                            const uint16_t* functionLabels, uint16_t index,
                            uint16_t epilogueLabel, uint16_t callFailureLabel)
{
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
        if (!emitter.acquire_temporary_slots(call.argumentCount, &base)) return false;
        for (uint32_t i = 0; i < call.argumentCount; ++i) {
            if (!emit_expression(emitter, unit, function, frame, functionLabels,
                                 function.callArguments[call.argumentStart + i],
                                 epilogueLabel, callFailureLabel) ||
                !emitter.mov_local_eax(temporary_displacement(frame, static_cast<uint16_t>(base + i)))) return false;
        }
        static const uint8_t argumentRegisters[] = {1, 2, 8, 9}; // ECX, EDX, R8D, R9D
        for (uint32_t i = 0; i < call.argumentCount; ++i)
            if (!emitter.mov_reg_mem32(argumentRegisters[i],
                                       temporary_displacement(frame, static_cast<uint16_t>(base + i)))) return false;
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
            emitter.release_temporary_slots(call.argumentCount);
    }
    switch (expression.kind) {
        case ExpressionKind::Constant:
            return emitter.mov_eax_imm32(static_cast<uint32_t>(expression.value));
        case ExpressionKind::LoadLocal:
            if (expression.localIndex >= function.integerParameterCount + function.localCount) return false;
            return emitter.mov_eax_local(local_displacement(expression.localIndex));
        case ExpressionKind::LoadGlobal:
            if (expression.globalIndex >= unit.globalCount) return false;
            return emitter.emit_global_data_address(unit.globals[expression.globalIndex].name,
                                                    expression.location) && emitter.mov_eax_rax();
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
        case StatementKind::DeclareLocal:
        case StatementKind::StoreLocal:
            return statement.localIndex < function.integerParameterCount + function.localCount &&
                emit_expression(emitter, unit, function, frame, functionLabels, statement.expression,
                                epilogueLabel, callFailureLabel) &&
                emitter.mov_local_eax(local_displacement(statement.localIndex));
        case StatementKind::StoreGlobal:
            if (statement.globalIndex >= unit.globalCount) return false;
            return emit_expression(emitter, unit, function, frame, functionLabels, statement.expression,
                                   epilogueLabel, callFailureLabel) &&
                emitter.emit_global_data_address(unit.globals[statement.globalIndex].name,
                                                 statement.location) && emitter.mov_rax_eax();
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
    if (!output || localCount > COMPILER_MAX_LOCALS) return false;
    const uint32_t localBytes = localCount * 4U;
    const uint32_t requested = 40U + localBytes;
    uint32_t frameBytes = 0;
    if (!align16(requested, &frameBytes)) return false;
    output->frameBytes = frameBytes;
    output->contextDisplacement = -static_cast<int32_t>(localBytes + 8U);
    output->localBytes = localBytes;
    output->parameterBytes = 0;
    output->temporaryBytes = 0;
    output->variableBytes = localBytes;
    output->transientBytes = 0;
    output->activationBytes = native_elf::NATIVE_ELF_RETURN_ADDRESS_BYTES +
        native_elf::NATIVE_ELF_SAVED_RBP_BYTES + frameBytes +
        native_elf::NATIVE_ELF_MAX_OUTGOING_CALL_RESERVE_BYTES;
    output->temporarySlots = 0;
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
    if (!output || parameterCount > COMPILER_MAX_PARAMETERS || localCount > COMPILER_MAX_LOCALS ||
        temporarySlots > COMPILER_MAX_TEMPORARY_SLOTS ||
        transientBytes > COMPILER_MAX_TRANSIENT_STACK_BYTES) return false;
    const uint32_t parameterBytes = parameterCount * 4U;
    const uint32_t localBytes = localCount * 4U;
    const uint32_t temporaryBytes = temporarySlots * 4U;
    const uint32_t requested = 40U + parameterBytes + localBytes + temporaryBytes;
    uint32_t frameBytes = 0;
    if (!align16(requested, &frameBytes)) return false;
    output->frameBytes = frameBytes;
    output->contextDisplacement = hasContext
        ? -static_cast<int32_t>(parameterBytes + localBytes + 8U) : 0;
    output->localBytes = localBytes;
    output->parameterBytes = parameterBytes;
    output->temporaryBytes = temporaryBytes;
    output->variableBytes = parameterBytes + localBytes;
    output->transientBytes = transientBytes;
    output->activationBytes = native_elf::NATIVE_ELF_RETURN_ADDRESS_BYTES +
        native_elf::NATIVE_ELF_SAVED_RBP_BYTES + frameBytes + transientBytes +
        native_elf::NATIVE_ELF_MAX_OUTGOING_CALL_RESERVE_BYTES;
    output->temporarySlots = static_cast<uint16_t>(temporarySlots);
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
            function.stringCount > COMPILER_MAX_STRING_LITERALS ||
            function.usesAppContext != (function.parameterCount == 1 && function.integerParameterCount == 0) ||
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
        if (needed > COMPILER_MAX_TEMPORARY_SLOTS ||
            !calculate_frame_layout(unit.functions[i].integerParameterCount, unit.functions[i].localCount,
                                     needed, unit.functions[i].usesAppContext, transientBytes,
                                     &frames[i])) return false;
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
            !function.hasHostLog && function.blockCount == 1 && function.statementCount == 1 &&
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
        for (uint32_t p = 0; p < function.integerParameterCount; ++p)
            if (!emitter.mov_reg_local32(argumentRegisters[p], local_displacement(static_cast<uint16_t>(p)))) return false;
        uint16_t epilogueLabel = COMPILER_INVALID_INDEX;
        uint16_t callFailureLabel = COMPILER_INVALID_INDEX;
        if (!emitter.create_label(&epilogueLabel) ||
            (function.callCount != 0 && !emitter.create_label(&callFailureLabel)) ||
            !emit_block(emitter, unit, function, function.rootBlock, readOnlyDataAddress, frame,
                        functionLabels, epilogueLabel, callFailureLabel, 0, 0) || emitter.loop_depth() != 0 ||
            (function.callCount != 0 && !emitter.emit_jmp(epilogueLabel))) return false;
        if (function.callCount != 0 &&
            (!emitter.define_label(callFailureLabel) || !emitter.mov_eax_imm32(0) ||
             !emitter.set_runtime_failure() ||
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
