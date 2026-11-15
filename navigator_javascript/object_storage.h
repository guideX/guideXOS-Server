#pragma once

#include "source.h"
#include "value.h"

#include <cstddef>
#include <cstdint>

namespace gxos {
namespace javascript {

constexpr std::uint32_t kInvalidPortableObjectSlot = 0xffffffffu;
constexpr std::uint32_t kInvalidPortablePropertySlot = 0xffffffffu;
constexpr std::uint32_t kInvalidPortableArrayElementSlot = 0xffffffffu;

enum class PortableObjectErrorCode : std::uint8_t {
    None = 0,
    InvalidStorage,
    InvalidObject,
    InvalidArray,
    InvalidProperty,
    StaleGeneration,
    InvalidIndex,
    InvalidLength,
    InvalidPrototype,
    PrototypeCycle,
    PrototypeDepthExceeded,
    NameTooLong,
    ObjectCapacityExceeded,
    PropertyCapacityExceeded,
    ArrayCapacityExceeded,
    NameStorageExhausted,
    ArithmeticOverflow,
    InternalInvariantFailure,
};

struct PortableObjectError {
    PortableObjectErrorCode code = PortableObjectErrorCode::None;
};

struct PortableObjectLimits {
    std::size_t maxObjects = 1024u;
    std::size_t maxProperties = 4096u;
    std::size_t maxPropertiesPerObject = 256u;
    std::size_t maxPropertyNameLength = 256u;
    std::size_t maxPropertyNameBytes = 256u * 1024u;
    std::size_t maxArrayElements = 1024u;
    std::size_t maxTotalArrayElements = 4096u;
    std::size_t maxArrayIndex = 1023u;
    std::size_t maxArrayLength = 1024u;
    std::size_t maxPrototypeDepth = 32u;
};

struct PortableObjectRecord {
    RuntimeObjectId prototype = kInvalidRuntimeObjectId;
    std::uint32_t firstProperty = kInvalidPortablePropertySlot;
    std::uint32_t lastProperty = kInvalidPortablePropertySlot;
    std::uint32_t firstArrayElement = kInvalidPortableArrayElementSlot;
    std::uint32_t arrayLength = 0u;
    std::uint16_t propertyCount = 0u;
    std::uint16_t arrayElementCount = 0u;
    bool array = false;
    bool active = false;
};

struct PortablePropertyRecord {
    std::uint32_t next = kInvalidPortablePropertySlot;
    std::uint32_t keyOffset = 0u;
    std::uint32_t keyLength = 0u;
    Value value = Value::undefined();
    WellKnownSymbol symbol = WellKnownSymbol::None;
    std::uint8_t attributes = 0x07u; // writable, enumerable, configurable
    bool active = false;
};

struct PortableArrayElementRecord {
    std::uint32_t next = kInvalidPortableArrayElementSlot;
    std::uint32_t index = 0u;
    Value value = Value::undefined();
    std::uint8_t attributes = 0x07u;
    bool active = false;
};

struct PortableObjectStorageView {
    PortableObjectRecord* objects = nullptr;
    std::size_t objectCapacity = 0u;
    PortablePropertyRecord* properties = nullptr;
    std::size_t propertyCapacity = 0u;
    PortableArrayElementRecord* arrayElements = nullptr;
    std::size_t arrayElementCapacity = 0u;
    char* propertyNameBytes = nullptr;
    std::size_t propertyNameCapacity = 0u;
};

struct PortablePropertyEntry {
    SourceView key;
    Value value = Value::undefined();
    WellKnownSymbol symbol = WellKnownSymbol::None;
    std::uint8_t attributes = 0u;
    std::uint32_t arrayIndex = 0u;
    bool isArrayElement = false;
    bool isArrayLength = false;
};

struct PortableArrayEntry {
    std::uint32_t index = 0u;
    Value value = Value::undefined();
    std::uint8_t attributes = 0x07u;
};

// Caller-backed, generation-checked object and Array records for the portable
// execution slice and future hosted adapters. Names are copied into the
// bounded arena; object and element Values remain opaque handles or scalars.
class PortableObjectStore {
public:
    PortableObjectStore() = default;

    bool configure(PortableObjectStorageView storage,
        PortableObjectLimits limits, RuntimeGeneration generation,
        PortableObjectError& error);
    bool reset(RuntimeGeneration generation, PortableObjectError& error);

