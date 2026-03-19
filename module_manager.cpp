#include "module_manager.h"
#include "logger.h"
#include "notepad.h"
#include "calculator.h"
#include "paint.h"
#include "image_viewer.h"
#include "clock.h"
#include "task_manager.h"
#include "file_explorer.h"
#include "console_window.h"
#include "onscreen_keyboard.h"
#include "shutdown_dialog.h"

namespace gxos {

std::vector<ModuleManager::ModuleEntry> ModuleManager::s_modules;

void ModuleManager::Register(const std::string& name, std::function<void()> launchAction) {
    // Prevent duplicates
    for (const auto& m : s_modules) {
        if (m.name == name) return;
    }
    s_modules.push_back({name, std::move(launchAction)});
    Logger::write(LogLevel::Info, "Module registered: " + name);
}

bool ModuleManager::Launch(const std::string& name) {
    for (const auto& m : s_modules) {
        if (m.name == name) {
            if (m.launchAction) {
                m.launchAction();
                Logger::write(LogLevel::Info, "Module launched: " + name);
                return true;
            }
        }
    }
    Logger::write(LogLevel::Info, "Module not found: " + name);
    return false;
}

size_t ModuleManager::Count() {
    return s_modules.size();
}

const std::string& ModuleManager::Name(size_t index) {
    return s_modules[index].name;
}

std::vector<std::string> ModuleManager::ListNames() {
    std::vector<std::string> names;
    names.reserve(s_modules.size());
    for (const auto& m : s_modules) {
        names.push_back(m.name);
    }
    return names;
}

void ModuleManager::InitializeBuiltins() {
    Register("Notepad",        []{ apps::Notepad::Launch(); });
    Register("Calculator",     []{ apps::Calculator::Launch(); });
    Register("Paint",          []{ apps::Paint::Launch(); });
    Register("ImageViewer",    []{ apps::ImageViewer::Launch(); });
    Register("Clock",          []{ apps::Clock::Launch(); });
    Register("TaskManager",    []{ apps::TaskManager::Launch(); });
    Register("FileExplorer",   []{ apps::FileExplorer::Launch(); });
    Register("Console",        []{ apps::ConsoleWindow::Launch(); });
    Register("OnScreenKeyboard", []{ apps::OnScreenKeyboard::Launch(); });
    Register("ShutdownDialog", []{ apps::ShutdownDialog::Launch(); });
    Logger::write(LogLevel::Info, "Built-in modules registered (" + std::to_string(s_modules.size()) + ")");
}

} // namespace gxos
