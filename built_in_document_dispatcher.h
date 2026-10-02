#pragma once

#include "app_registry.h"

#include <string>
#include <utility>
#include <vector>

namespace gxos { namespace apps {

// Routes canonical built-in app identities to their document activation entry
// points. Extensions and document contents stay outside this dispatcher;
// AppRegistry resolves capability and default policy first.
class BuiltInDocumentDispatcher {
public:
    using Handler = bool (*)(const AppActivationContext& activation, std::string& error);

    bool RegisterHandler(std::string canonicalAppId, Handler handler) {
        if (canonicalAppId.empty() || canonicalAppId.size() > kAppModelMaxAppIdBytes || !handler) return false;
        for (const Entry& entry : m_entries) {
            if (entry.appId == canonicalAppId) return false;
        }
        m_entries.push_back({ std::move(canonicalAppId), handler });
        return true;
    }

    bool Dispatch(const AppRegistry& registry,
                  const AppActivationContext& activation,
                  std::string& error) const {
        error.clear();
        if (activation.kind != AppActivationKind::Document) {
            error = "Built-in document dispatcher rejects non-document activation";
            return false;
        }
        if (activation.appId.empty() || activation.appId.size() > kAppModelMaxAppIdBytes ||
            !IsValidDocumentActivationPath(activation.documentPath)) {
            error = "Built-in document activation target is invalid or overlong";
            return false;
        }

        const RegisteredApp* app = registry.FindById(activation.appId);
        if (!app) {
            error = "Built-in document activation target application is missing";
            return false;
        }
        if (app->manifest.kind != AppKind::BuiltIn) {
            error = "Built-in document dispatcher rejected a non-built-in target";
            return false;
        }
        if (!registry.IsDocumentActivationCurrent(activation)) {
            error = "Built-in document activation target is stale or unavailable";
            return false;
        }

        for (const Entry& entry : m_entries) {
            if (entry.appId != activation.appId) continue;
            if (entry.handler(activation, error)) return true;
            if (error.empty()) error = "Built-in document activation handler failed";
            return false;
        }
        error = "No built-in document activation dispatcher is registered for this application";
        return false;
    }

private:
    struct Entry {
        std::string appId;
        Handler handler = nullptr;
    };

    std::vector<Entry> m_entries;
};

}} // namespace gxos::apps
