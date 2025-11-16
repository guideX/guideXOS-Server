#pragma once
#include "process.h"
#include "ipc_bus.h"
#include <string>

namespace gxos { namespace apps {
    
    /// <summary>
    /// Calculator - Simple calculator application
    /// Features: Basic arithmetic operations (+, -, *, /), decimal support, keyboard input
    /// </summary>
    class Calculator {
    public:
        /// <summary>
        /// Launch a new Calculator instance
        /// </summary>
        /// <returns>Process ID of the launched Calculator</returns>
        static uint64_t Launch();
        
    private:
        // Main entry point for Calculator process
        static int main(int argc, char** argv);
        
        // Button handlers
        static void handleDigit(int digit);
        static void handleOperation(char op);
        static void handleEquals();
        static void handleClear();
        static void handleClearEntry();
        static void handleDecimal();
        static void handleBackspace();
        
        // Calculation
        static void performOperation();
        
        // UI update
        static void updateDisplay();
        
        // Keyboard helpers
        static void handleKeyPress(int keyCode);
        
        // State
        static uint64_t s_windowId;
        static double s_currentValue;      // Currently displayed number
        static double s_storedValue;       // Stored operand for operation
        static char s_operation;           // Current operation (+, -, *, /)
        static std::string s_display;      // Display string
        static bool s_newNumber;           // True if next digit starts a new number
        static bool s_shiftPressed;        // For keyboard shortcuts
        static int s_lastKeyCode;          // For key debouncing
        static bool s_keyDown;             // For key debouncing
    };
    
}} // namespace gxos::apps
