#include "environment.h"

namespace gxos {
namespace javascript {

namespace {

bool checkedRange(const void* storage, std::size_t bytes)
{
    if (storage == nullptr) return bytes == 0u;
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(storage);
    const std::uintptr_t maximum = static_cast<std::uintptr_t>(-1);
    return bytes <= static_cast<std::size_t>(maximum - address);
}

bool equalBytes(SourceView left, SourceView right)
{
    if (left.length != right.length) return false;
    for (std::size_t index = 0u; index < left.length; ++index)
        if (left.data[index] != right.data[index]) return false;
    return true;
}

} // namespace

bool EnvironmentStore::configure(EnvironmentStorageView storage,
    RuntimeGeneration generation, EnvironmentError& error)
{
    const RuntimeGeneration previousGeneration = generation_;
    valid_ = false;
    storage_ = EnvironmentStorageView();
    generation_ = previousGeneration;
    environmentCount_ = 0u;
    bindingCount_ = 0u;
    nameBytesUsed_ = 0u;
    error.code = EnvironmentErrorCode::None;

    if (generation == kInvalidRuntimeGeneration) {
        error.code = EnvironmentErrorCode::InvalidStorage;
        return false;
    }
    if (previousGeneration != kInvalidRuntimeGeneration &&
        generation <= previousGeneration) {
        error.code = EnvironmentErrorCode::StaleGeneration;
        return false;
    }
    if (
        storage.environmentCapacity > kRuntimeHandleInvalidPart ||
        storage.bindingCapacity >
            static_cast<std::size_t>(kInvalidBindingIndex) ||
        (storage.environments == nullptr &&
            storage.environmentCapacity != 0u) ||
        (storage.bindings == nullptr && storage.bindingCapacity != 0u) ||
        (storage.nameBytes == nullptr && storage.nameByteCapacity != 0u)) {
        error.code = EnvironmentErrorCode::InvalidStorage;
        return false;
    }
    if ((storage.environments != nullptr &&
            reinterpret_cast<std::uintptr_t>(storage.environments) %
                alignof(EnvironmentRecord) != 0u) ||
        (storage.bindings != nullptr &&
            reinterpret_cast<std::uintptr_t>(storage.bindings) %
                alignof(Binding) != 0u)) {
        error.code = EnvironmentErrorCode::InvalidAlignment;
        return false;
    }
    if (storage.environmentCapacity >
            static_cast<std::size_t>(-1) / sizeof(EnvironmentRecord) ||
        storage.bindingCapacity >
            static_cast<std::size_t>(-1) / sizeof(Binding) ||
        !checkedRange(storage.environments,
            storage.environmentCapacity * sizeof(EnvironmentRecord)) ||
        !checkedRange(storage.bindings,
            storage.bindingCapacity * sizeof(Binding)) ||
        !checkedRange(storage.nameBytes, storage.nameByteCapacity)) {
        error.code = EnvironmentErrorCode::SizeOverflow;
        return false;
    }

    storage_ = storage;
    generation_ = generation;
    valid_ = true;
    return true;
}

void EnvironmentStore::reset(RuntimeGeneration generation)
{
    environmentCount_ = 0u;
    bindingCount_ = 0u;
    nameBytesUsed_ = 0u;
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

bool EnvironmentStore::createRoot(EnvironmentLimits limits,
    EnvironmentId& result, EnvironmentError& error)
{
    result = kInvalidEnvironmentId;
    error.code = EnvironmentErrorCode::None;
    if (!valid_) {
        error.code = EnvironmentErrorCode::InvalidStorage;
        return false;
    }
    if (environmentCount_ != 0u) {
        error.code = EnvironmentErrorCode::InternalInvariantFailure;
        return false;
    }
    if (storage_.environmentCapacity == 0u) {
        error.code = EnvironmentErrorCode::CapacityExceeded;
        return false;
    }
    if (limits.maxBindings > 0xffffu ||
        limits.maxBindingNameLength > 0xffffu) {
        error.code = EnvironmentErrorCode::CapacityExceeded;
        return false;
    }
    EnvironmentRecord recordValue;
    recordValue.parent = kInvalidEnvironmentId;
    recordValue.bindingLimit = static_cast<std::uint16_t>(limits.maxBindings);
    recordValue.nameLengthLimit =
        static_cast<std::uint16_t>(limits.maxBindingNameLength);
    recordValue.generation = generation_;
    recordValue.kind = EnvironmentKind::Global;
    recordValue.active = true;
    storage_.environments[0u] = recordValue;
    environmentCount_ = 1u;
    result = makeRuntimeHandle(generation_, 0u);
    return result != kInvalidEnvironmentId;
}

bool EnvironmentStore::create(EnvironmentId parent, EnvironmentKind kind,
    EnvironmentLimits limits, EnvironmentId& result,
    EnvironmentError& error)
{
    result = kInvalidEnvironmentId;
    error.code = EnvironmentErrorCode::None;
    if (!valid_) {
        error.code = EnvironmentErrorCode::InvalidStorage;
        return false;
    }
    if (limits.maxBindings > 0xffffu ||
        limits.maxBindingNameLength > 0xffffu) {
        error.code = EnvironmentErrorCode::CapacityExceeded;
        return false;
    }
    EnvironmentError parentError;
    if (record(parent, parentError) == nullptr) {
        error = parentError;
        return false;
    }
    if (environmentCount_ >= storage_.environmentCapacity ||
        environmentCount_ >= kRuntimeHandleInvalidPart) {
        error.code = EnvironmentErrorCode::CapacityExceeded;
        return false;
    }
    EnvironmentRecord recordValue;
    recordValue.parent = parent;
    recordValue.bindingLimit = static_cast<std::uint16_t>(limits.maxBindings);
    recordValue.nameLengthLimit =
        static_cast<std::uint16_t>(limits.maxBindingNameLength);
    recordValue.generation = generation_;
    recordValue.kind = kind;
    recordValue.active = true;
    storage_.environments[environmentCount_] = recordValue;
    result = makeRuntimeHandle(generation_,
        static_cast<std::uint32_t>(environmentCount_));
    if (result == kInvalidEnvironmentId) {
        error.code = EnvironmentErrorCode::InternalInvariantFailure;
        return false;
    }
    ++environmentCount_;
    return true;
}

bool EnvironmentStore::setParent(EnvironmentId environment,
    EnvironmentId parent, EnvironmentError& error)
{
    error.code = EnvironmentErrorCode::None;
    EnvironmentError environmentError;
    EnvironmentRecord* target = record(environment, environmentError);
    if (target == nullptr) {
        error = environmentError;
        return false;
    }
    if (runtimeHandleSlot(environment) == 0u) {
        if (parent != kInvalidEnvironmentId) {
            error.code = EnvironmentErrorCode::InvalidEnvironment;
            return false;
        }
        target->parent = kInvalidEnvironmentId;
        return true;
    }
    EnvironmentError parentError;
    if (record(parent, parentError) == nullptr) {
        error = parentError;
        return false;
    }
    EnvironmentId current = parent;
    for (std::size_t depth = 0u; depth < environmentCount_; ++depth) {
        if (current == environment) {
            error.code = EnvironmentErrorCode::ParentCycle;
            return false;
        }
        EnvironmentError currentError;
        const EnvironmentRecord* currentRecord = record(current, currentError);
        if (currentRecord == nullptr) {
            error = currentError;
            return false;
        }
        current = currentRecord->parent;
        if (current == kInvalidEnvironmentId) {
            target->parent = parent;
            return true;
        }
    }
    error.code = EnvironmentErrorCode::InternalInvariantFailure;
    return false;
}

bool EnvironmentStore::validateName(SourceView name,
    std::size_t maximumLength, EnvironmentError& error) const
{
    if (name.data == nullptr && name.length != 0u) {
        error.code = EnvironmentErrorCode::InvalidInput;
        return false;
    }
    if (name.length > maximumLength) {
        error.code = EnvironmentErrorCode::BindingNameTooLong;
        return false;
    }
    if (name.length > static_cast<std::size_t>(-1) - nameBytesUsed_) {
        error.code = EnvironmentErrorCode::SizeOverflow;
        return false;
    }
    return true;
}

std::uint32_t EnvironmentStore::findLocal(
    const EnvironmentRecord& environment, SourceView name,
    EnvironmentError& error) const
{
    std::uint32_t index = environment.firstBinding;
    std::size_t traversed = 0u;
    while (index != kInvalidBindingIndex) {
        if (index >= bindingCount_ || index >= storage_.bindingCapacity ||
            traversed >= environment.bindingCount) {
            error.code = EnvironmentErrorCode::InternalInvariantFailure;
            return kInvalidBindingIndex;
        }
        const Binding& binding = storage_.bindings[index];
        if (binding.name.data == nullptr && binding.name.length != 0u) {
            error.code = EnvironmentErrorCode::InternalInvariantFailure;
            return kInvalidBindingIndex;
        }
        if (equalBytes(binding.name, name)) return index;
        index = binding.nextIndex;
        ++traversed;
    }
    if (traversed != environment.bindingCount)
        error.code = EnvironmentErrorCode::InternalInvariantFailure;
    return kInvalidBindingIndex;
}

bool EnvironmentStore::declareImpl(EnvironmentId environment,
    SourceView name, Value value, bool immutable, bool lexical,
    bool initialized, EnvironmentError& error)
{
    error.code = EnvironmentErrorCode::None;
    EnvironmentError environmentError;
    EnvironmentRecord* target = record(environment, environmentError);
    if (target == nullptr) {
        error = environmentError;
        return false;
    }
    if (!validateName(name, target->nameLengthLimit, error)) return false;
    EnvironmentError searchError;
    const std::uint32_t existing = findLocal(*target, name, searchError);
    if (searchError.code != EnvironmentErrorCode::None) {
        error = searchError;
        return false;
    }
    if (existing != kInvalidBindingIndex) {
        if (lexical || storage_.bindings[existing].lexical) {
            error.code = EnvironmentErrorCode::DuplicateLexicalBinding;
            return false;
        }
        // `var` redeclaration preserves the original value; an initializer or
        // function declaration assigns explicitly after declaration succeeds.
        return true;
    }
    if (target->bindingCount >= target->bindingLimit) {
        error.code = EnvironmentErrorCode::BindingLimitExceeded;
        return false;
    }
    if (bindingCount_ >= storage_.bindingCapacity ||
        bindingCount_ >= static_cast<std::size_t>(kInvalidBindingIndex)) {
        error.code = EnvironmentErrorCode::CapacityExceeded;
        return false;
    }
    if (nameBytesUsed_ > storage_.nameByteCapacity ||
        name.length > storage_.nameByteCapacity - nameBytesUsed_) {
        error.code = EnvironmentErrorCode::CapacityExceeded;
        return false;
    }
    if (target->lastBinding != kInvalidBindingIndex &&
        target->lastBinding >= bindingCount_) {
        error.code = EnvironmentErrorCode::InternalInvariantFailure;
        return false;
    }

    if (name.length != 0u) {
        for (std::size_t offset = 0u; offset < name.length; ++offset)
            storage_.nameBytes[nameBytesUsed_ + offset] = name.data[offset];
    }
    const std::uint32_t bindingIndex =
        static_cast<std::uint32_t>(bindingCount_);
    const char* ownedName = storage_.nameBytes == nullptr
        ? nullptr : storage_.nameBytes + nameBytesUsed_;
    Binding binding;
    binding.name = SourceView(ownedName, name.length);
    binding.value = value;
    binding.nextIndex = kInvalidBindingIndex;
    binding.immutable = immutable;
    binding.initialized = initialized;
    binding.lexical = lexical;
    storage_.bindings[bindingIndex] = binding;

    if (target->lastBinding == kInvalidBindingIndex)
        target->firstBinding = bindingIndex;
    else
        storage_.bindings[target->lastBinding].nextIndex = bindingIndex;
    target->lastBinding = bindingIndex;
    ++target->bindingCount;
    ++bindingCount_;
    nameBytesUsed_ += name.length;
    return true;
}

bool EnvironmentStore::declare(EnvironmentId environment, SourceView name,
    Value value, EnvironmentError& error, bool immutable)
{
    return declareImpl(environment, name, value, immutable, false, true,
        error);
}

bool EnvironmentStore::declareLexical(EnvironmentId environment,
    SourceView name, bool immutable, EnvironmentError& error,
    const Value& initialValue, bool initialized)
{
    return declareImpl(environment, name, initialValue, immutable, true,
        initialized, error);
}

bool EnvironmentStore::initialize(EnvironmentId environment, SourceView name,
    Value value, EnvironmentError& error)
{
    error.code = EnvironmentErrorCode::None;
    EnvironmentError environmentError;
    EnvironmentRecord* target = record(environment, environmentError);
    if (target == nullptr) {
        error = environmentError;
        return false;
    }
    if (name.data == nullptr && name.length != 0u) {
        error.code = EnvironmentErrorCode::InvalidInput;
        return false;
    }
    EnvironmentError searchError;
    const std::uint32_t index = findLocal(*target, name, searchError);
    if (searchError.code != EnvironmentErrorCode::None) {
        error = searchError;
        return false;
    }
    if (index == kInvalidBindingIndex) {
        error.code = EnvironmentErrorCode::MissingBinding;
        return false;
    }
    Binding& binding = storage_.bindings[index];
    if (binding.initialized) {
        error.code = EnvironmentErrorCode::BindingAlreadyInitialized;
        return false;
    }
    binding.value = value;
    binding.initialized = true;
    return true;
}

bool EnvironmentStore::initializeChain(EnvironmentId start, SourceView name,
    Value value, EnvironmentError& error)
{
    Binding* binding = nullptr;
    if (!resolveChain(start, name, binding, error)) return false;
    if (binding->initialized) {
        error.code = EnvironmentErrorCode::BindingAlreadyInitialized;
        return false;
    }
    binding->value = value;
    binding->initialized = true;
    return true;
}

bool EnvironmentStore::assign(EnvironmentId environment, SourceView name,
    Value value, EnvironmentError& error)
{
    error.code = EnvironmentErrorCode::None;
    EnvironmentError environmentError;
    EnvironmentRecord* target = record(environment, environmentError);
    if (target == nullptr) {
        error = environmentError;
        return false;
    }
    if (name.data == nullptr && name.length != 0u) {
        error.code = EnvironmentErrorCode::InvalidInput;
        return false;
    }
    EnvironmentError searchError;
    const std::uint32_t index = findLocal(*target, name, searchError);
    if (searchError.code != EnvironmentErrorCode::None) {
        error = searchError;
        return false;
    }
    if (index == kInvalidBindingIndex) {
        error.code = EnvironmentErrorCode::MissingBinding;
        return false;
    }
    Binding& binding = storage_.bindings[index];
    if (!binding.initialized) {
        error.code = EnvironmentErrorCode::UninitializedBinding;
        return false;
    }
    if (binding.immutable) {
        error.code = EnvironmentErrorCode::ImmutableAssignment;
        return false;
    }
    binding.value = value;
    return true;
}

bool EnvironmentStore::resolveChain(EnvironmentId start, SourceView name,
    const Binding*& result, EnvironmentError& error) const
{
    Binding* writable = nullptr;
    const bool found = const_cast<EnvironmentStore*>(this)->resolveChain(
        start, name, writable, error);
    result = writable;
    return found;
}

bool EnvironmentStore::resolveChain(EnvironmentId start, SourceView name,
    Binding*& result, EnvironmentError& error)
{
    result = nullptr;
    error.code = EnvironmentErrorCode::None;
    if (name.data == nullptr && name.length != 0u) {
        error.code = EnvironmentErrorCode::InvalidInput;
        return false;
    }
    EnvironmentError startError;
    if (record(start, startError) == nullptr) {
        error = startError;
        return false;
    }
    EnvironmentId current = start;
    for (std::size_t depth = 0u; depth < environmentCount_; ++depth) {
        if (current == kInvalidEnvironmentId) {
            error.code = EnvironmentErrorCode::MissingBinding;
            return false;
        }
        EnvironmentError recordError;
        EnvironmentRecord* currentRecord = record(current, recordError);
        if (currentRecord == nullptr) {
            error = recordError;
            return false;
        }
        EnvironmentError searchError;
        const std::uint32_t index = findLocal(*currentRecord, name,
            searchError);
        if (searchError.code != EnvironmentErrorCode::None) {
            error = searchError;
            return false;
        }
        if (index != kInvalidBindingIndex) {
            result = &storage_.bindings[index];
            return true;
        }
        current = currentRecord->parent;
    }
    error.code = current == kInvalidEnvironmentId
        ? EnvironmentErrorCode::MissingBinding
        : EnvironmentErrorCode::InternalInvariantFailure;
    return false;
}

bool EnvironmentStore::readChain(EnvironmentId start, SourceView name,
    Value& result, EnvironmentError& error) const
{
    result = Value::undefined();
    const Binding* binding = nullptr;
    if (!resolveChain(start, name, binding, error)) return false;
    if (!binding->initialized) {
        error.code = EnvironmentErrorCode::UninitializedBinding;
        return false;
    }
    result = binding->value;
    return true;
}

bool EnvironmentStore::assignChain(EnvironmentId start, SourceView name,
    Value value, EnvironmentError& error)
{
    Binding* binding = nullptr;
    if (!resolveChain(start, name, binding, error)) return false;
    if (!binding->initialized) {
        error.code = EnvironmentErrorCode::UninitializedBinding;
        return false;
    }
    if (binding->immutable) {
        error.code = EnvironmentErrorCode::ImmutableAssignment;
        return false;
    }
    binding->value = value;
    return true;
}

bool EnvironmentStore::valid(EnvironmentId id, EnvironmentError& error) const
{
    return record(id, error) != nullptr;
}

const EnvironmentRecord* EnvironmentStore::record(EnvironmentId id,
    EnvironmentError& error) const
{
    if (!valid_) {
        error.code = EnvironmentErrorCode::InvalidStorage;
        return nullptr;
    }
    if (id == kInvalidEnvironmentId || id == kGlobalEnvironmentId) {
        error.code = EnvironmentErrorCode::InvalidEnvironment;
        return nullptr;
    }
    if (runtimeHandleGeneration(id) != generation_) {
        error.code = EnvironmentErrorCode::StaleGeneration;
        return nullptr;
    }
    const std::size_t slot = runtimeHandleSlot(id);
    if (slot >= environmentCount_ || slot >= storage_.environmentCapacity) {
        error.code = EnvironmentErrorCode::InvalidEnvironment;
        return nullptr;
    }
    const EnvironmentRecord& candidate = storage_.environments[slot];
    if (!candidate.active || candidate.generation != generation_) {
        error.code = EnvironmentErrorCode::InternalInvariantFailure;
        return nullptr;
    }
    error.code = EnvironmentErrorCode::None;
    return &candidate;
}

EnvironmentRecord* EnvironmentStore::record(EnvironmentId id,
    EnvironmentError& error)
{
    return const_cast<EnvironmentRecord*>(
        static_cast<const EnvironmentStore*>(this)->record(id, error));
}

const Binding* EnvironmentStore::resolveLocal(EnvironmentId environment,
    SourceView name, EnvironmentError& error) const
{
    const EnvironmentRecord* target = record(environment, error);
    if (target == nullptr) return nullptr;
    if (name.data == nullptr && name.length != 0u) {
        error.code = EnvironmentErrorCode::InvalidInput;
        return nullptr;
    }
    const std::uint32_t index = findLocal(*target, name, error);
    return index == kInvalidBindingIndex ? nullptr : &storage_.bindings[index];
}

Binding* EnvironmentStore::resolveLocal(EnvironmentId environment,
    SourceView name, EnvironmentError& error)
{
    return const_cast<Binding*>(static_cast<const EnvironmentStore*>(this)
        ->resolveLocal(environment, name, error));
}

const Binding* EnvironmentStore::bindingAt(EnvironmentId environment,
    std::size_t index) const
{
    EnvironmentError error;
    const EnvironmentRecord* target = record(environment, error);
    if (target == nullptr || index >= target->bindingCount) return nullptr;
    std::uint32_t bindingIndex = target->firstBinding;
    for (std::size_t current = 0u; current < index; ++current) {
        if (bindingIndex == kInvalidBindingIndex ||
            bindingIndex >= bindingCount_) return nullptr;
        bindingIndex = storage_.bindings[bindingIndex].nextIndex;
    }
    if (bindingIndex == kInvalidBindingIndex || bindingIndex >= bindingCount_)
        return nullptr;
    return &storage_.bindings[bindingIndex];
}

bool Environment::setParent(EnvironmentId parent, EnvironmentError& error)
{
    if (store_ == nullptr) {
        error.code = EnvironmentErrorCode::InvalidStorage;
        return false;
    }
    return store_->setParent(id_, parent, error);
}

void Environment::setParent(EnvironmentId parent)
{
    EnvironmentError ignored;
    (void)setParent(parent, ignored);
}

EnvironmentId Environment::parent() const
{
    if (store_ == nullptr) return kInvalidEnvironmentId;
    EnvironmentError error;
    const EnvironmentRecord* current = store_->record(id_, error);
    return current == nullptr ? kInvalidEnvironmentId : current->parent;
}

EnvironmentKind Environment::kind() const
{
    if (store_ == nullptr) return EnvironmentKind::Global;
    EnvironmentError error;
    const EnvironmentRecord* current = store_->record(id_, error);
    return current == nullptr ? EnvironmentKind::Global : current->kind;
}

bool Environment::declare(SourceView name, Value value,
    EnvironmentError& error, bool immutable)
{
    if (store_ == nullptr) {
        error.code = EnvironmentErrorCode::InvalidStorage;
        return false;
    }
    EnvironmentError inputError;
    if (!store_->valid(id_, inputError)) {
        error = inputError;
        return false;
    }
    if (name.length > limits_.maxBindingNameLength) {
        error.code = EnvironmentErrorCode::BindingNameTooLong;
        return false;
    }
    return store_->declare(id_, name, value, error, immutable);
}

bool Environment::declareLexical(SourceView name, bool immutable,
    EnvironmentError& error, const Value& initialValue, bool initialized)
{
    if (store_ == nullptr) {
        error.code = EnvironmentErrorCode::InvalidStorage;
        return false;
    }
    EnvironmentError inputError;
    if (!store_->valid(id_, inputError)) {
        error = inputError;
        return false;
    }
    if (name.length > limits_.maxBindingNameLength) {
        error.code = EnvironmentErrorCode::BindingNameTooLong;
        return false;
    }
    return store_->declareLexical(id_, name, immutable, error,
        initialValue, initialized);
}

bool Environment::initialize(SourceView name, Value value,
    EnvironmentError& error)
{
    if (store_ == nullptr) {
        error.code = EnvironmentErrorCode::InvalidStorage;
        return false;
    }
    return store_->initialize(id_, name, value, error);
}

bool Environment::initialize(SourceView name, Value value)
{
    EnvironmentError ignored;
    return initialize(name, value, ignored);
}

bool Environment::assign(SourceView name, Value value,
    EnvironmentError& error)
{
    if (store_ == nullptr) {
        error.code = EnvironmentErrorCode::InvalidStorage;
        return false;
    }
    return store_->assign(id_, name, value, error);
}

bool Environment::assign(SourceView name, Value value)
{
    EnvironmentError ignored;
    return assign(name, value, ignored);
}

bool Environment::isImmutable(SourceView name) const
{
    const Binding* binding = resolve(name);
    return binding != nullptr && binding->immutable;
}

const Binding* Environment::resolve(SourceView name) const
{
    if (store_ == nullptr) return nullptr;
    EnvironmentError error;
    return store_->resolveLocal(id_, name, error);
}

Binding* Environment::resolve(SourceView name)
{
    if (store_ == nullptr) return nullptr;
    EnvironmentError error;
    return store_->resolveLocal(id_, name, error);
}

const Value* Environment::lookup(SourceView name) const
{
    const Binding* binding = resolve(name);
    return binding == nullptr ? nullptr : &binding->value;
}

std::size_t Environment::bindingCount() const
{
    if (store_ == nullptr) return 0u;
    EnvironmentError error;
    const EnvironmentRecord* current = store_->record(id_, error);
    return current == nullptr ? 0u : current->bindingCount;
}

const Binding* Environment::bindingAt(std::size_t index) const
{
    return store_ == nullptr ? nullptr : store_->bindingAt(id_, index);
}

const char* environmentErrorCodeName(EnvironmentErrorCode code)
{
    switch (code) {
    case EnvironmentErrorCode::None: return "None";
    case EnvironmentErrorCode::InvalidStorage: return "InvalidStorage";
    case EnvironmentErrorCode::InvalidAlignment: return "InvalidAlignment";
    case EnvironmentErrorCode::InvalidInput: return "InvalidInput";
    case EnvironmentErrorCode::InvalidReference: return "InvalidReference";
    case EnvironmentErrorCode::InvalidEnvironment: return "InvalidEnvironment";
    case EnvironmentErrorCode::StaleGeneration: return "StaleGeneration";
    case EnvironmentErrorCode::MissingBinding: return "MissingBinding";
    case EnvironmentErrorCode::UninitializedBinding:
        return "UninitializedBinding";
    case EnvironmentErrorCode::BindingAlreadyInitialized:
        return "BindingAlreadyInitialized";
    case EnvironmentErrorCode::ImmutableAssignment:
        return "ImmutableAssignment";
    case EnvironmentErrorCode::DuplicateLexicalBinding:
        return "DuplicateLexicalBinding";
    case EnvironmentErrorCode::BindingLimitExceeded:
        return "BindingLimitExceeded";
    case EnvironmentErrorCode::BindingNameTooLong:
        return "BindingNameTooLong";
    case EnvironmentErrorCode::CapacityExceeded: return "CapacityExceeded";
    case EnvironmentErrorCode::AllocationFailure: return "AllocationFailure";
    case EnvironmentErrorCode::SizeOverflow: return "SizeOverflow";
    case EnvironmentErrorCode::ParentCycle: return "ParentCycle";
    case EnvironmentErrorCode::InternalInvariantFailure:
        return "InternalInvariantFailure";
    }
    return "Invalid";
}

} // namespace javascript
} // namespace gxos
