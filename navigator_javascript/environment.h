#pragma once

#include "source.h"
#include "value.h"

#include <cstddef>
#include <string>
#include <vector>

namespace gxos {
namespace javascript {

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

// JS3 has one global var environment.  Names are owned by the environment;
// lookup and assignment only borrow the caller's name view for the duration
// of the operation.  Redeclaration updates the existing entry, so the
// binding vector cannot grow from repeated `var x` declarations.
class Environment {
public:
    explicit Environment(EnvironmentLimits limits = EnvironmentLimits())
        : limits_(limits) {}

    void reset();

    bool declare(SourceView name, Value value, EnvironmentError& error);
    bool assign(SourceView name, Value value);
    const Value* lookup(SourceView name) const;

    std::size_t bindingCount() const { return bindings_.size(); }
    const Binding* bindingAt(std::size_t index) const;
    const EnvironmentLimits& limits() const { return limits_; }

private:
    std::size_t find(SourceView name) const;

    EnvironmentLimits limits_;
    std::vector<Binding> bindings_;
};

const char* environmentErrorCodeName(EnvironmentErrorCode code);

} // namespace javascript
} // namespace gxos
