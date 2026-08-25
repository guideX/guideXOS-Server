#include "environment.h"

#include <new>

namespace gxos {
namespace javascript {

namespace {
const std::size_t kNotFound = static_cast<std::size_t>(-1);
}

void Environment::reset()
{
    bindings_.clear();
}

std::size_t Environment::find(SourceView name) const
{
    for (std::size_t index = 0; index < bindings_.size(); ++index) {
        const std::string& candidate = bindings_[index].name;
        if (candidate.size() != name.length) continue;
        bool matches = true;
        for (std::size_t offset = 0; offset < name.length; ++offset) {
            if (candidate[offset] != name.data[offset]) {
                matches = false;
                break;
            }
        }
        if (matches) return index;
    }
    return kNotFound;
}

bool Environment::declare(SourceView name, Value value, EnvironmentError& error)
{
    error = EnvironmentError();
    if (name.data == nullptr && name.length != 0) {
        error.code = EnvironmentErrorCode::AllocationFailure;
        return false;
    }
    if (name.length > limits_.maxBindingNameLength) {
        error.code = EnvironmentErrorCode::BindingNameTooLong;
        return false;
    }

    const std::size_t existing = find(name);
    if (existing != kNotFound) {
        bindings_[existing].value = value;
        return true;
    }
    if (bindings_.size() >= limits_.maxBindings) {
        error.code = EnvironmentErrorCode::BindingLimitExceeded;
        return false;
    }

    try {
        Binding binding;
        binding.name.assign(name.data == nullptr ? "" : name.data, name.length);
        binding.value = value;
        bindings_.push_back(binding);
    } catch (const std::bad_alloc&) {
        error.code = EnvironmentErrorCode::AllocationFailure;
        return false;
    }
    return true;
}

bool Environment::assign(SourceView name, Value value)
{
    const std::size_t index = find(name);
    if (index == kNotFound) return false;
    bindings_[index].value = value;
    return true;
}

const Value* Environment::lookup(SourceView name) const
{
    const std::size_t index = find(name);
    return index == kNotFound ? nullptr : &bindings_[index].value;
}

const Binding* Environment::bindingAt(std::size_t index) const
{
    return index < bindings_.size() ? &bindings_[index] : nullptr;
}

const char* environmentErrorCodeName(EnvironmentErrorCode code)
{
    switch (code) {
    case EnvironmentErrorCode::None: return "None";
    case EnvironmentErrorCode::BindingLimitExceeded:
        return "BindingLimitExceeded";
    case EnvironmentErrorCode::BindingNameTooLong: return "BindingNameTooLong";
    case EnvironmentErrorCode::AllocationFailure: return "AllocationFailure";
    }
    return "Invalid";
}

} // namespace javascript
} // namespace gxos
