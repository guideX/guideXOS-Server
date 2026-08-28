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

static int32_t local_displacement(uint16_t slot)
{
    return -static_cast<int32_t>(4U * (static_cast<uint32_t>(slot) + 1U));
}

class Emitter {
public:
    Emitter(uint8_t* output, uint32_t capacity) : m_output(output), m_capacity(capacity), m_offset(0) {}

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

    bool mov_eax_imm32(uint32_t value) { return byte(0xB8) && u32(value); }
    bool mov_eax_local(int32_t displacement) {
        return bytes(reinterpret_cast<const uint8_t*>("\x8B\x85"), 2) && u32(static_cast<uint32_t>(displacement));
    }
    bool mov_local_eax(int32_t displacement) {
        return bytes(reinterpret_cast<const uint8_t*>("\x89\x85"), 2) && u32(static_cast<uint32_t>(displacement));
    }
    bool mov_context_local(int32_t displacement) {
        return bytes(reinterpret_cast<const uint8_t*>("\x48\x89\x8D"), 3) && u32(static_cast<uint32_t>(displacement));
    }
    bool mov_rcx_context_local(int32_t displacement) {
        return bytes(reinterpret_cast<const uint8_t*>("\x48\x8B\x8D"), 3) && u32(static_cast<uint32_t>(displacement));
    }
    bool mov_rdx_imm64(uint64_t address) {
        return bytes(reinterpret_cast<const uint8_t*>("\x48\xBA"), 2) && u64(address);
    }
    bool load_host_log() {
        static const uint8_t first[] = {0x48, 0x8B, 0x41, static_cast<uint8_t>(offsetof(gx_app_context, host))};
        static const uint8_t second[] = {0x48, 0x8B, 0x40, static_cast<uint8_t>(offsetof(gx_host_calls, log))};
        return bytes(first, sizeof(first)) && bytes(second, sizeof(second));
    }
    bool add_eax_ecx() { static const uint8_t v[] = {0x01, 0xC8}; return bytes(v, sizeof(v)); }
    bool sub_eax_ecx() { static const uint8_t v[] = {0x29, 0xC8}; return bytes(v, sizeof(v)); }
    bool imul_eax_ecx() { static const uint8_t v[] = {0x0F, 0xAF, 0xC1}; return bytes(v, sizeof(v)); }
    bool neg_eax() { static const uint8_t v[] = {0xF7, 0xD8}; return bytes(v, sizeof(v)); }
    bool push_rax() { return byte(0x50); }
    bool pop_rcx() { return byte(0x59); }
    bool sub_rsp(uint32_t value) {
        if (value <= 127U) return bytes(reinterpret_cast<const uint8_t*>("\x48\x83\xEC"), 3) && byte(static_cast<uint8_t>(value));
        return bytes(reinterpret_cast<const uint8_t*>("\x48\x81\xEC"), 3) && u32(value);
    }
    bool add_rsp(uint32_t value) {
        if (value <= 127U) return bytes(reinterpret_cast<const uint8_t*>("\x48\x83\xC4"), 3) && byte(static_cast<uint8_t>(value));
        return bytes(reinterpret_cast<const uint8_t*>("\x48\x81\xC4"), 3) && u32(value);
    }
    bool prologue(uint32_t frameBytes) {
        return byte(0x55) && bytes(reinterpret_cast<const uint8_t*>("\x48\x89\xE5"), 3) && sub_rsp(frameBytes);
    }
    bool epilogue() { return bytes(reinterpret_cast<const uint8_t*>("\x48\x89\xEC\x5D\xC3"), 5); }
    bool call_rax() { return bytes(reinterpret_cast<const uint8_t*>("\xFF\xD0"), 2); }

private:
    uint8_t* m_output;
    uint32_t m_capacity;
    uint32_t m_offset;
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
    return function.localCount == 0 && host_log_count(function) == 1 &&
           function.returnExpression < function.expressionCount &&
           function.expressions[function.returnExpression].kind == ExpressionKind::Constant;
}

static bool emit_log(Emitter& emitter, const FunctionIR& function, const Statement& statement,
                     uint64_t dataAddress, bool framed, int32_t contextDisplacement)
{
    if (statement.stringIndex >= function.stringCount || dataAddress == 0) return false;
    const uint64_t address = dataAddress + function.stringOffsets[statement.stringIndex];
    (void)contextDisplacement;
    if (framed) {
        // The frame prologue saved the incoming context. Reload it before
        // every independent host call because RCX is volatile across calls.
        if (!emitter.mov_rcx_context_local(contextDisplacement)) return false;
    }
    // The legacy one-log fast path intentionally uses the incoming RCX and
    // the historical 0x28 reservation.  Framed calls already have an aligned
    // RSP and only need the Microsoft x64 32-byte home area.
    if (!emitter.mov_rdx_imm64(address) || !emitter.load_host_log()) return false;
    if (!emitter.sub_rsp(framed ? 0x20U : 0x28U) || !emitter.call_rax() ||
        !emitter.add_rsp(framed ? 0x20U : 0x28U)) return false;
    return true;
}

