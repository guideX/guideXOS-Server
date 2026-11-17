#include "object_storage.h"

#include <cstdint>

namespace gxos {
namespace javascript {
namespace {

bool aligned(const void* pointer, std::size_t alignment)
{
    return pointer == nullptr ||
        (reinterpret_cast<std::uintptr_t>(pointer) % alignment) == 0u;
}

bool validRange(const void* pointer, std::size_t count,
    std::size_t elementSize)
{
    if (count == 0u) return true;
    if (pointer == nullptr || elementSize == 0u ||
        count > static_cast<std::size_t>(-1) / elementSize) return false;
    const std::size_t byteCount = count * elementSize;
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(pointer);
    return begin <= static_cast<std::uintptr_t>(-1) - byteCount;
}

bool rangesOverlap(const void* left, std::size_t leftCount,
    std::size_t leftSize, const void* right, std::size_t rightCount,
    std::size_t rightSize)
{
    if (leftCount == 0u || rightCount == 0u) return false;
    const std::uintptr_t leftBegin = reinterpret_cast<std::uintptr_t>(left);
    const std::uintptr_t rightBegin = reinterpret_cast<std::uintptr_t>(right);
    const std::uintptr_t leftEnd = leftBegin + leftCount * leftSize;
    const std::uintptr_t rightEnd = rightBegin + rightCount * rightSize;
    return leftBegin < rightEnd && rightBegin < leftEnd;
}

bool validBytes(SourceView view)
{
    return view.data != nullptr || view.length == 0u;
}

bool equalBytes(SourceView left, SourceView right)
{
    if (left.length != right.length) return false;
    for (std::size_t index = 0u; index < left.length; ++index) {
        if (left.data[index] != right.data[index]) return false;
    }
    return true;
}

} // namespace

bool PortableObjectStore::configure(PortableObjectStorageView storage,
    PortableObjectLimits limits, RuntimeGeneration generation,
    PortableObjectError& error)
{
    error.code = PortableObjectErrorCode::None;
    configured_ = false;
    storage_ = PortableObjectStorageView();
    generation_ = kInvalidRuntimeGeneration;
    if (generation == kInvalidRuntimeGeneration ||
        limits.maxObjects > storage.objectCapacity ||
        limits.maxProperties > storage.propertyCapacity ||
        limits.maxTotalArrayElements > storage.arrayElementCapacity ||
        limits.maxPropertyNameBytes > storage.propertyNameCapacity ||
        limits.maxObjects >= kRuntimeHandleInvalidPart ||
        limits.maxProperties > 0xffffffffu ||
        limits.maxTotalArrayElements > 0xffffffffu ||
        limits.maxPropertyNameBytes > 0xffffffffu ||
        limits.maxPropertiesPerObject > 0xffffu ||
        limits.maxArrayElements > 0xffffu ||
        limits.maxArrayLength > 0xffffffffu ||
        limits.maxArrayElements > limits.maxTotalArrayElements ||
        limits.maxPropertyNameLength > limits.maxPropertyNameBytes ||
        (storage.objectCapacity != 0u && storage.objects == nullptr) ||
        (storage.propertyCapacity != 0u && storage.properties == nullptr) ||
        (storage.arrayElementCapacity != 0u &&
            storage.arrayElements == nullptr) ||
        (storage.propertyNameCapacity != 0u &&
            storage.propertyNameBytes == nullptr) ||
        !validRange(storage.objects, storage.objectCapacity,
            sizeof(PortableObjectRecord)) ||
        !validRange(storage.properties, storage.propertyCapacity,
            sizeof(PortablePropertyRecord)) ||
        !validRange(storage.arrayElements, storage.arrayElementCapacity,
            sizeof(PortableArrayElementRecord)) ||
        !validRange(storage.propertyNameBytes, storage.propertyNameCapacity,
            sizeof(char)) ||
        !aligned(storage.objects, alignof(PortableObjectRecord)) ||
        !aligned(storage.properties, alignof(PortablePropertyRecord)) ||
        !aligned(storage.arrayElements,
            alignof(PortableArrayElementRecord)) ||
        rangesOverlap(storage.objects, storage.objectCapacity,
            sizeof(PortableObjectRecord), storage.properties,
            storage.propertyCapacity, sizeof(PortablePropertyRecord)) ||
        rangesOverlap(storage.objects, storage.objectCapacity,
            sizeof(PortableObjectRecord), storage.arrayElements,
            storage.arrayElementCapacity,
            sizeof(PortableArrayElementRecord)) ||
        rangesOverlap(storage.objects, storage.objectCapacity,
            sizeof(PortableObjectRecord), storage.propertyNameBytes,
            storage.propertyNameCapacity, sizeof(char)) ||
        rangesOverlap(storage.properties, storage.propertyCapacity,
            sizeof(PortablePropertyRecord), storage.arrayElements,
            storage.arrayElementCapacity,
            sizeof(PortableArrayElementRecord)) ||
        rangesOverlap(storage.properties, storage.propertyCapacity,
            sizeof(PortablePropertyRecord), storage.propertyNameBytes,
            storage.propertyNameCapacity, sizeof(char)) ||
        rangesOverlap(storage.arrayElements, storage.arrayElementCapacity,
            sizeof(PortableArrayElementRecord), storage.propertyNameBytes,
            storage.propertyNameCapacity, sizeof(char))) {
        error.code = PortableObjectErrorCode::InvalidStorage;
        return false;
    }
    storage_ = storage;
    limits_ = limits;
    generation_ = generation;
    objectCount_ = 0u;
    propertyCount_ = 0u;
    propertyNameBytesUsed_ = 0u;
    arrayElementCount_ = 0u;
    freeProperty_ = kInvalidPortablePropertySlot;
    freeArrayElement_ = kInvalidPortableArrayElementSlot;
    for (std::size_t index = 0u; index < limits.maxObjects; ++index)
        storage.objects[index] = PortableObjectRecord();
    for (std::size_t index = 0u; index < limits.maxProperties; ++index)
        storage.properties[index] = PortablePropertyRecord();
    for (std::size_t index = 0u; index < limits.maxTotalArrayElements;
        ++index) {
        storage.arrayElements[index] = PortableArrayElementRecord();
    }
    configured_ = true;
    return true;
}

bool PortableObjectStore::reset(RuntimeGeneration generation,
    PortableObjectError& error)
{
    error.code = PortableObjectErrorCode::None;
    if (!configured_ || generation == kInvalidRuntimeGeneration ||
        generation <= generation_) {
        error.code = PortableObjectErrorCode::InvalidStorage;
        return false;
    }
    for (std::size_t index = 0u; index < objectCount_; ++index)
        storage_.objects[index] = PortableObjectRecord();
    for (std::size_t index = 0u; index < limits_.maxProperties; ++index)
        storage_.properties[index] = PortablePropertyRecord();
    for (std::size_t index = 0u;
        index < limits_.maxTotalArrayElements; ++index) {
        storage_.arrayElements[index] = PortableArrayElementRecord();
    }
    generation_ = generation;
    objectCount_ = 0u;
    propertyCount_ = 0u;
    propertyNameBytesUsed_ = 0u;
    arrayElementCount_ = 0u;
    freeProperty_ = kInvalidPortablePropertySlot;
    freeArrayElement_ = kInvalidPortableArrayElementSlot;
    return true;
}

bool PortableObjectStore::rollbackObjects(std::size_t retainedCount,
    PortableObjectError& error)
{
    error.code = PortableObjectErrorCode::None;
    if (!configured_ || retainedCount > objectCount_) {
        error.code = PortableObjectErrorCode::InvalidStorage;
        return false;
    }
    const auto referencesRemovedObject = [this, retainedCount](Value value) {
        return value.isObject() &&
            runtimeHandleIsValid(value.objectId(), generation_) &&
            runtimeHandleSlot(value.objectId()) >= retainedCount;
    };
    for (std::size_t index = 0u; index < retainedCount; ++index) {
        const PortableObjectRecord& object = storage_.objects[index];
        if (object.prototype != kInvalidRuntimeObjectId &&
            runtimeHandleIsValid(object.prototype, generation_) &&
            runtimeHandleSlot(object.prototype) >= retainedCount) {
            error.code = PortableObjectErrorCode::InvalidPrototype;
            return false;
        }
        std::uint32_t property = object.firstProperty;
        std::size_t inspected = 0u;
        while (property != kInvalidPortablePropertySlot) {
            if (property >= limits_.maxProperties ||
                !storage_.properties[property].active ||
                inspected++ >= limits_.maxProperties) {
                error.code =
                    PortableObjectErrorCode::InternalInvariantFailure;
                return false;
            }
            if (referencesRemovedObject(storage_.properties[property].value)) {
                error.code = PortableObjectErrorCode::InvalidProperty;
                return false;
            }
            property = storage_.properties[property].next;
        }
        std::uint32_t element = object.firstArrayElement;
        inspected = 0u;
        while (element != kInvalidPortableArrayElementSlot) {
            if (element >= limits_.maxTotalArrayElements ||
                !storage_.arrayElements[element].active ||
                inspected++ >= limits_.maxTotalArrayElements) {
                error.code =
                    PortableObjectErrorCode::InternalInvariantFailure;
                return false;
            }
            if (referencesRemovedObject(storage_.arrayElements[element].value)) {
                error.code = PortableObjectErrorCode::InvalidProperty;
                return false;
            }
            element = storage_.arrayElements[element].next;
        }
    }
    for (std::size_t index = retainedCount; index < objectCount_; ++index) {
        PortableObjectRecord& object = storage_.objects[index];
        while (object.firstProperty != kInvalidPortablePropertySlot) {
            const std::uint32_t property = object.firstProperty;
            if (property >= limits_.maxProperties ||
                !storage_.properties[property].active ||
                object.propertyCount == 0u) {
                error.code =
                    PortableObjectErrorCode::InternalInvariantFailure;
                return false;
            }
            object.firstProperty = storage_.properties[property].next;
            --object.propertyCount;
            if (object.firstProperty == kInvalidPortablePropertySlot)
                object.lastProperty = kInvalidPortablePropertySlot;
            if (!releaseProperty(property, error)) return false;
        }
        while (object.firstArrayElement !=
            kInvalidPortableArrayElementSlot) {
            const std::uint32_t element = object.firstArrayElement;
            if (element >= limits_.maxTotalArrayElements ||
                !storage_.arrayElements[element].active ||
                object.arrayElementCount == 0u) {
                error.code =
                    PortableObjectErrorCode::InternalInvariantFailure;
                return false;
            }
            object.firstArrayElement =
                storage_.arrayElements[element].next;
            --object.arrayElementCount;
            if (!releaseArrayElement(element, error)) return false;
        }
        object = PortableObjectRecord();
    }
    objectCount_ = retainedCount;
    return true;
}

bool PortableObjectStore::objectRecord(RuntimeObjectId object,
    PortableObjectRecord*& record, PortableObjectError& error) const
{
    record = nullptr;
    if (!configured_) {
        error.code = PortableObjectErrorCode::InvalidStorage;
        return false;
    }
    if (!runtimeHandleIsValid(object, generation_)) {
        error.code = runtimeHandleGeneration(object) == generation_
            ? PortableObjectErrorCode::InvalidObject
            : PortableObjectErrorCode::StaleGeneration;
        return false;
    }
    const std::uint32_t slot = runtimeHandleSlot(object);
    if (slot >= objectCount_ || !storage_.objects[slot].active) {
        error.code = PortableObjectErrorCode::InvalidObject;
        return false;
    }
    record = &storage_.objects[slot];
    return true;
}

bool PortableObjectStore::validObject(RuntimeObjectId object,
    PortableObjectError& error) const
{
    PortableObjectRecord* record = nullptr;
    error.code = PortableObjectErrorCode::None;
    return objectRecord(object, record, error);
}

bool PortableObjectStore::createObject(RuntimeObjectId prototype,
    RuntimeObjectId& result, PortableObjectError& error)
{
    error.code = PortableObjectErrorCode::None;
    result = kInvalidRuntimeObjectId;
    if (!configured_) {
        error.code = PortableObjectErrorCode::InvalidStorage;
        return false;
    }
    if (prototype != kInvalidRuntimeObjectId) {
        RuntimeObjectId current = prototype;
        bool reachedRoot = false;
        for (std::size_t depth = 0u; depth < limits_.maxPrototypeDepth;
            ++depth) {
            PortableObjectRecord* parent = nullptr;
            if (!objectRecord(current, parent, error)) {
                error.code = PortableObjectErrorCode::InvalidPrototype;
                return false;
            }
            if (parent->prototype == kInvalidRuntimeObjectId) {
                reachedRoot = true;
                break;
            }
            current = parent->prototype;
        }
        if (!reachedRoot) {
            error.code = PortableObjectErrorCode::PrototypeDepthExceeded;
            return false;
        }
    }
    if (objectCount_ >= limits_.maxObjects ||
        objectCount_ >= kRuntimeHandleInvalidPart) {
        error.code = PortableObjectErrorCode::ObjectCapacityExceeded;
        return false;
    }
    const std::size_t slot = objectCount_;
    PortableObjectRecord record;
    record.prototype = prototype;
    record.active = true;
    storage_.objects[slot] = record;
    ++objectCount_;
    result = makeRuntimeHandle(generation_,
        static_cast<std::uint32_t>(slot));
    return true;
}

bool PortableObjectStore::createArray(std::size_t length,
    RuntimeObjectId prototype, RuntimeObjectId& result,
    PortableObjectError& error)
{
    error.code = PortableObjectErrorCode::None;
    result = kInvalidRuntimeObjectId;
    if (length > limits_.maxArrayLength) {
        error.code = PortableObjectErrorCode::ArrayCapacityExceeded;
        return false;
    }
    if (!createObject(prototype, result, error)) return false;
    PortableObjectRecord* record = nullptr;
    if (!objectRecord(result, record, error)) {
        --objectCount_;
        result = kInvalidRuntimeObjectId;
        error.code = PortableObjectErrorCode::InternalInvariantFailure;
        return false;
    }
    record->array = true;
    record->arrayLength = static_cast<std::uint32_t>(length);
    return true;
}

bool PortableObjectStore::isArray(RuntimeObjectId object, bool& result,
    PortableObjectError& error) const
{
    error.code = PortableObjectErrorCode::None;
    PortableObjectRecord* record = nullptr;
    if (!objectRecord(object, record, error)) return false;
    result = record->array;
    return true;
}

bool PortableObjectStore::arrayLength(RuntimeObjectId object,
    std::size_t& result, PortableObjectError& error) const
{
    error.code = PortableObjectErrorCode::None;
    PortableObjectRecord* record = nullptr;
    if (!objectRecord(object, record, error)) return false;
    if (!record->array) {
        error.code = PortableObjectErrorCode::InvalidArray;
        return false;
    }
    result = record->arrayLength;
    return true;
}

bool PortableObjectStore::prototypeOf(RuntimeObjectId object,
    RuntimeObjectId& result, PortableObjectError& error) const
{
    error.code = PortableObjectErrorCode::None;
    PortableObjectRecord* record = nullptr;
    if (!objectRecord(object, record, error)) return false;
    result = record->prototype;
    return true;
}

bool PortableObjectStore::setPrototype(RuntimeObjectId object,
    RuntimeObjectId prototype, PortableObjectError& error)
{
    error.code = PortableObjectErrorCode::None;
    PortableObjectRecord* record = nullptr;
    if (!objectRecord(object, record, error)) return false;
    if (prototype == kInvalidRuntimeObjectId) {
        record->prototype = prototype;
        return true;
    }
    RuntimeObjectId current = prototype;
    for (std::size_t depth = 0u; depth < limits_.maxPrototypeDepth; ++depth) {
        if (current == object) {
            error.code = PortableObjectErrorCode::PrototypeCycle;
            return false;
        }
        PortableObjectRecord* parent = nullptr;
        if (!objectRecord(current, parent, error)) {
            error.code = PortableObjectErrorCode::InvalidPrototype;
            return false;
        }
        if (parent->prototype == kInvalidRuntimeObjectId) {
            record->prototype = prototype;
            return true;
        }
        current = parent->prototype;
    }
    error.code = PortableObjectErrorCode::PrototypeDepthExceeded;
    return false;
}

bool PortableObjectStore::propertyKeyMatches(
    const PortablePropertyRecord& property, SourceView key) const
{
    return propertyKeyMatches(property, key, WellKnownSymbol::None);
}

bool PortableObjectStore::propertyKeyMatches(
    const PortablePropertyRecord& property, SourceView key,
    WellKnownSymbol symbol) const
{
    if (!property.active || property.symbol != symbol) return false;
    if (symbol != WellKnownSymbol::None) return true;
    if (property.keyLength != key.length) return false;
    const char* storedKey = storage_.propertyNameBytes == nullptr
        ? nullptr : storage_.propertyNameBytes + property.keyOffset;
    return equalBytes(SourceView(storedKey, property.keyLength), key);
}

bool PortableObjectStore::findProperty(const PortableObjectRecord& object,
    SourceView key, std::uint32_t& slot) const
{
    return findProperty(object, key, WellKnownSymbol::None, slot);
}

bool PortableObjectStore::findProperty(const PortableObjectRecord& object,
    SourceView key, WellKnownSymbol symbol, std::uint32_t& slot) const
{
    slot = object.firstProperty;
    std::size_t inspected = 0u;
    while (slot != kInvalidPortablePropertySlot) {
        if (slot >= limits_.maxProperties ||
            !storage_.properties[slot].active ||
            inspected >= limits_.maxProperties) {
            slot = kInvalidPortablePropertySlot;
            return false;
        }
        if (propertyKeyMatches(storage_.properties[slot], key, symbol))
            return true;
        slot = storage_.properties[slot].next;
        ++inspected;
    }
    return false;
}

bool PortableObjectStore::parseArrayIndex(SourceView key,
    std::uint32_t& index) const
{
    if (!validBytes(key) || key.length == 0u ||
        (key.length > 1u && key.data[0] == '0')) return false;
    std::uint64_t value = 0u;
    for (std::size_t cursor = 0u; cursor < key.length; ++cursor) {
        const char character = key.data[cursor];
        if (character < '0' || character > '9') return false;
        const std::uint64_t digit =
            static_cast<unsigned>(character - '0');
        if (value > (4294967294ull - digit) / 10u) return false;
        value = value * 10u + digit;
    }
    index = static_cast<std::uint32_t>(value);
    return true;
}

bool PortableObjectStore::findArrayElement(
    const PortableObjectRecord& object, std::size_t index,
    std::uint32_t& slot, std::uint32_t& previous) const
{
    previous = kInvalidPortableArrayElementSlot;
    slot = object.firstArrayElement;
    std::size_t inspected = 0u;
    while (slot != kInvalidPortableArrayElementSlot) {
        if (slot >= limits_.maxTotalArrayElements ||
            !storage_.arrayElements[slot].active ||
            inspected >= limits_.maxTotalArrayElements) {
            slot = kInvalidPortableArrayElementSlot;
            previous = kInvalidPortableArrayElementSlot;
            return false;
        }
        if (storage_.arrayElements[slot].index >= index) return true;
        previous = slot;
        slot = storage_.arrayElements[slot].next;
        ++inspected;
    }
    return true;
}

bool PortableObjectStore::hasOwnProperty(RuntimeObjectId object,
    SourceView key, bool& found, PortableObjectError& error) const
{
    Value value;
    return getOwnProperty(object, key, value, found, error);
}

bool PortableObjectStore::hasOwnProperty(RuntimeObjectId object,
    WellKnownSymbol symbol, bool& found, PortableObjectError& error) const
{
    Value value;
    return getOwnProperty(object, symbol, value, found, error);
}

bool PortableObjectStore::getOwnProperty(RuntimeObjectId object,
    SourceView key, Value& value, bool& found,
    PortableObjectError& error) const
{
    PortablePropertyEntry entry;
    if (!getOwnPropertyEntry(object, key, entry, found, error)) {
        value = Value::undefined();
        return false;
    }
    value = found ? entry.value : Value::undefined();
    return true;
}

bool PortableObjectStore::getOwnProperty(RuntimeObjectId object,
    WellKnownSymbol symbol, Value& value, bool& found,
    PortableObjectError& error) const
{
    PortablePropertyEntry entry;
    if (!getOwnPropertyEntry(object, symbol, entry, found, error)) {
        value = Value::undefined();
        return false;
    }
    value = found ? entry.value : Value::undefined();
    return true;
}

bool PortableObjectStore::getOwnPropertyEntry(RuntimeObjectId object,
    SourceView key, PortablePropertyEntry& result, bool& found,
    PortableObjectError& error) const
{
    return getOwnPropertyEntry(object, key, WellKnownSymbol::None, result,
        found, error);
}

bool PortableObjectStore::getOwnPropertyEntry(RuntimeObjectId object,
    WellKnownSymbol symbol, PortablePropertyEntry& result, bool& found,
    PortableObjectError& error) const
{
    return getOwnPropertyEntry(object, SourceView(), symbol, result, found,
        error);
}

bool PortableObjectStore::getOwnPropertyEntry(RuntimeObjectId object,
    SourceView key, WellKnownSymbol symbol, PortablePropertyEntry& result,
    bool& found, PortableObjectError& error) const
{
    error.code = PortableObjectErrorCode::None;
    found = false;
    result = PortablePropertyEntry();
    if (!validBytes(key) ||
        (symbol != WellKnownSymbol::None && key.length != 0u)) {
        error.code = PortableObjectErrorCode::InvalidProperty;
        return false;
    }
    PortableObjectRecord* record = nullptr;
    if (!objectRecord(object, record, error)) return false;
    if (symbol == WellKnownSymbol::None && record->array &&
        key.length == 6u && key.data != nullptr &&
        key.data[0] == 'l' && key.data[1] == 'e' && key.data[2] == 'n' &&
        key.data[3] == 'g' && key.data[4] == 't' && key.data[5] == 'h') {
        result.key = SourceView("length", 6u);
        result.value = Value::number(static_cast<double>(record->arrayLength));
        result.attributes = 0x01u;
        result.isArrayLength = true;
        found = true;
        return true;
    }
    std::uint32_t index = 0u;
    if (symbol == WellKnownSymbol::None && record->array &&
        parseArrayIndex(key, index)) {
        if (index > limits_.maxArrayIndex) {
            error.code = PortableObjectErrorCode::InvalidIndex;
            return false;
        }
        std::uint32_t slot = kInvalidPortableArrayElementSlot;
        std::uint32_t previous = kInvalidPortableArrayElementSlot;
        if (!findArrayElement(*record, index, slot, previous)) {
            error.code = PortableObjectErrorCode::InternalInvariantFailure;
            return false;
        }
        if (slot != kInvalidPortableArrayElementSlot &&
            storage_.arrayElements[slot].index == index) {
            const PortableArrayElementRecord& element =
                storage_.arrayElements[slot];
            result.key = key;
            result.value = element.value;
            result.attributes = element.attributes;
            result.arrayIndex = index;
            result.isArrayElement = true;
            found = true;
            return true;
        }
    }
    std::uint32_t property = kInvalidPortablePropertySlot;
    if (findProperty(*record, key, symbol, property)) {
        const PortablePropertyRecord& current = storage_.properties[property];
        const char* keyBytes = storage_.propertyNameBytes == nullptr
            ? nullptr : storage_.propertyNameBytes + current.keyOffset;
        result.key = SourceView(keyBytes, current.keyLength);
        result.value = current.value;
        result.symbol = current.symbol;
        result.attributes = current.attributes;
        found = true;
    }
    return true;
}

bool PortableObjectStore::getProperty(RuntimeObjectId object,
    SourceView key, Value& value, bool& found,
    PortableObjectError& error) const
{
    error.code = PortableObjectErrorCode::None;
    value = Value::undefined();
    found = false;
    RuntimeObjectId current = object;
    for (std::size_t depth = 0u; depth < limits_.maxPrototypeDepth; ++depth) {
        PortableObjectRecord* record = nullptr;
        if (!objectRecord(current, record, error)) return false;
        bool own = false;
        if (!getOwnProperty(current, key, value, own, error)) return false;
        if (own) {
            found = true;
            return true;
        }
        if (record->prototype == kInvalidRuntimeObjectId) return true;
        current = record->prototype;
    }
    error.code = PortableObjectErrorCode::PrototypeDepthExceeded;
    return false;
}

bool PortableObjectStore::getProperty(RuntimeObjectId object,
    WellKnownSymbol symbol, Value& value, bool& found,
    PortableObjectError& error) const
{
    error.code = PortableObjectErrorCode::None;
    value = Value::undefined();
    found = false;
    RuntimeObjectId current = object;
    for (std::size_t depth = 0u; depth < limits_.maxPrototypeDepth; ++depth) {
        PortableObjectRecord* record = nullptr;
        if (!objectRecord(current, record, error)) return false;
        bool own = false;
        if (!getOwnProperty(current, symbol, value, own, error)) return false;
        if (own) {
            found = true;
            return true;
        }
        if (record->prototype == kInvalidRuntimeObjectId) return true;
        current = record->prototype;
    }
    error.code = PortableObjectErrorCode::PrototypeDepthExceeded;
    return false;
}

bool PortableObjectStore::allocateProperty(std::uint32_t& slot,
    PortableObjectError& error)
{
    if (freeProperty_ != kInvalidPortablePropertySlot) {
        slot = freeProperty_;
        freeProperty_ = storage_.properties[slot].next;
        storage_.properties[slot] = PortablePropertyRecord();
        return true;
    }
    for (std::size_t index = 0u; index < limits_.maxProperties; ++index) {
        if (!storage_.properties[index].active &&
            storage_.properties[index].next ==
                kInvalidPortablePropertySlot) {
            slot = static_cast<std::uint32_t>(index);
            storage_.properties[index] = PortablePropertyRecord();
            return true;
        }
    }
    error.code = PortableObjectErrorCode::PropertyCapacityExceeded;
    return false;
}

bool PortableObjectStore::allocateArrayElement(std::uint32_t& slot,
    PortableObjectError& error)
{
    if (freeArrayElement_ != kInvalidPortableArrayElementSlot) {
        slot = freeArrayElement_;
        freeArrayElement_ = storage_.arrayElements[slot].next;
        storage_.arrayElements[slot] = PortableArrayElementRecord();
        return true;
    }
    for (std::size_t index = 0u;
        index < limits_.maxTotalArrayElements; ++index) {
        if (!storage_.arrayElements[index].active &&
            storage_.arrayElements[index].next ==
                kInvalidPortableArrayElementSlot) {
            slot = static_cast<std::uint32_t>(index);
            storage_.arrayElements[index] = PortableArrayElementRecord();
            return true;
        }
    }
    error.code = PortableObjectErrorCode::ArrayCapacityExceeded;
    return false;
}

bool PortableObjectStore::setArrayLengthRecord(PortableObjectRecord& object,
    std::size_t length, PortableObjectError& error)
{
    if (!object.array) {
        error.code = PortableObjectErrorCode::InvalidArray;
        return false;
    }
    if (length > limits_.maxArrayLength) {
        error.code = PortableObjectErrorCode::ArrayCapacityExceeded;
        return false;
    }
    if (length < object.arrayLength) {
        std::uint32_t previous = kInvalidPortableArrayElementSlot;
        std::uint32_t current = object.firstArrayElement;
        while (current != kInvalidPortableArrayElementSlot &&
            storage_.arrayElements[current].index < length) {
            previous = current;
            current = storage_.arrayElements[current].next;
        }
        if (current != kInvalidPortableArrayElementSlot) {
            if (previous == kInvalidPortableArrayElementSlot)
                object.firstArrayElement =
                    kInvalidPortableArrayElementSlot;
            else storage_.arrayElements[previous].next =
                kInvalidPortableArrayElementSlot;
            while (current != kInvalidPortableArrayElementSlot) {
                const std::uint32_t next =
                    storage_.arrayElements[current].next;
                if (!releaseArrayElement(current, error)) return false;
                if (object.arrayElementCount == 0u) {
                    error.code =
                        PortableObjectErrorCode::InternalInvariantFailure;
                    return false;
                }
                --object.arrayElementCount;
                current = next;
            }
        }
    }
    object.arrayLength = static_cast<std::uint32_t>(length);
    return true;
}

bool PortableObjectStore::setOwnProperty(RuntimeObjectId object,
    SourceView key, Value value, PortableObjectError& error,
    std::uint8_t attributes)
{
    return setOwnPropertyImpl(object, key, WellKnownSymbol::None, value,
        error, attributes);
}

bool PortableObjectStore::setOwnProperty(RuntimeObjectId object,
    WellKnownSymbol symbol, Value value, PortableObjectError& error,
    std::uint8_t attributes)
{
    return setOwnPropertyImpl(object, SourceView(), symbol, value, error,
        attributes);
}

bool PortableObjectStore::setOwnPropertyImpl(RuntimeObjectId object,
    SourceView key, WellKnownSymbol symbol, Value value,
    PortableObjectError& error, std::uint8_t attributes)
{
    error.code = PortableObjectErrorCode::None;
    if (!configured_ || !validBytes(key) || attributes > 0x3fu ||
        (symbol != WellKnownSymbol::None && key.length != 0u)) {
        error.code = PortableObjectErrorCode::InvalidProperty;
        return false;
    }
    if (symbol == WellKnownSymbol::None &&
        key.length > limits_.maxPropertyNameLength) {
        error.code = PortableObjectErrorCode::NameTooLong;
        return false;
    }
    PortableObjectRecord* record = nullptr;
    if (!objectRecord(object, record, error)) return false;
    if (symbol == WellKnownSymbol::None && record->array &&
        key.length == 6u && key.data != nullptr &&
        key.data[0] == 'l' && key.data[1] == 'e' && key.data[2] == 'n' &&
        key.data[3] == 'g' && key.data[4] == 't' && key.data[5] == 'h') {
        if ((attributes != 0x01u && attributes != 0x07u &&
                attributes != 0x39u && attributes != 0x3fu) ||
            !value.isNumber()) {
            error.code = PortableObjectErrorCode::InvalidLength;
            return false;
        }
        const double requested = value.numberValue();
        if (!(requested >= 0.0) || requested >
                static_cast<double>(limits_.maxArrayLength) ||
            static_cast<double>(static_cast<std::size_t>(requested)) !=
                requested) {
            error.code = PortableObjectErrorCode::InvalidLength;
            return false;
        }
        return setArrayLengthRecord(*record,
            static_cast<std::size_t>(requested), error);
    }
    std::uint32_t index = 0u;
    if (symbol == WellKnownSymbol::None && record->array &&
        parseArrayIndex(key, index)) {
        if (index > limits_.maxArrayIndex) {
            error.code = PortableObjectErrorCode::InvalidIndex;
            return false;
        }
        if (attributes != 0x07u && attributes != 0x3fu) {
            error.code = PortableObjectErrorCode::InvalidProperty;
            return false;
        }
        return setArrayElement(object, index, value, error, attributes);
    }
    std::uint32_t property = kInvalidPortablePropertySlot;
    if (findProperty(*record, key, symbol, property)) {
        PortablePropertyRecord& current = storage_.properties[property];
        if ((current.attributes & 0x01u) != 0u) current.value = value;
        return true;
    }
    if (record->propertyCount >= limits_.maxPropertiesPerObject ||
        propertyCount_ >= limits_.maxProperties) {
        error.code = PortableObjectErrorCode::PropertyCapacityExceeded;
        return false;
    }
    const std::size_t nameLength = symbol == WellKnownSymbol::None
        ? key.length : 0u;
    if (nameLength > limits_.maxPropertyNameBytes ||
        propertyNameBytesUsed_ > limits_.maxPropertyNameBytes - nameLength ||
        nameLength > storage_.propertyNameCapacity ||
        propertyNameBytesUsed_ > storage_.propertyNameCapacity - nameLength) {
        error.code = PortableObjectErrorCode::NameStorageExhausted;
        return false;
    }
    std::uint32_t newProperty = kInvalidPortablePropertySlot;
    if (!allocateProperty(newProperty, error)) return false;
    PortablePropertyRecord& inserted = storage_.properties[newProperty];
    inserted.keyOffset = static_cast<std::uint32_t>(propertyNameBytesUsed_);
    inserted.keyLength = static_cast<std::uint32_t>(nameLength);
    inserted.symbol = symbol;
    if (nameLength != 0u) {
        for (std::size_t cursor = 0u; cursor < nameLength; ++cursor)
            storage_.propertyNameBytes[propertyNameBytesUsed_ + cursor] =
                key.data[cursor];
    }
    inserted.value = value;
    inserted.attributes = attributes;
    inserted.active = true;
    inserted.next = kInvalidPortablePropertySlot;
    if (record->lastProperty == kInvalidPortablePropertySlot)
        record->firstProperty = newProperty;
    else storage_.properties[record->lastProperty].next = newProperty;
    record->lastProperty = newProperty;
    ++record->propertyCount;
    ++propertyCount_;
    propertyNameBytesUsed_ += nameLength;
    return true;
}

bool PortableObjectStore::updateOwnProperty(RuntimeObjectId object,
    SourceView key, Value value, std::uint8_t attributes,
    PortableObjectError& error)
{
    return updateOwnPropertyImpl(object, key, WellKnownSymbol::None, value,
        attributes, error);
}

bool PortableObjectStore::updateOwnProperty(RuntimeObjectId object,
    WellKnownSymbol symbol, Value value, std::uint8_t attributes,
    PortableObjectError& error)
{
    return updateOwnPropertyImpl(object, SourceView(), symbol, value,
        attributes, error);
}

bool PortableObjectStore::updateOwnPropertyImpl(RuntimeObjectId object,
    SourceView key, WellKnownSymbol symbol, Value value,
    std::uint8_t attributes, PortableObjectError& error)
{
    error.code = PortableObjectErrorCode::None;
    if (!validBytes(key) || attributes > 0x3fu ||
        (symbol != WellKnownSymbol::None && key.length != 0u)) {
        error.code = PortableObjectErrorCode::InvalidProperty;
        return false;
    }
    PortableObjectRecord* record = nullptr;
    if (!objectRecord(object, record, error)) return false;
    if (symbol == WellKnownSymbol::None && record->array &&
        key.length == 6u && key.data != nullptr &&
        key.data[0] == 'l' && key.data[1] == 'e' && key.data[2] == 'n' &&
        key.data[3] == 'g' && key.data[4] == 't' && key.data[5] == 'h') {
        if (!value.isNumber()) {
            error.code = PortableObjectErrorCode::InvalidLength;
            return false;
        }
        const double requested = value.numberValue();
        if (!(requested >= 0.0) || requested >
                static_cast<double>(limits_.maxArrayLength) ||
            static_cast<double>(static_cast<std::size_t>(requested)) !=
                requested) {
            error.code = PortableObjectErrorCode::InvalidLength;
            return false;
        }
        return setArrayLengthRecord(*record,
            static_cast<std::size_t>(requested), error);
    }
    std::uint32_t index = 0u;
    if (symbol == WellKnownSymbol::None && record->array &&
        parseArrayIndex(key, index)) {
        if (index > limits_.maxArrayIndex) {
            error.code = PortableObjectErrorCode::InvalidIndex;
            return false;
        }
        std::uint32_t slot = kInvalidPortableArrayElementSlot;
        std::uint32_t previous = kInvalidPortableArrayElementSlot;
        if (!findArrayElement(*record, index, slot, previous)) {
            error.code = PortableObjectErrorCode::InternalInvariantFailure;
            return false;
        }
        if (slot == kInvalidPortableArrayElementSlot ||
            storage_.arrayElements[slot].index != index) {
            error.code = PortableObjectErrorCode::InvalidProperty;
            return false;
        }
        PortableArrayElementRecord& element = storage_.arrayElements[slot];
        element.value = value;
        element.attributes = attributes;
        return true;
    }
    std::uint32_t slot = kInvalidPortablePropertySlot;
    if (!findProperty(*record, key, symbol, slot)) {
        error.code = PortableObjectErrorCode::InvalidProperty;
        return false;
    }
    PortablePropertyRecord& property = storage_.properties[slot];
    property.value = value;
    property.attributes = attributes;
    return true;
}

bool PortableObjectStore::compactPropertyNames(std::uint32_t removedOffset,
    std::uint32_t removedLength, PortableObjectError& error)
{
    if (removedOffset > propertyNameBytesUsed_ ||
        removedLength > propertyNameBytesUsed_ - removedOffset) {
        error.code = PortableObjectErrorCode::InternalInvariantFailure;
        return false;
    }
    const std::size_t tailOffset =
        static_cast<std::size_t>(removedOffset) + removedLength;
    const std::size_t tailLength = propertyNameBytesUsed_ - tailOffset;
    for (std::size_t index = 0u; index < limits_.maxProperties; ++index) {
        const PortablePropertyRecord& property = storage_.properties[index];
        if (!property.active) continue;
        if (property.keyOffset > propertyNameBytesUsed_ ||
            property.keyLength > propertyNameBytesUsed_ - property.keyOffset ||
            (property.keyOffset > removedOffset &&
                property.keyOffset < tailOffset)) {
            error.code = PortableObjectErrorCode::InternalInvariantFailure;
            return false;
        }
    }
    for (std::size_t index = 0u; index < tailLength; ++index)
        storage_.propertyNameBytes[removedOffset + index] =
            storage_.propertyNameBytes[tailOffset + index];
    for (std::size_t index = 0u; index < limits_.maxProperties; ++index) {
        PortablePropertyRecord& property = storage_.properties[index];
        if (property.active && property.keyOffset >= tailOffset)
            property.keyOffset -= removedLength;
    }
    propertyNameBytesUsed_ -= removedLength;
    return true;
}

bool PortableObjectStore::releaseProperty(std::uint32_t slot,
    PortableObjectError& error)
{
    error.code = PortableObjectErrorCode::None;
    if (slot >= limits_.maxProperties ||
        !storage_.properties[slot].active || propertyCount_ == 0u) {
        error.code = PortableObjectErrorCode::InternalInvariantFailure;
        return false;
    }
    PortablePropertyRecord& property = storage_.properties[slot];
    const std::uint32_t offset = property.keyOffset;
    const std::uint32_t length = property.keyLength;
    property.active = false;
    if (!compactPropertyNames(offset, length, error)) return false;
    property = PortablePropertyRecord();
    property.next = freeProperty_;
    freeProperty_ = slot;
    --propertyCount_;
    return true;
}

bool PortableObjectStore::deleteOwnProperty(RuntimeObjectId object,
    SourceView key, bool& deleted, PortableObjectError& error)
{
    return deleteOwnPropertyImpl(object, key, WellKnownSymbol::None, deleted,
        error);
}

bool PortableObjectStore::deleteOwnProperty(RuntimeObjectId object,
    WellKnownSymbol symbol, bool& deleted, PortableObjectError& error)
{
    return deleteOwnPropertyImpl(object, SourceView(), symbol, deleted,
        error);
}

bool PortableObjectStore::deleteOwnPropertyImpl(RuntimeObjectId object,
    SourceView key, WellKnownSymbol symbol, bool& deleted,
    PortableObjectError& error)
{
    error.code = PortableObjectErrorCode::None;
    deleted = false;
    if (!validBytes(key) ||
        (symbol != WellKnownSymbol::None && key.length != 0u)) {
        error.code = PortableObjectErrorCode::InvalidProperty;
        return false;
    }
    PortableObjectRecord* record = nullptr;
    if (!objectRecord(object, record, error)) return false;
    if (symbol == WellKnownSymbol::None && record->array &&
        key.length == 6u && key.data != nullptr &&
        key.data[0] == 'l' && key.data[1] == 'e' && key.data[2] == 'n' &&
        key.data[3] == 'g' && key.data[4] == 't' && key.data[5] == 'h')
        return true;
    std::uint32_t index = 0u;
    if (symbol == WellKnownSymbol::None && record->array &&
        parseArrayIndex(key, index)) {
        if (index > limits_.maxArrayIndex) return true;
        return deleteArrayElement(object, index, deleted, error);
    }
    std::uint32_t previous = kInvalidPortablePropertySlot;
    std::uint32_t property = record->firstProperty;
    std::size_t inspected = 0u;
    while (property != kInvalidPortablePropertySlot) {
        if (property >= limits_.maxProperties ||
            !storage_.properties[property].active ||
            inspected >= limits_.maxProperties) {
            error.code = PortableObjectErrorCode::InternalInvariantFailure;
            return false;
        }
        PortablePropertyRecord& current = storage_.properties[property];
        if (propertyKeyMatches(current, key, symbol)) {
            if ((current.attributes & 0x04u) == 0u) return true;
            if (record->propertyCount == 0u || propertyCount_ == 0u) {
                error.code = PortableObjectErrorCode::InternalInvariantFailure;
                return false;
            }
            const std::uint32_t next = current.next;
            if (previous == kInvalidPortablePropertySlot)
                record->firstProperty = next;
            else storage_.properties[previous].next = next;
            if (record->lastProperty == property)
                record->lastProperty = previous;
            --record->propertyCount;
            if (!releaseProperty(property, error)) return false;
            deleted = true;
            return true;
        }
        previous = property;
        property = current.next;
        ++inspected;
    }
    return true;
}

bool PortableObjectStore::getOwnPropertyAt(RuntimeObjectId object,
    std::size_t ordinal, PortablePropertyEntry& result, bool& found,
    PortableObjectError& error) const
{
    error.code = PortableObjectErrorCode::None;
    found = false;
    result = PortablePropertyEntry();
    PortableObjectRecord* record = nullptr;
    if (!objectRecord(object, record, error)) return false;
    if (record->array) {
        if (ordinal < record->arrayElementCount) {
            PortableArrayEntry element;
            if (!getArrayElementAt(object, ordinal, element, found, error))
                return false;
            if (!found) {
                error.code = PortableObjectErrorCode::InternalInvariantFailure;
                return false;
            }
            result.value = element.value;
            result.attributes = element.attributes;
            result.arrayIndex = element.index;
            result.isArrayElement = true;
            return true;
        }
        if (ordinal == record->arrayElementCount) {
            result.key = SourceView("length", 6u);
            result.value = Value::number(
                static_cast<double>(record->arrayLength));
            result.attributes = 0x01u;
            result.isArrayLength = true;
            found = true;
            return true;
        }
        ordinal -= static_cast<std::size_t>(record->arrayElementCount) + 1u;
        std::uint32_t property = record->firstProperty;
        std::size_t index = 0u;
        while (property != kInvalidPortablePropertySlot) {
            if (property >= limits_.maxProperties ||
                !storage_.properties[property].active ||
                index >= limits_.maxProperties) {
                error.code = PortableObjectErrorCode::InternalInvariantFailure;
                return false;
            }
            const PortablePropertyRecord& current =
                storage_.properties[property];
            const char* keyBytes = storage_.propertyNameBytes == nullptr
                ? nullptr : storage_.propertyNameBytes + current.keyOffset;
            const SourceView key(keyBytes, current.keyLength);
            std::uint32_t numericIndex = 0u;
            if (!parseArrayIndex(key, numericIndex)) {
                if (index == ordinal) {
                    result.key = key;
                    result.value = current.value;
                    result.symbol = current.symbol;
                    result.attributes = current.attributes;
                    found = true;
                    return true;
                }
                ++index;
            }
            property = current.next;
        }
        return true;
    }

    // Ordinary object integer-index keys enumerate in ascending numeric
    // order. Other string keys retain their insertion order.
    std::size_t integerKeyCount = 0u;
    std::uint32_t property = record->firstProperty;
    std::size_t inspected = 0u;
    while (property != kInvalidPortablePropertySlot) {
        if (property >= limits_.maxProperties ||
            !storage_.properties[property].active ||
            inspected >= limits_.maxProperties) {
            error.code = PortableObjectErrorCode::InternalInvariantFailure;
            return false;
        }
        const PortablePropertyRecord& current = storage_.properties[property];
        const char* keyBytes = storage_.propertyNameBytes == nullptr
            ? nullptr : storage_.propertyNameBytes + current.keyOffset;
        std::uint32_t numericIndex = 0u;
        if (parseArrayIndex(SourceView(keyBytes, current.keyLength),
                numericIndex)) ++integerKeyCount;
        property = current.next;
        ++inspected;
    }
    if (ordinal < integerKeyCount) {
        bool havePrevious = false;
        std::uint32_t previousIndex = 0u;
        std::uint32_t selected = kInvalidPortablePropertySlot;
        for (std::size_t rank = 0u; rank <= ordinal; ++rank) {
            bool haveSelected = false;
            std::uint32_t selectedIndex = 0u;
            property = record->firstProperty;
            inspected = 0u;
            selected = kInvalidPortablePropertySlot;
            while (property != kInvalidPortablePropertySlot) {
                if (property >= limits_.maxProperties ||
                    !storage_.properties[property].active ||
                    inspected >= limits_.maxProperties) {
                    error.code =
                        PortableObjectErrorCode::InternalInvariantFailure;
                    return false;
                }
                const PortablePropertyRecord& current =
                    storage_.properties[property];
                const char* keyBytes = storage_.propertyNameBytes == nullptr
                    ? nullptr : storage_.propertyNameBytes + current.keyOffset;
                std::uint32_t numericIndex = 0u;
                if (parseArrayIndex(SourceView(keyBytes, current.keyLength),
                        numericIndex) &&
                    (!havePrevious || numericIndex > previousIndex) &&
                    (!haveSelected || numericIndex < selectedIndex)) {
                    selected = property;
                    selectedIndex = numericIndex;
                    haveSelected = true;
                }
                property = current.next;
                ++inspected;
            }
            if (!haveSelected) {
                error.code = PortableObjectErrorCode::InternalInvariantFailure;
                return false;
            }
            previousIndex = selectedIndex;
            havePrevious = true;
        }
        const PortablePropertyRecord& current = storage_.properties[selected];
        const char* keyBytes = storage_.propertyNameBytes == nullptr
            ? nullptr : storage_.propertyNameBytes + current.keyOffset;
        result.key = SourceView(keyBytes, current.keyLength);
        result.value = current.value;
        result.symbol = current.symbol;
        result.attributes = current.attributes;
        found = true;
        return true;
    }
    ordinal -= integerKeyCount;
    property = record->firstProperty;
    inspected = 0u;
    std::size_t index = 0u;
    while (property != kInvalidPortablePropertySlot) {
        if (property >= limits_.maxProperties ||
            !storage_.properties[property].active ||
            inspected >= limits_.maxProperties) {
            error.code = PortableObjectErrorCode::InternalInvariantFailure;
            return false;
        }
        const PortablePropertyRecord& current = storage_.properties[property];
        const char* keyBytes = storage_.propertyNameBytes == nullptr
            ? nullptr : storage_.propertyNameBytes + current.keyOffset;
        std::uint32_t numericIndex = 0u;
        if (!parseArrayIndex(SourceView(keyBytes, current.keyLength),
                numericIndex)) {
            if (index == ordinal) {
                result.key = SourceView(keyBytes, current.keyLength);
                result.value = current.value;
                result.symbol = current.symbol;
                result.attributes = current.attributes;
                found = true;
                return true;
            }
            ++index;
        }
        property = current.next;
        ++inspected;
    }
    return true;
}

bool PortableObjectStore::beginOwnStringPropertyIteration(
    RuntimeObjectId object, PortableObjectPropertyCursor& cursor,
    PortableObjectError& error) const
{
    error.code = PortableObjectErrorCode::None;
    cursor = PortableObjectPropertyCursor();
    PortableObjectRecord* record = nullptr;
    if (!objectRecord(object, record, error)) return false;
    if (record->array) {
        error.code = PortableObjectErrorCode::InvalidArray;
        return false;
    }
    cursor.object = object;
    return true;
}

bool PortableObjectStore::nextOwnStringProperty(
    PortableObjectPropertyCursor& cursor, PortablePropertyEntry& result,
    bool& found, std::size_t& inspections,
    PortableObjectError& error) const
{
    error.code = PortableObjectErrorCode::None;
    result = PortablePropertyEntry();
    found = false;
    inspections = 0u;
    if (cursor.object == kInvalidRuntimeObjectId) {
        error.code = PortableObjectErrorCode::InvalidObject;
        return false;
    }

    PortableObjectRecord* record = nullptr;
    if (!objectRecord(cursor.object, record, error)) return false;
    if (record->array) {
        error.code = PortableObjectErrorCode::InvalidArray;
        return false;
    }

    if (!cursor.stringsStarted) {
        bool selectedFound = false;
        std::uint32_t selectedSlot = kInvalidPortablePropertySlot;
        std::uint32_t selectedIndex = 0u;
        std::uint32_t property = record->firstProperty;
        std::size_t inspected = 0u;
        while (property != kInvalidPortablePropertySlot) {
            if (property >= limits_.maxProperties ||
                !storage_.properties[property].active ||
                inspected >= limits_.maxProperties) {
                error.code = PortableObjectErrorCode::InternalInvariantFailure;
                return false;
            }
            ++inspected;
            ++inspections;
            const PortablePropertyRecord& current =
                storage_.properties[property];
            if (current.symbol == WellKnownSymbol::None) {
                const char* keyBytes = storage_.propertyNameBytes == nullptr
                    ? nullptr : storage_.propertyNameBytes + current.keyOffset;
                std::uint32_t numericIndex = 0u;
                if (parseArrayIndex(SourceView(keyBytes, current.keyLength),
                        numericIndex) &&
                    (!cursor.hasLastArrayIndex ||
                        numericIndex > cursor.lastArrayIndex) &&
                    (!selectedFound || numericIndex < selectedIndex)) {
                    selectedFound = true;
                    selectedSlot = property;
                    selectedIndex = numericIndex;
                }
            }
            property = current.next;
        }
        if (selectedFound) {
            const PortablePropertyRecord& current =
                storage_.properties[selectedSlot];
            const char* keyBytes = storage_.propertyNameBytes == nullptr
                ? nullptr : storage_.propertyNameBytes + current.keyOffset;
            result.key = SourceView(keyBytes, current.keyLength);
            result.value = current.value;
            result.attributes = current.attributes;
            result.arrayIndex = selectedIndex;
            found = true;
            cursor.lastArrayIndex = selectedIndex;
            cursor.hasLastArrayIndex = true;
            return true;
        }

        cursor.stringsStarted = true;
        cursor.nextStringProperty = record->firstProperty;
        cursor.inspectedStringProperties = 0u;
    }

    while (cursor.nextStringProperty != kInvalidPortablePropertySlot) {
        const std::uint32_t property = cursor.nextStringProperty;
        if (property >= limits_.maxProperties ||
            !storage_.properties[property].active ||
            cursor.inspectedStringProperties >= limits_.maxProperties) {
            error.code = PortableObjectErrorCode::InternalInvariantFailure;
            return false;
        }
        ++cursor.inspectedStringProperties;
        ++inspections;
        const PortablePropertyRecord& current =
            storage_.properties[property];
        cursor.nextStringProperty = current.next;
        if (current.symbol != WellKnownSymbol::None) continue;
        const char* keyBytes = storage_.propertyNameBytes == nullptr
            ? nullptr : storage_.propertyNameBytes + current.keyOffset;
        std::uint32_t numericIndex = 0u;
        if (parseArrayIndex(SourceView(keyBytes, current.keyLength),
                numericIndex)) continue;
        result.key = SourceView(keyBytes, current.keyLength);
        result.value = current.value;
        result.attributes = current.attributes;
        found = true;
        return true;
    }
    return true;
}

bool PortableObjectStore::hasOwnArrayElement(RuntimeObjectId object,
    std::size_t index, bool& found, PortableObjectError& error) const
{
    Value value;
    return getOwnArrayElement(object, index, value, found, error);
}

bool PortableObjectStore::getOwnArrayElement(RuntimeObjectId object,
    std::size_t index, Value& value, bool& found,
    PortableObjectError& error) const
{
    error.code = PortableObjectErrorCode::None;
    value = Value::undefined();
    found = false;
    PortableObjectRecord* record = nullptr;
    if (!objectRecord(object, record, error)) return false;
    if (!record->array) {
        error.code = PortableObjectErrorCode::InvalidArray;
        return false;
    }
    if (index > limits_.maxArrayIndex) {
        error.code = PortableObjectErrorCode::InvalidIndex;
        return false;
    }
    std::uint32_t slot = kInvalidPortableArrayElementSlot;
    std::uint32_t previous = kInvalidPortableArrayElementSlot;
    if (!findArrayElement(*record, index, slot, previous)) {
        error.code = PortableObjectErrorCode::InternalInvariantFailure;
        return false;
    }
    if (slot != kInvalidPortableArrayElementSlot &&
        storage_.arrayElements[slot].index == index) {
        value = storage_.arrayElements[slot].value;
        found = true;
    }
    return true;
}

bool PortableObjectStore::getArrayElementAt(RuntimeObjectId object,
    std::size_t ordinal, PortableArrayEntry& result, bool& found,
    PortableObjectError& error) const
{
    error.code = PortableObjectErrorCode::None;
    found = false;
    result = PortableArrayEntry();
    PortableObjectRecord* record = nullptr;
    if (!objectRecord(object, record, error)) return false;
    if (!record->array) {
        error.code = PortableObjectErrorCode::InvalidArray;
        return false;
    }
    std::uint32_t element = record->firstArrayElement;
    std::size_t index = 0u;
    while (element != kInvalidPortableArrayElementSlot) {
        if (element >= limits_.maxTotalArrayElements ||
            !storage_.arrayElements[element].active ||
            index >= limits_.maxTotalArrayElements) {
            error.code = PortableObjectErrorCode::InternalInvariantFailure;
            return false;
        }
        if (index == ordinal) {
            const PortableArrayElementRecord& current =
                storage_.arrayElements[element];
            result.index = current.index;
            result.value = current.value;
            result.attributes = current.attributes;
            found = true;
            return true;
        }
        element = storage_.arrayElements[element].next;
        ++index;
    }
    return true;
}

bool PortableObjectStore::setArrayElement(RuntimeObjectId object,
    std::size_t index, Value value, PortableObjectError& error,
    std::uint8_t attributes)
{
    error.code = PortableObjectErrorCode::None;
    PortableObjectRecord* record = nullptr;
    if (!objectRecord(object, record, error)) return false;
    if (!record->array) {
        error.code = PortableObjectErrorCode::InvalidArray;
        return false;
    }
    if (index > limits_.maxArrayIndex) {
        error.code = PortableObjectErrorCode::InvalidIndex;
        return false;
    }
    if (index >= limits_.maxArrayLength || attributes > 0x3fu) {
        error.code = PortableObjectErrorCode::ArrayCapacityExceeded;
        return false;
    }
    std::uint32_t slot = kInvalidPortableArrayElementSlot;
    std::uint32_t previous = kInvalidPortableArrayElementSlot;
    if (!findArrayElement(*record, index, slot, previous)) {
        error.code = PortableObjectErrorCode::InternalInvariantFailure;
        return false;
    }
    if (slot != kInvalidPortableArrayElementSlot &&
        storage_.arrayElements[slot].index == index) {
        if ((storage_.arrayElements[slot].attributes & 0x01u) != 0u)
            storage_.arrayElements[slot].value = value;
        return true;
    }
    if (record->arrayElementCount >= limits_.maxArrayElements ||
        arrayElementCount_ >= limits_.maxTotalArrayElements) {
        error.code = PortableObjectErrorCode::ArrayCapacityExceeded;
        return false;
    }
    std::uint32_t insertedSlot = kInvalidPortableArrayElementSlot;
    if (!allocateArrayElement(insertedSlot, error)) return false;
    PortableArrayElementRecord& inserted =
        storage_.arrayElements[insertedSlot];
    inserted.index = static_cast<std::uint32_t>(index);
    inserted.value = value;
    inserted.attributes = attributes;
    inserted.active = true;
    inserted.next = slot;
    if (previous == kInvalidPortableArrayElementSlot)
        record->firstArrayElement = insertedSlot;
    else storage_.arrayElements[previous].next = insertedSlot;
    ++record->arrayElementCount;
    ++arrayElementCount_;
    const std::size_t newLength = index + 1u;
    if (newLength > record->arrayLength)
        record->arrayLength = static_cast<std::uint32_t>(newLength);
    return true;
}

bool PortableObjectStore::releaseArrayElement(std::uint32_t slot,
    PortableObjectError& error)
{
    error.code = PortableObjectErrorCode::None;
    if (slot >= limits_.maxTotalArrayElements ||
        !storage_.arrayElements[slot].active || arrayElementCount_ == 0u) {
        error.code = PortableObjectErrorCode::InternalInvariantFailure;
        return false;
    }
    storage_.arrayElements[slot] = PortableArrayElementRecord();
    storage_.arrayElements[slot].next = freeArrayElement_;
    freeArrayElement_ = slot;
    --arrayElementCount_;
    return true;
}

bool PortableObjectStore::deleteArrayElement(RuntimeObjectId object,
    std::size_t index, bool& deleted, PortableObjectError& error)
{
    error.code = PortableObjectErrorCode::None;
    deleted = false;
    PortableObjectRecord* record = nullptr;
    if (!objectRecord(object, record, error)) return false;
    if (!record->array) {
        error.code = PortableObjectErrorCode::InvalidArray;
        return false;
    }
    if (index > limits_.maxArrayIndex) return true;
    std::uint32_t slot = kInvalidPortableArrayElementSlot;
    std::uint32_t previous = kInvalidPortableArrayElementSlot;
    if (!findArrayElement(*record, index, slot, previous)) {
        error.code = PortableObjectErrorCode::InternalInvariantFailure;
        return false;
    }
    if (slot == kInvalidPortableArrayElementSlot ||
        storage_.arrayElements[slot].index != index) return true;
    if ((storage_.arrayElements[slot].attributes & 0x04u) == 0u) return true;
    if (record->arrayElementCount == 0u) {
        error.code = PortableObjectErrorCode::InternalInvariantFailure;
        return false;
    }
    if (previous == kInvalidPortableArrayElementSlot)
        record->firstArrayElement = storage_.arrayElements[slot].next;
    else storage_.arrayElements[previous].next =
        storage_.arrayElements[slot].next;
    --record->arrayElementCount;
    if (!releaseArrayElement(slot, error)) return false;
    deleted = true;
    return true;
}

bool PortableObjectStore::setArrayLength(RuntimeObjectId object,
    std::size_t length, PortableObjectError& error)
{
    error.code = PortableObjectErrorCode::None;
    PortableObjectRecord* record = nullptr;
    if (!objectRecord(object, record, error)) return false;
    if (!setArrayLengthRecord(*record, length, error)) return false;
    return true;
}

} // namespace javascript
} // namespace gxos
