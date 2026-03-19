#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace gxos {

    /// Module registry ported from guideXOS.Legacy ModuleManager.cs
    /// Lightweight pluggable component system — register modules by name
    /// and launch them by name at runtime.
    class ModuleManager {
    public:
        /// Register a module with a launch callback
        static void Register(const std::string& name, std::function<void()> launchAction);

        /// Launch a module by name
        /// @return true if the module was found and launched
        static bool Launch(const std::string& name);

        /// Number of registered modules
        static size_t Count();

        /// Get the name of module at index
        static const std::string& Name(size_t index);

        /// Get a list of all registered module names
        static std::vector<std::string> ListNames();

        /// Register built-in modules (Notepad, Calculator, Paint, etc.)
        static void InitializeBuiltins();

    private:
        struct ModuleEntry {
            std::string name;
            std::function<void()> launchAction;
        };
        static std::vector<ModuleEntry> s_modules;
    };

} // namespace gxos
