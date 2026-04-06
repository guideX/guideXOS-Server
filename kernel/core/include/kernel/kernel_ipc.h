//
// guideXOS Kernel IPC System
//
// Provides inter-process communication primitives for kernel-mode apps.
// This is a lightweight IPC system for communicating between the desktop
// compositor and GUI applications in bare-metal/UEFI mode.
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_KERNEL_IPC_H
#define KERNEL_KERNEL_IPC_H

#include "kernel/types.h"

namespace kernel {
namespace ipc {

// ============================================================
// Constants
// ============================================================

static const int MAX_CHANNELS = 16;
static const int MAX_CHANNEL_NAME = 32;
static const int MAX_QUEUE_SIZE = 64;
static const int MAX_MESSAGE_DATA = 256;

// ============================================================
// Message Types
// ============================================================

enum class MessageType : uint32_t {
    None = 0,
    
    // Window management
    WindowCreate = 100,
    WindowDestroy,
    WindowShow,
    WindowHide,
    WindowMove,
    WindowResize,
    WindowFocus,
    WindowBlur,
    WindowMinimize,
    WindowMaximize,
    WindowRestore,
    WindowClose,
    WindowTitleChange,
    
    // Input events
    MouseMove = 200,
    MouseDown,
    MouseUp,
    MouseWheel,
    KeyDown,
    KeyUp,
    KeyChar,
    
    // Drawing commands
    DrawRect = 300,
    DrawText,
    DrawLine,
    DrawCircle,
    InvalidateRect,
    
    // Widget events
    WidgetClick = 400,
    WidgetValueChange,
    WidgetTextChange,
    
    // App lifecycle
    AppLaunch = 500,
    AppShutdown,
    AppSuspend,
    AppResume,
    
    // System
    SystemShutdown = 600,
    SystemRestart,
    SystemSleep
};

// ============================================================
// Message structure
// ============================================================

struct Message {
    MessageType type;
    uint32_t senderId;      // Source window/app ID
    uint32_t targetId;      // Destination window/app ID (0 = broadcast)
    uint32_t timestamp;
    
    // Payload union for different message types
    union {
        // Window events
        struct {
            int32_t x, y, w, h;
        } window;
        
        // Mouse events
        struct {
            int32_t x, y;
            uint8_t buttons;
            int8_t wheelDelta;
        } mouse;
        
        // Key events
        struct {
            uint32_t keyCode;
            char keyChar;
            bool shift;
            bool ctrl;
            bool alt;
        } key;
        
        // Widget events
        struct {
            int32_t widgetId;
            int32_t value;
        } widget;
        
        // Generic data
        uint8_t data[MAX_MESSAGE_DATA];
    } payload;
    
    Message() : type(MessageType::None), senderId(0), targetId(0), timestamp(0) {
        // Zero-initialize payload
        for (int i = 0; i < MAX_MESSAGE_DATA; i++) {
            payload.data[i] = 0;
        }
    }
};

// ============================================================
// Message Queue (circular buffer)
// ============================================================

struct MessageQueue {
    Message messages[MAX_QUEUE_SIZE];
    int head;
    int tail;
    int count;
    
    MessageQueue() : head(0), tail(0), count(0) {}
    
    bool isEmpty() const { return count == 0; }
    bool isFull() const { return count >= MAX_QUEUE_SIZE; }
    
    bool push(const Message& msg);
    bool pop(Message& outMsg);
    bool peek(Message& outMsg) const;
    void clear();
};

// ============================================================
// IPC Channel
// ============================================================

struct Channel {
    char name[MAX_CHANNEL_NAME];
    MessageQueue inputQueue;
    MessageQueue outputQueue;
    bool active;
    uint32_t subscriberId;  // Window/app that owns this channel
    
    Channel() : active(false), subscriberId(0) {
        name[0] = '\0';
    }
};

// ============================================================
// IPC Manager (singleton)
// ============================================================

class IpcManager {
public:
    // Initialize the IPC system
    static void init();
    
    // Create a named channel
    static int createChannel(const char* name, uint32_t subscriberId);
    
    // Destroy a channel
    static void destroyChannel(int channelId);
    
    // Find channel by name
    static int findChannel(const char* name);
    
    // Send a message to a channel
    static bool send(int channelId, const Message& msg);
    
    // Send to a specific window/app
    static bool sendTo(uint32_t targetId, const Message& msg);
    
    // Broadcast to all channels
    static void broadcast(const Message& msg);
    
    // Receive a message from a channel (non-blocking)
    static bool receive(int channelId, Message& outMsg);
    
    // Peek at next message without removing it
    static bool peek(int channelId, Message& outMsg);
    
    // Check if channel has pending messages
    static bool hasPendingMessages(int channelId);
    
    // Process all pending messages (called from main loop)
    static void processAll();
    
    // Get current tick count for timestamps
    static uint32_t getTicks();
    
    // Increment tick counter
    static void tick();
    
private:
    static Channel s_channels[MAX_CHANNELS];
    static int s_channelCount;
    static uint32_t s_tickCounter;
    static bool s_initialized;
};

// ============================================================
// Helper functions for creating common messages
// ============================================================

inline Message makeWindowCreateMsg(uint32_t windowId, int x, int y, int w, int h) {
    Message msg;
    msg.type = MessageType::WindowCreate;
    msg.senderId = windowId;
    msg.payload.window.x = x;
    msg.payload.window.y = y;
    msg.payload.window.w = w;
    msg.payload.window.h = h;
    return msg;
}

inline Message makeMouseMoveMsg(uint32_t windowId, int x, int y) {
    Message msg;
    msg.type = MessageType::MouseMove;
    msg.targetId = windowId;
    msg.payload.mouse.x = x;
    msg.payload.mouse.y = y;
    return msg;
}

inline Message makeMouseDownMsg(uint32_t windowId, int x, int y, uint8_t button) {
    Message msg;
    msg.type = MessageType::MouseDown;
    msg.targetId = windowId;
    msg.payload.mouse.x = x;
    msg.payload.mouse.y = y;
    msg.payload.mouse.buttons = button;
    return msg;
}

inline Message makeMouseUpMsg(uint32_t windowId, int x, int y, uint8_t button) {
    Message msg;
    msg.type = MessageType::MouseUp;
    msg.targetId = windowId;
    msg.payload.mouse.x = x;
    msg.payload.mouse.y = y;
    msg.payload.mouse.buttons = button;
    return msg;
}

inline Message makeKeyDownMsg(uint32_t windowId, uint32_t keyCode) {
    Message msg;
    msg.type = MessageType::KeyDown;
    msg.targetId = windowId;
    msg.payload.key.keyCode = keyCode;
    return msg;
}

inline Message makeKeyCharMsg(uint32_t windowId, char c) {
    Message msg;
    msg.type = MessageType::KeyChar;
    msg.targetId = windowId;
    msg.payload.key.keyChar = c;
    return msg;
}

inline Message makeWidgetClickMsg(uint32_t windowId, int widgetId) {
    Message msg;
    msg.type = MessageType::WidgetClick;
    msg.targetId = windowId;
    msg.payload.widget.widgetId = widgetId;
    return msg;
}

inline Message makeWindowCloseMsg(uint32_t windowId) {
    Message msg;
    msg.type = MessageType::WindowClose;
    msg.targetId = windowId;
    return msg;
}

inline Message makeWindowFocusMsg(uint32_t windowId) {
    Message msg;
    msg.type = MessageType::WindowFocus;
    msg.targetId = windowId;
    return msg;
}

} // namespace ipc
} // namespace kernel

#endif // KERNEL_KERNEL_IPC_H
