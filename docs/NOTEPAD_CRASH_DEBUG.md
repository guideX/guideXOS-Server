# ?? Notepad Crash - Debugging Guide

## ?? Issue: Notepad Crashes on Launch

**Status:** Under investigation  
**Severity:** High - Blocks Phase 6 progress  

---

## ?? Symptoms

- Crash occurs when launching Notepad via `notepad` command
- May crash during process spawn or IPC initialization
- Crash location: Unknown (need stack trace)

---

## ?? Potential Causes

### 1. IPC Channel Issues ??
**Problem:** Notepad tries to use IPC bus before it's properly initialized

**Evidence:**
- Calling `ipc::Bus::pop()` without prior subscription
- No delay between spawn and IPC operations

**Fix Applied:**
```cpp
// Added small delay to ensure IPC bus is ready
std::this_thread::sleep_for(std::chrono::milliseconds(50));
```

### 2. Process Spawning Issues ??
**Problem:** Static function pointer might not work with ProcessTable::spawn

**Evidence:**
- Using `Notepad::main` as function pointer
- Static member functions have different calling conventions

**Potential Fix:**
- Make main() a free function instead of static member
- Or use a lambda wrapper

### 3. Static Variable Initialization ??
**Problem:** Static members might not be initialized in spawned process

**Evidence:**
```cpp
static uint64_t s_windowId = 0;
static std::string s_filePath = "";
static std::vector<std::string> s_lines;
```

**Note:** Static initialization should work, but might have issues in process context

### 4. String Operations on Empty Vector ??
**Problem:** Possible out-of-bounds access

**Evidence:**
```cpp
s_cursorLine = s_lines.size();  // = 10
s_lines.push_back("");  // Now size = 11
// But later: s_lines[s_cursorLine]  // Accesses index 10 (valid)
```

**Status:** Probably OK, but worth checking

---

## ?? Debugging Steps

### Step 1: Check if Process Spawns
```bash
# In server
gui.start
notepad

# Check process list
plist
# Should show notepad process

# Check logs
log
# Should show "Notepad starting..."
```

**If process doesn't spawn:** Issue is in `ProcessTable::spawn()`

**If process spawns but crashes:** Issue is in `Notepad::main()`

### Step 2: Add Logging
Already added logging at key points:
```cpp
Logger::write(LogLevel::Info, "Notepad starting...");
Logger::write(LogLevel::Info, "Notepad: Publishing window create message...");
Logger::write(LogLevel::Info, "Notepad: Received message type " + std::to_string((uint32_t)msgType));
```

**Check logs after crash** to see where it fails

### Step 3: Check IPC Bus State
```bash
# Before launching notepad
bus.stats gui.input
bus.stats gui.output

# After crash
bus.stats gui.input
bus.stats gui.output
```

### Step 4: Simplify Notepad
Create minimal version that just:
1. Logs startup
2. Creates window
3. Waits for close
4. No text editing

---

## ?? Fixes Applied

### Fix 1: Added IPC Initialization Delay
```cpp
// Small delay to ensure IPC bus is ready
std::this_thread::sleep_for(std::chrono::milliseconds(50));
```

**Rationale:** Give IPC bus time to initialize before first operation

### Fix 2: Added Diagnostic Logging
```cpp
Logger::write(LogLevel::Info, "Notepad: Publishing window create message...");
Logger::write(LogLevel::Info, "Notepad: Received message type " + std::to_string((uint32_t)msgType));
```

**Rationale:** Track execution flow to pinpoint crash location

### Fix 3: Added Missing Includes
```cpp
#include <thread>
#include <chrono>
```

**Rationale:** Required for std::this_thread::sleep_for

---

## ?? Next Steps

### Immediate Actions
1. **Test with logging** - Run `notepad` and check logs
2. **Get stack trace** - If using debugger, capture crash location
3. **Simplify code** - Create minimal test version

### If Crash Persists
1. **Check process.cpp** - Verify ProcessTable::spawn works with static methods
2. **Test IPC separately** - Create simple IPC test app
3. **Review compositor** - Check if it's ready for app windows

### Alternative Approaches
1. **Use free function** instead of static member:
```cpp
static int notepad_main(int argc, char** argv);
ProcessSpec spec{"notepad", notepad_main};
```

2. **Use lambda wrapper**:
```cpp
ProcessSpec spec{"notepad", [](int argc, char** argv) {
    return Notepad::main(argc, argv);
}};
```

3. **Separate process entry point**:
```cpp
// In notepad.cpp (global scope)
extern "C" int notepad_entry(int argc, char** argv) {
    return gxos::apps::Notepad::main(argc, argv);
}
```

---

## ?? Error Messages to Look For

### Common Crash Indicators
- **Access violation** - Null pointer dereference
- **Stack overflow** - Infinite recursion
- **Heap corruption** - Memory management issue
- **Pure virtual function call** - VTable issue

### IPC Errors
- "Channel not found"
- "Bus not initialized"
- "Subscription failed"
- "Publish failed"

### Process Errors
- "Process spawn failed"
- "Invalid function pointer"
- "Process table full"

---

## ?? Minimal Test Version

Create `notepad_test.cpp`:
```cpp
#include "notepad.h"
#include "logger.h"
#include <thread>
#include <chrono>

namespace gxos { namespace apps {
    
    uint64_t Notepad::LaunchTest() {
        ProcessSpec spec{"notepad_test", [](int argc, char** argv) -> int {
            Logger::write(LogLevel::Info, "TEST: Notepad test started");
            std::this_thread::sleep_for(std::chrono::seconds(5));
            Logger::write(LogLevel::Info, "TEST: Notepad test ending");
            return 0;
        }};
        return ProcessTable::spawn(spec, {"notepad_test"});
    }
    
}} // namespace
```

**Test:**
```bash
# Add LaunchTest() to desktop_service
desktop.launch NotepadTest
log
# Should see: "TEST: Notepad test started" and "TEST: Notepad test ending"
```

---

## ?? Insights

### Process Spawning in C++
- Function pointers must match signature exactly
- Static member functions have implicit `this` (NO - they don't!)
- Lambda captures can cause issues with spawn

### IPC Bus Behavior
- Channels must exist before publishing
- `Bus::ensure()` creates channel if needed
- `Bus::pop()` blocks for specified timeout
- No subscription needed for `Bus::pop()` - it reads from channel directly

### Static Variables
- Initialized once per program, not per process
- Each process gets its own copy of static data
- Thread-safe initialization (C++11+)

---

## ?? Most Likely Cause

Based on the code analysis, **the crash is likely happening because:**

1. **Process spawning might be failing** - Check if `ProcessTable::spawn()` supports function pointers from static methods
2. **IPC bus not ready** - First IPC operation might crash if bus isn't initialized
3. **Compositor not ready** - Window create might fail if compositor isn't running

**Recommended Fix:** Add more defensive checks:
```cpp
// Check if compositor is running
if (!ipc::Bus::exists("gui.input")) {
    Logger::write(LogLevel::Error, "Notepad: GUI not available");
    return -1;
}
```

---

## ?? Progress Tracker

- [x] Added logging
- [x] Added delay before IPC
- [x] Added missing includes
- [ ] Test with minimal version
- [ ] Get stack trace
- [ ] Identify root cause
- [ ] Apply permanent fix

---

**Status:** Build successful ?  
**Next:** Test and gather crash logs ??  
**Priority:** High ??  
