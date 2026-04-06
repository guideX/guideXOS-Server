//
// guideXOS Kernel GUI Apps
//
// Sample GUI applications that can run in bare-metal/UEFI mode
// without the user-mode server.
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_KERNEL_APPS_H
#define KERNEL_KERNEL_APPS_H

#include "kernel/kernel_app.h"

namespace kernel {
namespace apps {

// ============================================================
// Notepad App
// ============================================================

class NotepadApp : public app::KernelApp {
public:
    NotepadApp();
    virtual ~NotepadApp() override;
    
    virtual bool init() override;
    virtual void shutdown() override;
    virtual void draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) override;
    
    virtual void onKeyChar(char c) override;
    virtual void onKeyDown(uint32_t key) override;
    virtual void onMouseDown(int x, int y, uint8_t button) override;
    
    static app::KernelApp* create() { return new NotepadApp(); }
    
private:
    static const int MAX_TEXT_LENGTH = 4096;
    static const int MAX_LINES = 100;
    char m_text[MAX_TEXT_LENGTH];
    int m_textLength;
    int m_cursorPos;
    int m_scrollY;
    
    void insertChar(char c);
    void deleteChar();
    void moveCursor(int delta);
    int getLineCount() const;
    int getLineStart(int lineIndex) const;
};

// ============================================================
// Calculator App
// ============================================================

class CalculatorApp : public app::KernelApp {
public:
    CalculatorApp();
    virtual ~CalculatorApp() override;
    
    virtual bool init() override;
    virtual void shutdown() override;
    virtual void draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) override;
    
    virtual void onWidgetClick(int widgetId) override;
    virtual void onKeyChar(char c) override;
    
    static app::KernelApp* create() { return new CalculatorApp(); }
    
private:
    double m_accumulator;
    double m_operand;
    char m_operation;
    bool m_newNumber;
    char m_display[32];
    
    // Widget IDs
    int m_displayId;
    int m_btnIds[20];  // 0-9, +, -, *, /, =, C, CE, ., +/-, %
    
    void handleButton(char btn);
    void updateDisplay();
    void calculate();
    void clear();
    void clearEntry();
};

// ============================================================
// Task Manager App
// ============================================================

class TaskManagerApp : public app::KernelApp {
public:
    TaskManagerApp();
    virtual ~TaskManagerApp() override;
    
    virtual bool init() override;
    virtual void shutdown() override;
    virtual void update() override;
    virtual void draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) override;
    
    virtual void onMouseDown(int x, int y, uint8_t button) override;
    virtual void onWidgetClick(int widgetId) override;
    
    static app::KernelApp* create() { return new TaskManagerApp(); }
    
private:
    int m_selectedApp;
    int m_refreshBtnId;
    int m_endTaskBtnId;
    uint32_t m_lastUpdate;
    
    struct AppEntry {
        char name[app::MAX_APP_NAME];
        int windowCount;
        bool running;
    };
    static const int MAX_ENTRIES = 16;
    AppEntry m_entries[MAX_ENTRIES];
    int m_entryCount;
    
    void refreshList();
};

// ============================================================
// App Registration
// ============================================================

// Call this once to register all kernel GUI apps
void registerKernelApps();

} // namespace apps
} // namespace kernel

#endif // KERNEL_KERNEL_APPS_H
