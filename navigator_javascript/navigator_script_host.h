#pragma once

#include "host.h"

namespace gxos {
namespace javascript {

// Compile-time ownership boundary for a future Navigator page adapter. This
// JS7 shell intentionally exposes no document, element, URL, or mutable DOM
// behavior. Navigator owns the page generation and can invalidate this adapter
// when a page is replaced without changing the generic evaluator.
class NavigatorScriptHostAdapter final : public HostAdapter {
public:
    explicit NavigatorScriptHostAdapter(HostGenerationId generation = 1u)
        : generation_(generation) {}

    void setGeneration(HostGenerationId generation) { generation_ = generation; }
    HostGenerationId generation() const { return generation_; }

    HostResult validate(const HostObjectReference& object) override;
    HostResult getProperty(const HostObjectReference& object,
        SourceView property, HostValue& result) override;
    HostResult setProperty(const HostObjectReference& object,
        SourceView property, const HostValue& value) override;
    HostResult call(const HostObjectReference* receiver,
        std::uint32_t methodId, const HostValue* arguments,
        std::size_t argumentCount, HostValue& result) override;

private:
    HostGenerationId generation_ = 1u;
};

} // namespace javascript
} // namespace gxos
