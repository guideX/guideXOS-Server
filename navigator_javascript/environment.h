#pragma once

#include "source.h"
#include "value.h"

#include <cstddef>
#include <string>
#include <vector>

namespace gxos {
namespace javascript {

using EnvironmentId = std::uint32_t;
constexpr EnvironmentId kGlobalEnvironmentId = 0u;
constexpr EnvironmentId kInvalidEnvironmentId = 0xffffffffu;

enum class EnvironmentErrorCode : std::uint8_t {
    None = 0,
    BindingLimitExceeded,
    BindingNameTooLong,
    AllocationFailure,
};

struct EnvironmentError {
    EnvironmentErrorCode code = EnvironmentErrorCode::None;
};

struct EnvironmentLimits {
    std::size_t maxBindings = 256u;
    std::size_t maxBindingNameLength = 256u;
};

struct Binding {
    std::string name;
    Value value;
};

// Names are owned by the environment; lookup and assignment only borrow the
// caller's name view for the duration of the operation. Environment::declare
// is idempotent for redeclarations, allowing the evaluator to preserve an
// existing `var` value until an initializer or function binding assigns it.
class Environment {
public:
    explicit Environment(EnvironmentLimits limits = EnvironmentLimits(),
        EnvironmentId parent = kInvalidEnvironmentId)
        : limits_(limits), parent_(parent) {}

    void reset();

    EnvironmentId parent() const { return parent_; }
    void setParent(EnvironmentId parent) { parent_ = parent; }

    bool declare(SourceView name, Value value, EnvironmentError& error);
    bool assign(SourceView name, Value value);
    const Value* lookup(SourceView name) const;

    std::size_t bindingCount() const { return bindings_.size(); }
    const Binding* bindingAt(std::size_t index) const;
    const EnvironmentLimits& limits() const { return limits_; }

private:
    std::size_t find(SourceView name) const;

    EnvironmentLimits limits_;
    EnvironmentId parent_ = kInvalidEnvironmentId;
    std::vector<Binding> bindings_;
};

const char* environmentErrorCodeName(EnvironmentErrorCode code);

} // namespace javascript
} // namespace gxos
