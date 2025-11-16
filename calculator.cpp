#include "calculator.h"
#include "gui_protocol.h"
#include "logger.h"
#include <sstream>
#include <cmath>
#include <iomanip>

namespace gxos { namespace apps {
    
    using namespace gxos::gui;
    
    // Static member initialization
    uint64_t Calculator::s_windowId = 0;
    double Calculator::s_currentValue = 0.0;
    double Calculator::s_storedValue = 0.0;
    char Calculator::s_operation = '\0';
    std::string Calculator::s_display = "0";
    bool Calculator::s_newNumber = true;
    bool Calculator::s_shiftPressed = false;
    int Calculator::s_lastKeyCode = 0;
    bool Calculator::s_keyDown = false;
    
    uint64_t Calculator::Launch() {
        ProcessSpec spec{"calculator", Calculator::main};
        return ProcessTable::spawn(spec, {"calculator"});
    }
    
    int Calculator::main(int argc, char** argv) {
        try {
            Logger::write(LogLevel::Info, "Calculator starting...");
            
            // Initialize state
            s_windowId = 0;
            s_currentValue = 0.0;
            s_storedValue = 0.0;
            s_operation = '\0';
            s_display = "0";
            s_newNumber = true;
            s_shiftPressed = false;
            s_lastKeyCode = 0;
            s_keyDown = false;
            
            // Subscribe to IPC channels
            const char* kGuiChanIn = "gui.input";
            const char* kGuiChanOut = "gui.output";
            ipc::Bus::ensure(kGuiChanIn);
            ipc::Bus::ensure(kGuiChanOut);
            
            // Create window (320x420)
            ipc::Message createMsg;
            createMsg.type = (uint32_t)MsgType::MT_Create;
            std::ostringstream oss;
            oss << "Calculator|320|420";
            std::string payload = oss.str();
            createMsg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(createMsg), false);
            
            // Main event loop
            bool running = true;
            while (running) {
                ipc::Message msg;
                if (ipc::Bus::pop(kGuiChanOut, msg, 100)) {
                    MsgType msgType = (MsgType)msg.type;
                    
                    switch (msgType) {
                        case MsgType::MT_Create: {
                            // Window created - extract window ID
                            std::string payload(msg.data.begin(), msg.data.end());
                            size_t sep = payload.find('|');
                            if (sep != std::string::npos && sep > 0) {
                                try {
                                    std::string idStr = payload.substr(0, sep);
                                    s_windowId = std::stoull(idStr);
                                    Logger::write(LogLevel::Info, std::string("Calculator window created: ") + std::to_string(s_windowId));
                                    
                                    // Helper lambda to add a button
                                    auto addButton = [](int id, int x, int y, int w, int h, const std::string& text) {
                                        ipc::Message msg;
                                        msg.type = (uint32_t)MsgType::MT_WidgetAdd;
                                        std::ostringstream oss;
                                        oss << s_windowId << "|1|" << id << "|" << x << "|" << y << "|" << w << "|" << h << "|" << text;
                                        std::string payload = oss.str();
                                        msg.data.assign(payload.begin(), payload.end());
                                        ipc::Bus::publish("gui.input", std::move(msg), false);
                                    };
                                    
                                    // Button layout (4 rows, 4 columns)
                                    // Row 0 (Display area - shown via MT_DrawText)
                                    
                                    // Row 1: 7, 8, 9, /
                                    addButton(7, 10, 60, 60, 60, "7");
                                    addButton(8, 80, 60, 60, 60, "8");
                                    addButton(9, 150, 60, 60, 60, "9");
                                    addButton(15, 220, 60, 60, 60, "/");  // Divide
                                    
                                    // Row 2: 4, 5, 6, *
                                    addButton(4, 10, 130, 60, 60, "4");
                                    addButton(5, 80, 130, 60, 60, "5");
                                    addButton(6, 150, 130, 60, 60, "6");
                                    addButton(14, 220, 130, 60, 60, "*");  // Multiply
                                    
                                    // Row 3: 1, 2, 3, -
                                    addButton(1, 10, 200, 60, 60, "1");
                                    addButton(2, 80, 200, 60, 60, "2");
                                    addButton(3, 150, 200, 60, 60, "3");
                                    addButton(13, 220, 200, 60, 60, "-");  // Subtract
                                    
                                    // Row 4: 0, ., =, +
                                    addButton(0, 10, 270, 60, 60, "0");
                                    addButton(11, 80, 270, 60, 60, ".");   // Decimal
                                    addButton(16, 150, 270, 60, 60, "=");  // Equals
                                    addButton(12, 220, 270, 60, 60, "+");  // Add
                                    
                                    // Row 5: C, CE (clear buttons at bottom)
                                    addButton(17, 10, 340, 130, 60, "C");   // Clear
                                    addButton(18, 150, 340, 130, 60, "CE");  // Clear Entry
                                    
                                    // Draw initial display
                                    updateDisplay();
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("Calculator: Failed to parse window ID: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        case MsgType::MT_Close: {
                            // Window closed
                            std::string payload(msg.data.begin(), msg.data.end());
                            if (!payload.empty()) {
                                try {
                                    uint64_t closedId = std::stoull(payload);
                                    if (closedId == s_windowId) {
                                        Logger::write(LogLevel::Info, "Calculator closing...");
                                        running = false;
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("Calculator: Failed to parse close ID: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        case MsgType::MT_InputKey: {
                            // Keyboard input
                            std::string payload(msg.data.begin(), msg.data.end());
                            size_t sep = payload.find('|');
                            if (sep != std::string::npos && sep > 0) {
                                try {
                                    int keyCode = std::stoi(payload.substr(0, sep));
                                    std::string action = payload.substr(sep + 1);
                                    
                                    // Key debouncing
                                    if (action == "down") {
                                        if (s_keyDown && keyCode == s_lastKeyCode) {
                                            break;
                                        }
                                        s_keyDown = true;
                                        s_lastKeyCode = keyCode;
                                        
                                        // Track shift for keyboard shortcuts
                                        if (keyCode == 16) {
                                            s_shiftPressed = true;
                                        } else {
                                            handleKeyPress(keyCode);
                                        }
                                    } else {
                                        s_keyDown = false;
                                        s_lastKeyCode = 0;
                                        if (keyCode == 16) {
                                            s_shiftPressed = false;
                                        }
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("Calculator: Failed to parse key code: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        case MsgType::MT_WidgetEvt: {
                            // Button click
                            std::string payload(msg.data.begin(), msg.data.end());
                            std::istringstream iss(payload);
                            std::string winIdStr, widgetIdStr, event, value;
                            std::getline(iss, winIdStr, '|');
                            std::getline(iss, widgetIdStr, '|');
                            std::getline(iss, event, '|');
                            std::getline(iss, value);
                            
                            if (!winIdStr.empty() && !widgetIdStr.empty()) {
                                try {
                                    uint64_t winId = std::stoull(winIdStr);
                                    if (winId == s_windowId && event == "click") {
                                        int widgetId = std::stoi(widgetIdStr);
                                        
                                        // Handle button clicks
                                        if (widgetId >= 0 && widgetId <= 9) {
                                            // Digit buttons (0-9)
                                            handleDigit(widgetId);
                                        } else if (widgetId == 11) {
                                            // Decimal point
                                            handleDecimal();
                                        } else if (widgetId == 12) {
                                            // Add
                                            handleOperation('+');
                                        } else if (widgetId == 13) {
                                            // Subtract
                                            handleOperation('-');
                                        } else if (widgetId == 14) {
                                            // Multiply
                                            handleOperation('*');
                                        } else if (widgetId == 15) {
                                            // Divide
                                            handleOperation('/');
                                        } else if (widgetId == 16) {
                                            // Equals
                                            handleEquals();
                                        } else if (widgetId == 17) {
                                            // Clear
                                            handleClear();
                                        } else if (widgetId == 18) {
                                            // Clear Entry
                                            handleClearEntry();
                                        }
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("Calculator: Failed to parse widget event: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        default:
                            break;
                    }
                }
            }
            
            Logger::write(LogLevel::Info, "Calculator stopped");
            return 0;
            
        } catch (const std::exception& e) {
            Logger::write(LogLevel::Error, std::string("Calculator EXCEPTION: ") + e.what());
            return -1;
        } catch (...) {
            Logger::write(LogLevel::Error, "Calculator UNKNOWN EXCEPTION");
            return -1;
        }
    }
    
    void Calculator::handleDigit(int digit) {
        if (s_newNumber) {
            s_display = std::to_string(digit);
            s_newNumber = false;
        } else {
            // Limit display length
            if (s_display.length() < 15) {
                if (s_display == "0") {
                    s_display = std::to_string(digit);
                } else {
                    s_display += std::to_string(digit);
                }
            }
        }
        
        s_currentValue = std::stod(s_display);
        updateDisplay();
    }
    
    void Calculator::handleOperation(char op) {
        // If there's a pending operation, perform it first
        if (s_operation != '\0' && !s_newNumber) {
            performOperation();
        }
        
        s_storedValue = s_currentValue;
        s_operation = op;
        s_newNumber = true;
        
        Logger::write(LogLevel::Info, std::string("Calculator: Operation ") + op);
    }
    
    void Calculator::handleEquals() {
        if (s_operation != '\0') {
            performOperation();
            s_operation = '\0';
        }
    }
    
    void Calculator::handleClear() {
        s_currentValue = 0.0;
        s_storedValue = 0.0;
        s_operation = '\0';
        s_display = "0";
        s_newNumber = true;
        updateDisplay();
        Logger::write(LogLevel::Info, "Calculator: Cleared");
    }
    
    void Calculator::handleClearEntry() {
        s_currentValue = 0.0;
        s_display = "0";
        s_newNumber = true;
        updateDisplay();
        Logger::write(LogLevel::Info, "Calculator: Entry cleared");
    }
    
    void Calculator::handleDecimal() {
        if (s_newNumber) {
            s_display = "0.";
            s_newNumber = false;
        } else {
            // Add decimal point if not already present
            if (s_display.find('.') == std::string::npos && s_display.length() < 15) {
                s_display += ".";
            }
        }
        updateDisplay();
    }
    
    void Calculator::handleBackspace() {
        if (!s_newNumber && s_display.length() > 1) {
            s_display = s_display.substr(0, s_display.length() - 1);
            s_currentValue = std::stod(s_display);
        } else {
            s_display = "0";
            s_currentValue = 0.0;
            s_newNumber = true;
        }
        updateDisplay();
    }
    
    void Calculator::performOperation() {
        double result = 0.0;
        
        switch (s_operation) {
            case '+':
                result = s_storedValue + s_currentValue;
                break;
            case '-':
                result = s_storedValue - s_currentValue;
                break;
            case '*':
                result = s_storedValue * s_currentValue;
                break;
            case '/':
                if (s_currentValue != 0.0) {
                    result = s_storedValue / s_currentValue;
                } else {
                    Logger::write(LogLevel::Warn, "Calculator: Division by zero");
                    handleClear();
                    s_display = "Error";
                    updateDisplay();
                    return;
                }
                break;
            default:
                result = s_currentValue;
                break;
        }
        
        s_currentValue = result;
        
        // Format result for display
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(6) << result;
        s_display = oss.str();
        
        // Remove trailing zeros and decimal point if not needed
        size_t dotPos = s_display.find('.');
        if (dotPos != std::string::npos) {
            // Remove trailing zeros
            while (s_display.length() > 0 && s_display.back() == '0') {
                s_display.pop_back();
            }
            // Remove decimal point if no decimals left
            if (s_display.length() > 0 && s_display.back() == '.') {
                s_display.pop_back();
            }
        }
        
        s_newNumber = true;
        updateDisplay();
        
        Logger::write(LogLevel::Info, std::string("Calculator: Result = ") + s_display);
    }
    
    void Calculator::updateDisplay() {
        const char* kGuiChanIn = "gui.input";
        
        ipc::Message msg;
        msg.type = (uint32_t)MsgType::MT_DrawText;
        
        std::ostringstream oss;
        oss << s_windowId << "|" << s_display;
        std::string payload = oss.str();
        msg.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
    }
    
    void Calculator::handleKeyPress(int keyCode) {
        // Number keys (0-9)
        if (keyCode >= 48 && keyCode <= 57) {
            handleDigit(keyCode - 48);
        }
        // Numpad keys (96-105)
        else if (keyCode >= 96 && keyCode <= 105) {
            handleDigit(keyCode - 96);
        }
        // Operations
        else if (keyCode == 187 || keyCode == 107) { // + or numpad +
            handleOperation('+');
        }
        else if (keyCode == 189 || keyCode == 109) { // - or numpad -
            handleOperation('-');
        }
        else if ((keyCode == 56 && s_shiftPressed) || keyCode == 106) { // * (Shift+8) or numpad *
            handleOperation('*');
        }
        else if (keyCode == 191 || keyCode == 111) { // / or numpad /
            handleOperation('/');
        }
        // Equals
        else if (keyCode == 13 || keyCode == 187) { // Enter or =
            handleEquals();
        }
        // Decimal
        else if (keyCode == 190 || keyCode == 110) { // . or numpad .
            handleDecimal();
        }
        // Clear
        else if (keyCode == 27) { // Escape
            handleClear();
        }
        // Backspace
        else if (keyCode == 8) {
            handleBackspace();
        }
        // Delete (acts as Clear Entry)
        else if (keyCode == 46) {
            handleClearEntry();
        }
    }
    
}} // namespace gxos::apps