static bool emit_expression(Emitter& emitter, const FunctionIR& function,
                            uint16_t index)
{
    if (index == COMPILER_INVALID_INDEX || index >= function.expressionCount) return false;
    const Expression& expression = function.expressions[index];
    switch (expression.kind) {
        case ExpressionKind::Constant:
            return emitter.mov_eax_imm32(static_cast<uint32_t>(expression.value));
        case ExpressionKind::LoadLocal:
            if (expression.localIndex >= function.localCount) return false;
            return emitter.mov_eax_local(local_displacement(expression.localIndex));
        case ExpressionKind::Negate:
            return emit_expression(emitter, function, expression.left) && emitter.neg_eax();
        case ExpressionKind::Add:
        case ExpressionKind::Subtract:
        case ExpressionKind::Multiply:
            if (!emit_expression(emitter, function, expression.left) || !emitter.push_rax() ||
                !emit_expression(emitter, function, expression.right) || !emitter.pop_rcx()) return false;
            if (expression.kind == ExpressionKind::Add) return emitter.add_eax_ecx();
            if (expression.kind == ExpressionKind::Subtract) {
                // The left operand is on the stack. Exchange the two 32-bit
                // values using a scratch register so the operation is left-right.
                // EAX=right, ECX=left: xchg is unnecessary; use the fixed
                // register sequence supported by this tiny backend.
                return emitter.bytes(reinterpret_cast<const uint8_t*>("\x91"), 1) && emitter.sub_eax_ecx();
            }
            return emitter.imul_eax_ecx();
        default: return false;
    }
}

static bool emit_legacy_log(const FunctionIR& function, uint64_t dataAddress,
                            Emitter& emitter)
{
    const Statement* log = nullptr;
    for (uint32_t i = 0; i < function.statementCount; ++i)
        if (function.statements[i].kind == StatementKind::HostLog) log = &function.statements[i];
    if (!log) return false;
    return emit_log(emitter, function, *log, dataAddress, false, 0) &&
        emit_expression(emitter, function, function.returnExpression) && emitter.byte(0xC3);
}

} // namespace

bool calculate_frame_layout(uint32_t localCount, FrameLayout* output)
{
    if (!output || localCount > COMPILER_MAX_LOCALS) return false;
    const uint32_t localBytes = localCount * 4U;
    const uint32_t requested = 40U + localBytes; // 32-byte home area + saved ctx slot + locals
    uint32_t frameBytes = 0;
    if (!align16(requested, &frameBytes)) return false;
    output->frameBytes = frameBytes;
    output->contextDisplacement = -static_cast<int32_t>(localBytes + 8U);
    output->localBytes = localBytes;
    return true;
}

bool emit_function(const FunctionIR& function, uint64_t readOnlyDataAddress,
                   uint8_t* output, uint32_t outputCapacity, uint32_t* outputSize)
{
    static_assert(offsetof(gx_app_context, host) == 8, "generated gx_app_context host offset changed");
    static_assert(offsetof(gx_host_calls, log) == 8, "generated gx_host_calls log offset changed");
    if (!output || !outputSize || outputCapacity == 0 || !is_gx_main(function.name) ||
        function.returnExpression == COMPILER_INVALID_INDEX ||
        function.returnExpression >= function.expressionCount) return false;
    const uint32_t logCount = host_log_count(function);
    if (function.hasHostLog && (!function.usesAppContext || readOnlyDataAddress == 0 ||
                                function.stringCount == 0 || logCount == 0)) return false;

    Emitter emitter(output, outputCapacity);
    if (is_legacy_single_log(function)) {
        if (!emit_legacy_log(function, readOnlyDataAddress, emitter)) return false;
        *outputSize = emitter.size();
        return true;
    }

    const bool framed = function.localCount != 0 || logCount > 1;
    FrameLayout frame = {};
    if (framed && !calculate_frame_layout(function.localCount, &frame)) return false;
    if (framed) {
        if (!emitter.prologue(frame.frameBytes) || !emitter.mov_context_local(frame.contextDisplacement)) return false;
    }

    for (uint32_t i = 0; i < function.statementCount; ++i) {
        const Statement& statement = function.statements[i];
        switch (statement.kind) {
            case StatementKind::DeclareLocal:
            case StatementKind::StoreLocal:
                if (statement.localIndex >= function.localCount ||
                    !emit_expression(emitter, function, statement.expression) ||
                    !emitter.mov_local_eax(local_displacement(statement.localIndex))) return false;
                break;
            case StatementKind::HostLog:
                if (!emit_log(emitter, function, statement, readOnlyDataAddress,
                              framed, frame.contextDisplacement)) return false;
                break;
            case StatementKind::Return:
                if (!emit_expression(emitter, function, statement.expression)) return false;
                if (framed && !emitter.epilogue()) return false;
                if (!framed && !emitter.byte(0xC3)) return false;
                break;
            default: return false;
        }
    }
    *outputSize = emitter.size();
    return *outputSize != 0;
}

bool emit_function(const FunctionIR& function, uint8_t* output,
                   uint32_t outputCapacity, uint32_t* outputSize)
{
    return emit_function(function, 0, output, outputCapacity, outputSize);
}

} // namespace amd64
} // namespace compiler
} // namespace kernel
