#include "value.h"
#include "runtime_string_storage.h"

namespace gxos {
namespace javascript {

Value Value::undefined()
{
    return Value();
}

Value Value::nullValue()
{
    Value value;
    value.type_ = ValueType::Null;
    return value;
}

Value Value::boolean(bool boolean)
{
    Value value;
    value.type_ = ValueType::Boolean;
    value.payload_.booleanValue = boolean;
    return value;
}

Value Value::number(double number)
{
    Value value;
    value.type_ = ValueType::Number;
    value.payload_.numberValue = number;
    return value;
}

Value Value::string(RuntimeStringId id)
{
    Value value;
    value.type_ = ValueType::String;
    value.payload_.stringId = id;
    return value;
}

Value Value::function(RuntimeFunctionId id)
{
    Value value;
    value.type_ = ValueType::Function;
    value.payload_.functionId = id;
    return value;
}

Value Value::object(RuntimeObjectId id)
{
    Value value;
    value.type_ = ValueType::Object;
    value.payload_.objectId = id;
    return value;
}

Value Value::hostObject(RuntimeHostObjectId id)
{
    Value value;
    value.type_ = ValueType::HostObject;
    value.payload_.hostObjectId = id;
    return value;
}

Value Value::wellKnownSymbol(WellKnownSymbol symbol)
{
    Value value;
    value.type_ = ValueType::Symbol;
    value.payload_.stringId = static_cast<RuntimeStringId>(symbol);
    return value;
}

Value Value::symbolNamespace()
{
    Value value;
    value.type_ = ValueType::SymbolNamespace;
    return value;
}

Value Value::objectNamespace()
{
    Value value;
    value.type_ = ValueType::ObjectNamespace;
    return value;
}

Value Value::jsonNamespace()
{
    Value value;
    value.type_ = ValueType::JsonNamespace;
    return value;
}

bool Value::booleanValue() const
{
    return type_ == ValueType::Boolean ? payload_.booleanValue : false;
}

double Value::numberValue() const
{
    return type_ == ValueType::Number ? payload_.numberValue : 0.0;
}

RuntimeStringId Value::stringId() const
{
    return type_ == ValueType::String || type_ == ValueType::Symbol
        ? payload_.stringId : kInvalidRuntimeStringId;
}

RuntimeFunctionId Value::functionId() const
{
    return type_ == ValueType::Function ? payload_.functionId
                                        : kInvalidRuntimeFunctionId;
}

RuntimeObjectId Value::objectId() const
{
    return type_ == ValueType::Object ? payload_.objectId
                                      : kInvalidRuntimeObjectId;
}

RuntimeHostObjectId Value::hostObjectId() const
{
    return type_ == ValueType::HostObject ? payload_.hostObjectId
                                          : kInvalidRuntimeHostObjectId;
}

WellKnownSymbol Value::wellKnownSymbolValue() const
{
    return type_ == ValueType::Symbol
        ? static_cast<WellKnownSymbol>(payload_.stringId)
        : WellKnownSymbol::None;
}

const char* valueTypeName(ValueType type)
{
    switch (type) {
    case ValueType::Undefined: return "Undefined";
    case ValueType::Null: return "Null";
    case ValueType::Boolean: return "Boolean";
    case ValueType::Number: return "Number";
    case ValueType::String: return "String";
    case ValueType::Function: return "Function";
    case ValueType::Object: return "Object";
    case ValueType::HostObject: return "HostObject";
    case ValueType::Symbol: return "Symbol";
    case ValueType::SymbolNamespace: return "SymbolNamespace";
    case ValueType::ObjectNamespace: return "ObjectNamespace";
    case ValueType::JsonNamespace: return "JsonNamespace";
    }
    return "Invalid";
}

bool RuntimeStringStore::configure(RuntimeStringStorageView storage,
    RuntimeGeneration generation, RuntimeStringError& error)
{
    const RuntimeGeneration previousGeneration = generation_;
    valid_ = false;
    storage_ = RuntimeStringStorageView();
    generation_ = previousGeneration;
    count_ = 0u;
    bytesUsed_ = 0u;
    error.code = RuntimeStringErrorCode::None;

    if (generation == kInvalidRuntimeGeneration) {
        error.code = RuntimeStringErrorCode::InvalidStorage;
        return false;
    }
    if (previousGeneration != kInvalidRuntimeGeneration &&
        generation <= previousGeneration) {
        error.code = RuntimeStringErrorCode::StaleGeneration;
        return false;
    }
    if (
        storage.recordCapacity > kRuntimeHandleInvalidPart ||
        (storage.records == nullptr && storage.recordCapacity != 0u) ||
        (storage.bytes == nullptr && storage.byteCapacity != 0u)) {
        error.code = RuntimeStringErrorCode::InvalidStorage;
        return false;
    }
    if (storage.records != nullptr &&
        reinterpret_cast<std::uintptr_t>(storage.records) %
            alignof(RuntimeStringRecord) != 0u) {
        error.code = RuntimeStringErrorCode::InvalidAlignment;
        return false;
    }
    if (storage.recordCapacity >
            static_cast<std::size_t>(-1) / sizeof(RuntimeStringRecord)) {
        error.code = RuntimeStringErrorCode::SizeOverflow;
        return false;
    }
    const std::size_t recordBytes =
        storage.recordCapacity * sizeof(RuntimeStringRecord);
    const std::uintptr_t addressMaximum =
        static_cast<std::uintptr_t>(-1);
    const std::uintptr_t recordAddress =
        reinterpret_cast<std::uintptr_t>(storage.records);
    const std::uintptr_t byteAddress =
        reinterpret_cast<std::uintptr_t>(storage.bytes);
    if ((storage.records != nullptr && recordBytes >
            static_cast<std::size_t>(addressMaximum - recordAddress)) ||
        (storage.bytes != nullptr && storage.byteCapacity >
            static_cast<std::size_t>(addressMaximum - byteAddress))) {
        error.code = RuntimeStringErrorCode::SizeOverflow;
        return false;
    }

    storage_ = storage;
    generation_ = generation;
    valid_ = true;
    return true;
}

void RuntimeStringStore::reset(RuntimeGeneration generation)
{
    count_ = 0u;
    bytesUsed_ = 0u;
    if (generation == kInvalidRuntimeGeneration) {
        valid_ = false;
        return;
    }
    if (!valid_ || generation <= generation_) {
        valid_ = false;
        return;
    }
    generation_ = generation;
}

bool RuntimeStringStore::create(SourceView text, std::size_t maximumLength,
    RuntimeStringId& result, RuntimeStringError& error)
{
    result = kInvalidRuntimeStringId;
    error.code = RuntimeStringErrorCode::None;
    if (!valid_) {
        error.code = RuntimeStringErrorCode::InvalidStorage;
        return false;
    }
    if (text.data == nullptr && text.length != 0u) {
        error.code = RuntimeStringErrorCode::InvalidInput;
        return false;
    }
    if (text.length > maximumLength) {
        error.code = RuntimeStringErrorCode::CapacityExceeded;
        return false;
    }
    if (count_ >= storage_.recordCapacity ||
        count_ >= kRuntimeHandleInvalidPart ||
        bytesUsed_ > storage_.byteCapacity ||
        text.length > storage_.byteCapacity - bytesUsed_) {
        error.code = RuntimeStringErrorCode::CapacityExceeded;
        return false;
    }
    if (text.length > static_cast<std::size_t>(-1) - bytesUsed_) {
        error.code = RuntimeStringErrorCode::SizeOverflow;
        return false;
    }

    if (text.length != 0u) {
        for (std::size_t index = 0u; index < text.length; ++index)
            storage_.bytes[bytesUsed_ + index] = text.data[index];
    }
    storage_.records[count_] = RuntimeStringRecord{bytesUsed_, text.length};
    result = makeRuntimeHandle(generation_,
        static_cast<std::uint32_t>(count_));
    if (result == kInvalidRuntimeStringId) {
        error.code = RuntimeStringErrorCode::InternalInvariantFailure;
        return false;
    }
    bytesUsed_ += text.length;
    ++count_;
    return true;
}

bool RuntimeStringStore::view(RuntimeStringId id, SourceView& result,
    RuntimeStringError& error) const
{
    result = SourceView();
    error.code = RuntimeStringErrorCode::None;
    if (!valid_) {
        error.code = RuntimeStringErrorCode::InvalidStorage;
        return false;
    }
    if (id == kInvalidRuntimeStringId) {
        error.code = RuntimeStringErrorCode::InvalidReference;
        return false;
    }
    if (runtimeHandleGeneration(id) != generation_) {
        error.code = RuntimeStringErrorCode::StaleGeneration;
        return false;
    }
    const std::size_t slot = runtimeHandleSlot(id);
    if (slot >= count_) {
        error.code = RuntimeStringErrorCode::InvalidReference;
        return false;
    }
    const RuntimeStringRecord& record = storage_.records[slot];
    if (record.offset > bytesUsed_ ||
        record.length > bytesUsed_ - record.offset ||
        (record.length != 0u && storage_.bytes == nullptr)) {
        error.code = RuntimeStringErrorCode::InternalInvariantFailure;
        return false;
    }
    const char* data = storage_.bytes == nullptr
        ? nullptr : storage_.bytes + record.offset;
    result = SourceView(data, record.length);
    return true;
}

bool RuntimeStringStore::restore(RuntimeStringMark mark,
    RuntimeStringError& error)
{
    error.code = RuntimeStringErrorCode::None;
    if (!valid_) {
        error.code = RuntimeStringErrorCode::InvalidStorage;
        return false;
    }
    if (mark.generation != generation_) {
        error.code = RuntimeStringErrorCode::StaleGeneration;
        return false;
    }
    if (mark.count > count_ || mark.bytesUsed > bytesUsed_ ||
        mark.count > storage_.recordCapacity ||
        mark.bytesUsed > storage_.byteCapacity) {
        error.code = RuntimeStringErrorCode::InvalidReference;
        return false;
    }
    std::size_t expectedBytes = 0u;
    for (std::size_t index = 0u; index < mark.count; ++index) {
        const RuntimeStringRecord& record = storage_.records[index];
        if (record.offset != expectedBytes || expectedBytes > mark.bytesUsed ||
            record.length > mark.bytesUsed - expectedBytes) {
            error.code = RuntimeStringErrorCode::InvalidReference;
            return false;
        }
        expectedBytes += record.length;
    }
    if (expectedBytes != mark.bytesUsed) {
        error.code = RuntimeStringErrorCode::InvalidReference;
        return false;
    }
    count_ = mark.count;
    bytesUsed_ = mark.bytesUsed;
    return true;
}

const char* runtimeStringErrorCodeName(RuntimeStringErrorCode code)
{
    switch (code) {
    case RuntimeStringErrorCode::None: return "None";
    case RuntimeStringErrorCode::InvalidStorage: return "InvalidStorage";
    case RuntimeStringErrorCode::InvalidAlignment: return "InvalidAlignment";
    case RuntimeStringErrorCode::InvalidInput: return "InvalidInput";
    case RuntimeStringErrorCode::InvalidReference: return "InvalidReference";
    case RuntimeStringErrorCode::StaleGeneration: return "StaleGeneration";
    case RuntimeStringErrorCode::CapacityExceeded: return "CapacityExceeded";
    case RuntimeStringErrorCode::SizeOverflow: return "SizeOverflow";
    case RuntimeStringErrorCode::InternalInvariantFailure:
        return "InternalInvariantFailure";
    }
    return "Invalid";
}

} // namespace javascript
} // namespace gxos
