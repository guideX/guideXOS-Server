# Copilot Instructions

## Project Guidelines
- Do not create fake stubs that silently pretend apps or features exist; if a runtime target cannot support an app, prefer explicit unavailable launch errors or accurate target registration.

### App registration and naming
- Prefer app-model-first registration for default bundled apps: use the normal app registry, manifest, launch, lifecycle, and desktop/start-menu mechanisms.
- Avoid one-off hardcoded bypasses; use them only if the repository-wide pattern explicitly requires them.
- Use consistent app and file naming when matching existing conventions: use display names like 'guideXOS Navigator' and lowercase filenames such as navigator.cpp and navigator.h.