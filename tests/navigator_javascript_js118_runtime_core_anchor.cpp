#include "navigator_javascript_js118_runtime_core_probe.h"

#include <cstddef>
#include <cstdint>

#include "navigator_javascript/environment.h"
#include "navigator_javascript/runtime_string_storage.h"
#include "navigator_javascript/value.h"

static_assert(sizeof(gxos::javascript::Value) == 16u,
    "JS118 AMD64 Value record size");
static_assert(sizeof(gxos::javascript::RuntimeStringRecord) == 16u,
    "JS118 AMD64 string metadata size");
static_assert(sizeof(gxos::javascript::EnvironmentRecord) == 24u,
    "JS118 AMD64 environment record size");
static_assert(sizeof(gxos::javascript::Binding) == 40u,
    "JS118 AMD64 binding record size");
static_assert(sizeof(gxos::javascript::EnvironmentStore) == 88u,
    "JS118 AMD64 environment arena metadata size");
static_assert(sizeof(gxos::javascript::RuntimeStringStore) == 64u,
    "JS118 AMD64 string arena metadata size");
static_assert(sizeof(gxos::javascript::Environment) == 32u,
    "JS118 AMD64 environment adapter size");
static_assert(alignof(gxos::javascript::Value) == 8u,
    "JS118 AMD64 Value alignment");
static_assert(alignof(gxos::javascript::RuntimeStringRecord) == 8u,
    "JS118 AMD64 string metadata alignment");
static_assert(alignof(gxos::javascript::EnvironmentRecord) == 4u,
    "JS118 AMD64 environment record alignment");
static_assert(alignof(gxos::javascript::Binding) == 8u,
    "JS118 AMD64 binding record alignment");

extern "C" std::uint32_t gxos_js118_runtime_core_probe(
    gxos::javascript::EnvironmentRecord* environmentRecords,
    std::size_t environmentCapacity, gxos::javascript::Binding* bindings,
    std::size_t bindingCapacity, char* bindingNameBytes,
    std::size_t bindingNameCapacity,
    gxos::javascript::RuntimeStringRecord* stringRecords,
    std::size_t stringRecordCapacity, char* stringBytes,
    std::size_t stringByteCapacity, std::uint16_t generation,
    gxos::javascript::Js118RuntimeCoreProbeResult* result)
{
    using namespace gxos::javascript;
    if (result == nullptr) return 1u;
    *result = Js118RuntimeCoreProbeResult();

    EnvironmentStore environments;
    EnvironmentError environmentError;
    const EnvironmentStorageView environmentStorage{
        environmentRecords, environmentCapacity, bindings, bindingCapacity,
        bindingNameBytes, bindingNameCapacity};
    if (!environments.configure(environmentStorage, generation,
            environmentError)) {
        result->status = 2u;
        result->environmentError = static_cast<std::uint8_t>(
            environmentError.code);
        return result->status;
    }
    if (!environments.createRoot(EnvironmentLimits{4u, 64u}, result->root,
            environmentError)) {
        result->status = 3u;
        result->environmentError = static_cast<std::uint8_t>(
            environmentError.code);
        return result->status;
    }
    if (!environments.create(result->root, EnvironmentKind::Block,
            EnvironmentLimits{4u, 64u}, result->child, environmentError)) {
        result->status = 4u;
        result->environmentError = static_cast<std::uint8_t>(
            environmentError.code);
        return result->status;
    }
    if (!environments.declare(result->root, SourceView("x", 1u),
            Value::number(1.0), environmentError)) {
        result->status = 5u;
        result->environmentError = static_cast<std::uint8_t>(
            environmentError.code);
        return result->status;
    }
    if (!environments.declareLexical(result->child, SourceView("x", 1u),
            false, environmentError, Value::undefined(), false)) {
        result->status = 6u;
        result->environmentError = static_cast<std::uint8_t>(
            environmentError.code);
        return result->status;
    }
    Value observed;
    if (environments.readChain(result->child, SourceView("x", 1u), observed,
            environmentError) || environmentError.code !=
                EnvironmentErrorCode::UninitializedBinding) {
        result->status = 7u;
        result->environmentError = static_cast<std::uint8_t>(
            environmentError.code);
        return result->status;
    }
    if (!environments.initializeChain(result->child, SourceView("x", 1u),
            Value::number(2.0), environmentError) ||
        !environments.assignChain(result->child, SourceView("x", 1u),
            Value::number(3.0), environmentError) ||
        !environments.readChain(result->child, SourceView("x", 1u), observed,
            environmentError)) {
        result->status = 8u;
        result->environmentError = static_cast<std::uint8_t>(
            environmentError.code);
        return result->status;
    }

    RuntimeStringStore strings;
    RuntimeStringError stringError;
    const RuntimeStringStorageView stringStorage{stringRecords,
        stringRecordCapacity, stringBytes, stringByteCapacity};
    if (!strings.configure(stringStorage, generation, stringError)) {
        result->status = 9u;
        result->stringError = static_cast<std::uint8_t>(stringError.code);
        return result->status;
    }
    if (!strings.create(SourceView("ok", 2u), 64u, result->text,
            stringError)) {
        result->status = 10u;
        result->stringError = static_cast<std::uint8_t>(stringError.code);
        return result->status;
    }
    SourceView storedText;
    if (!strings.view(result->text, storedText, stringError) ||
        storedText.length != 2u || storedText.data == nullptr) {
        result->status = 11u;
        result->stringError = static_cast<std::uint8_t>(stringError.code);
        return result->status;
    }

    result->valueType = static_cast<std::uint8_t>(Value::number(3.0).type());
    result->observedNumber = observed.numberValue();
    result->observedText[0] = storedText.data[0];
    result->observedText[1] = storedText.data[1];
    result->environmentCount = environments.environmentCount();
    result->bindingCount = environments.bindingCount();
    result->nameBytesUsed = environments.nameBytesUsed();
    result->stringCount = strings.count();
    result->stringBytesUsed = strings.bytesUsed();
    return 0u;
}
