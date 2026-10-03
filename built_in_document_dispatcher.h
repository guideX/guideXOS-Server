#pragma once

#include "app_registry.h"

#include <string>
#include <utility>
#include <vector>

namespace gxos { namespace apps {

// Routes canonical built-in app identities to their activation entry points.
// Handler discovery and default selection stay in AppRegistry.
class BuiltInActivationDispatcher {
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
        const bool document = activation.kind == AppActivationKind::Document;
        const bool uri = activation.kind == AppActivationKind::Uri;
        const bool folder = activation.kind == AppActivationKind::Folder;
        if ((!document && !uri && !folder) || activation.appId.empty() || activation.appId.size() > kAppModelMaxAppIdBytes ||
            (document && !IsValidDocumentActivationPath(activation.documentPath)) ||
            (folder && !IsValidFolderActivationPath(activation.folderPath)) ||
            (uri && !IsValidUriActivationUri(activation.uri))) {
            error = "Built-in activation target is invalid, unsupported, or overlong";
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
        const bool current = document ? registry.IsDocumentActivationCurrent(activation)
            : (uri ? registry.IsUriActivationCurrent(activation) : registry.IsFolderActivationCurrent(activation));
        if (!current) {
            error = "Built-in activation target is stale or unavailable";
            return false;
        }

        for (const Entry& entry : m_entries) {
            if (entry.appId != activation.appId) continue;
            if (entry.handler(activation, error)) return true;
            if (error.empty()) error = "Built-in document activation handler failed";
            return false;
        }
        error = "No built-in activation dispatcher is registered for this application";
        return false;
    }

private:
    struct Entry {
        std::string appId;
        Handler handler = nullptr;
    };

    std::vector<Entry> m_entries;
};

// Source compatibility for existing document-only tests and callers.
using BuiltInDocumentDispatcher = BuiltInActivationDispatcher;

}} // namespace gxos::apps
