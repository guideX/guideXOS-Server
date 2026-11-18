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

// Caller-owned copy of an own enumerable string key. The byte range points
// into the key arena supplied to snapshotOwnEnumerableStringProperties, so a
// later property deletion, insertion, or name-arena compaction cannot change
// the captured key set.
struct PortablePropertyKeySnapshotEntry {
    std::uint32_t byteOffset = 0u;
    std::uint32_t byteLength = 0u;
};

// Iterates ordinary-object own string properties in ECMAScript property
// order: canonical array-index keys first, followed by remaining strings in
// insertion order. Symbols are excluded. The iterator is read-only and does
// not retain pointers to property-name storage.
struct PortableObjectPropertyCursor {
    RuntimeObjectId object = kInvalidRuntimeObjectId;
    std::uint32_t lastArrayIndex = 0u;
    std::uint32_t nextStringProperty = kInvalidPortablePropertySlot;
    std::size_t inspectedStringProperties = 0u;
    bool hasLastArrayIndex = false;
    bool stringsStarted = false;
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
    // Removes only an unpublished tail of object records and its properties
    // and Array elements. Existing records that refer into the removed tail
    // make the rollback fail without changing the store.
    bool rollbackObjects(std::size_t retainedCount,
        PortableObjectError& error);

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
    bool hasOwnProperty(RuntimeObjectId object, WellKnownSymbol symbol,
        bool& found, PortableObjectError& error) const;
    bool getOwnProperty(RuntimeObjectId object, SourceView key, Value& value,
        bool& found, PortableObjectError& error) const;
    bool getOwnProperty(RuntimeObjectId object, WellKnownSymbol symbol,
        Value& value, bool& found, PortableObjectError& error) const;
    bool getOwnPropertyEntry(RuntimeObjectId object, SourceView key,
        PortablePropertyEntry& result, bool& found,
        PortableObjectError& error) const;
    bool getOwnPropertyEntry(RuntimeObjectId object, WellKnownSymbol symbol,
        PortablePropertyEntry& result, bool& found,
        PortableObjectError& error) const;
    bool getProperty(RuntimeObjectId object, SourceView key, Value& value,
        bool& found, PortableObjectError& error) const;
    bool getProperty(RuntimeObjectId object, WellKnownSymbol symbol,
        Value& value, bool& found, PortableObjectError& error) const;
    bool setOwnProperty(RuntimeObjectId object, SourceView key, Value value,
        PortableObjectError& error, std::uint8_t attributes = 0x07u);
    bool setOwnProperty(RuntimeObjectId object, WellKnownSymbol symbol,
        Value value, PortableObjectError& error,
        std::uint8_t attributes = 0x07u);
    bool updateOwnProperty(RuntimeObjectId object, SourceView key,
        Value value, std::uint8_t attributes, PortableObjectError& error);
    bool updateOwnProperty(RuntimeObjectId object, WellKnownSymbol symbol,
        Value value, std::uint8_t attributes, PortableObjectError& error);
    bool deleteOwnProperty(RuntimeObjectId object, SourceView key,
        bool& deleted, PortableObjectError& error);
    bool deleteOwnProperty(RuntimeObjectId object, WellKnownSymbol symbol,
        bool& deleted, PortableObjectError& error);
    bool getOwnPropertyAt(RuntimeObjectId object, std::size_t ordinal,
        PortablePropertyEntry& result, bool& found,
        PortableObjectError& error) const;
    bool beginOwnStringPropertyIteration(RuntimeObjectId object,
        PortableObjectPropertyCursor& cursor,
        PortableObjectError& error) const;
    bool nextOwnStringProperty(PortableObjectPropertyCursor& cursor,
        PortablePropertyEntry& result, bool& found,
        std::size_t& inspections, PortableObjectError& error) const;
    bool snapshotOwnEnumerableStringProperties(RuntimeObjectId object,
        PortablePropertyKeySnapshotEntry* entries, std::size_t entryCapacity,
        char* keyBytes, std::size_t keyByteCapacity,
        std::size_t& entryCount, std::size_t& keyBytesUsed,
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
    friend struct NavigatorScriptRuntimeTestAccess;

    bool objectRecord(RuntimeObjectId object, PortableObjectRecord*& record,
        PortableObjectError& error) const;
    bool propertyKeyMatches(const PortablePropertyRecord& property,
        SourceView key) const;
    bool propertyKeyMatches(const PortablePropertyRecord& property,
        SourceView key, WellKnownSymbol symbol) const;
    bool findProperty(const PortableObjectRecord& object, SourceView key,
        std::uint32_t& slot) const;
    bool findProperty(const PortableObjectRecord& object, SourceView key,
        WellKnownSymbol symbol, std::uint32_t& slot) const;
    bool findArrayElement(const PortableObjectRecord& object,
        std::size_t index, std::uint32_t& slot,
        std::uint32_t& previous) const;
    bool parseArrayIndex(SourceView key, std::uint32_t& index) const;
    bool setArrayLengthRecord(PortableObjectRecord& object,
        std::size_t length, PortableObjectError& error);
    bool allocateProperty(std::uint32_t& slot, PortableObjectError& error);
    bool allocateArrayElement(std::uint32_t& slot,
        PortableObjectError& error);
    bool setOwnPropertyImpl(RuntimeObjectId object, SourceView key,
        WellKnownSymbol symbol, Value value, PortableObjectError& error,
        std::uint8_t attributes);
    bool getOwnPropertyEntry(RuntimeObjectId object, SourceView key,
        WellKnownSymbol symbol, PortablePropertyEntry& result, bool& found,
        PortableObjectError& error) const;
    bool updateOwnPropertyImpl(RuntimeObjectId object, SourceView key,
        WellKnownSymbol symbol, Value value, std::uint8_t attributes,
        PortableObjectError& error);
    bool deleteOwnPropertyImpl(RuntimeObjectId object, SourceView key,
        WellKnownSymbol symbol, bool& deleted, PortableObjectError& error);
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