    bool configured() const { return configured_; }
    RuntimeGeneration generation() const { return generation_; }
    std::size_t objectCount() const { return objectCount_; }
    std::size_t propertyCount() const { return propertyCount_; }
    std::size_t propertyNameBytesUsed() const { return propertyNameBytesUsed_; }
    std::size_t arrayElementCount() const { return arrayElementCount_; }
    const PortableObjectLimits& limits() const { return limits_; }

    bool createObject(RuntimeObjectId prototype, RuntimeObjectId& result,
        PortableObjectError& error);
    bool createArray(std::size_t length, RuntimeObjectId prototype,
        RuntimeObjectId& result, PortableObjectError& error);
    bool validObject(RuntimeObjectId object, PortableObjectError& error) const;
    bool isArray(RuntimeObjectId object, bool& result,
        PortableObjectError& error) const;
    bool arrayLength(RuntimeObjectId object, std::size_t& result,
        PortableObjectError& error) const;
    bool setPrototype(RuntimeObjectId object, RuntimeObjectId prototype,
        PortableObjectError& error);
    bool prototypeOf(RuntimeObjectId object, RuntimeObjectId& result,
        PortableObjectError& error) const;

    bool hasOwnProperty(RuntimeObjectId object, SourceView key, bool& found,
        PortableObjectError& error) const;
    bool getOwnProperty(RuntimeObjectId object, SourceView key, Value& value,
        bool& found, PortableObjectError& error) const;
    bool getProperty(RuntimeObjectId object, SourceView key, Value& value,
        bool& found, PortableObjectError& error) const;
    bool setOwnProperty(RuntimeObjectId object, SourceView key, Value value,
        PortableObjectError& error, std::uint8_t attributes = 0x07u);
    bool deleteOwnProperty(RuntimeObjectId object, SourceView key,
        bool& deleted, PortableObjectError& error);
    bool getOwnPropertyAt(RuntimeObjectId object, std::size_t ordinal,
        PortablePropertyEntry& result, bool& found,
        PortableObjectError& error) const;

    bool hasOwnArrayElement(RuntimeObjectId object, std::size_t index,
        bool& found, PortableObjectError& error) const;
    bool getOwnArrayElement(RuntimeObjectId object, std::size_t index,
        Value& value, bool& found, PortableObjectError& error) const;
    bool getArrayElementAt(RuntimeObjectId object, std::size_t ordinal,
        PortableArrayEntry& result, bool& found,
        PortableObjectError& error) const;
    bool setArrayElement(RuntimeObjectId object, std::size_t index,
        Value value, PortableObjectError& error,
        std::uint8_t attributes = 0x07u);
    bool deleteArrayElement(RuntimeObjectId object, std::size_t index,
        bool& deleted, PortableObjectError& error);
    bool setArrayLength(RuntimeObjectId object, std::size_t length,
        PortableObjectError& error);

private:
    bool objectRecord(RuntimeObjectId object, PortableObjectRecord*& record,
        PortableObjectError& error) const;
    bool propertyKeyMatches(const PortablePropertyRecord& property,
        SourceView key) const;
    bool findProperty(const PortableObjectRecord& object, SourceView key,
        std::uint32_t& slot) const;
    bool findArrayElement(const PortableObjectRecord& object,
        std::size_t index, std::uint32_t& slot,
        std::uint32_t& previous) const;
    bool parseArrayIndex(SourceView key, std::uint32_t& index) const;
    bool setArrayLengthRecord(PortableObjectRecord& object,
        std::size_t length, PortableObjectError& error);
    bool allocateProperty(std::uint32_t& slot, PortableObjectError& error);
    bool allocateArrayElement(std::uint32_t& slot,
        PortableObjectError& error);
    bool compactPropertyNames(std::uint32_t removedOffset,
        std::uint32_t removedLength, PortableObjectError& error);
    bool releaseProperty(std::uint32_t slot, PortableObjectError& error);
    bool releaseArrayElement(std::uint32_t slot,
        PortableObjectError& error);
    PortableObjectStorageView storage_;
    PortableObjectLimits limits_;
    RuntimeGeneration generation_ = kInvalidRuntimeGeneration;
    std::size_t objectCount_ = 0u;
    std::size_t propertyCount_ = 0u;
    std::size_t propertyNameBytesUsed_ = 0u;
    std::size_t arrayElementCount_ = 0u;
    std::uint32_t freeProperty_ = kInvalidPortablePropertySlot;
    std::uint32_t freeArrayElement_ = kInvalidPortableArrayElementSlot;
    bool configured_ = false;
};

} // namespace javascript
} // namespace gxos
