//
// guideXOS Kernel IPC System Implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/kernel_ipc.h"

namespace kernel {
namespace ipc {

// ============================================================
// Static member initialization
// ============================================================

Channel IpcManager::s_channels[MAX_CHANNELS];
int IpcManager::s_channelCount = 0;
uint32_t IpcManager::s_tickCounter = 0;
bool IpcManager::s_initialized = false;

// ============================================================
// MessageQueue implementation
// ============================================================

bool MessageQueue::push(const Message& msg) {
    if (isFull()) {
        return false;
    }
    
    messages[tail] = msg;
    tail = (tail + 1) % MAX_QUEUE_SIZE;
    count++;
    return true;
}

bool MessageQueue::pop(Message& outMsg) {
    if (isEmpty()) {
        return false;
    }
    
    outMsg = messages[head];
    head = (head + 1) % MAX_QUEUE_SIZE;
    count--;
    return true;
}

bool MessageQueue::peek(Message& outMsg) const {
    if (isEmpty()) {
        return false;
    }
    
    outMsg = messages[head];
    return true;
}

void MessageQueue::clear() {
    head = 0;
    tail = 0;
    count = 0;
}

// ============================================================
// IpcManager implementation
// ============================================================

void IpcManager::init() {
    if (s_initialized) {
        return;
    }
    
    // Clear all channels
    for (int i = 0; i < MAX_CHANNELS; i++) {
        s_channels[i].active = false;
        s_channels[i].name[0] = '\0';
        s_channels[i].subscriberId = 0;
        s_channels[i].inputQueue.clear();
        s_channels[i].outputQueue.clear();
    }
    
    s_channelCount = 0;
    s_tickCounter = 0;
    s_initialized = true;
}

int IpcManager::createChannel(const char* name, uint32_t subscriberId) {
    if (!s_initialized || !name || name[0] == '\0') {
        return -1;
    }
    
    // Check if channel already exists
    int existing = findChannel(name);
    if (existing >= 0) {
        return existing;
    }
    
    // Find empty slot
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (!s_channels[i].active) {
            // Copy name
            int j = 0;
            while (name[j] && j < MAX_CHANNEL_NAME - 1) {
                s_channels[i].name[j] = name[j];
                j++;
            }
            s_channels[i].name[j] = '\0';
            
            s_channels[i].active = true;
            s_channels[i].subscriberId = subscriberId;
            s_channels[i].inputQueue.clear();
            s_channels[i].outputQueue.clear();
            s_channelCount++;
            return i;
        }
    }
    
    return -1;  // No available slot
}

void IpcManager::destroyChannel(int channelId) {
    if (!s_initialized || channelId < 0 || channelId >= MAX_CHANNELS) {
        return;
    }
    
    if (s_channels[channelId].active) {
        s_channels[channelId].active = false;
        s_channels[channelId].name[0] = '\0';
        s_channels[channelId].subscriberId = 0;
        s_channels[channelId].inputQueue.clear();
        s_channels[channelId].outputQueue.clear();
        s_channelCount--;
    }
}

int IpcManager::findChannel(const char* name) {
    if (!s_initialized || !name) {
        return -1;
    }
    
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (s_channels[i].active) {
            // Compare names
            const char* a = s_channels[i].name;
            const char* b = name;
            bool match = true;
            while (*a && *b) {
                if (*a != *b) {
                    match = false;
                    break;
                }
                a++;
                b++;
            }
            if (match && *a == '\0' && *b == '\0') {
                return i;
            }
        }
    }
    
    return -1;
}

bool IpcManager::send(int channelId, const Message& msg) {
    if (!s_initialized || channelId < 0 || channelId >= MAX_CHANNELS) {
        return false;
    }
    
    if (!s_channels[channelId].active) {
        return false;
    }
    
    Message msgWithTimestamp = msg;
    msgWithTimestamp.timestamp = s_tickCounter;
    
    return s_channels[channelId].inputQueue.push(msgWithTimestamp);
}

bool IpcManager::sendTo(uint32_t targetId, const Message& msg) {
    if (!s_initialized) {
        return false;
    }
    
    // Find channel with matching subscriber ID
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (s_channels[i].active && s_channels[i].subscriberId == targetId) {
            return send(i, msg);
        }
    }
    
    return false;
}

void IpcManager::broadcast(const Message& msg) {
    if (!s_initialized) {
        return;
    }
    
    Message msgWithTimestamp = msg;
    msgWithTimestamp.timestamp = s_tickCounter;
    
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (s_channels[i].active) {
            s_channels[i].inputQueue.push(msgWithTimestamp);
        }
    }
}

bool IpcManager::receive(int channelId, Message& outMsg) {
    if (!s_initialized || channelId < 0 || channelId >= MAX_CHANNELS) {
        return false;
    }
    
    if (!s_channels[channelId].active) {
        return false;
    }
    
    return s_channels[channelId].inputQueue.pop(outMsg);
}

bool IpcManager::peek(int channelId, Message& outMsg) {
    if (!s_initialized || channelId < 0 || channelId >= MAX_CHANNELS) {
        return false;
    }
    
    if (!s_channels[channelId].active) {
        return false;
    }
    
    return s_channels[channelId].inputQueue.peek(outMsg);
}

bool IpcManager::hasPendingMessages(int channelId) {
    if (!s_initialized || channelId < 0 || channelId >= MAX_CHANNELS) {
        return false;
    }
    
    if (!s_channels[channelId].active) {
        return false;
    }
    
    return !s_channels[channelId].inputQueue.isEmpty();
}

void IpcManager::processAll() {
    // This function can be extended to handle automatic message routing
    // For now, messages are pulled by individual apps
}

uint32_t IpcManager::getTicks() {
    return s_tickCounter;
}

void IpcManager::tick() {
    s_tickCounter++;
}

} // namespace ipc
} // namespace kernel
