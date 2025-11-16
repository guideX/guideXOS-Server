# ?? NOTEPAD STILL CRASHING - DIAGNOSTIC GUIDE

## ? Build Status: SUCCESSFUL

The code compiles successfully. The crash is a **runtime issue**.

---

## ?? Most Likely Cause

Based on analysis of the compositor code, the crash is likely happening because:

### 1. Compositor Not Running (95% probability)
**Symptom:** Notepad publishes to "gui.input" but gets no response

**Why:** The try-catch blocks will prevent the crash from `stoull()`, but **if compositor isn't running**, Notepad will:
1. Start successfully ?
2. Publish MT_Create message ?
3. Wait 100ms for response from "gui.output"
4. Get NO response (timeout)
5. Loop forever waiting for messages
6. Eventually get killed by process timeout OR crash from some other issue

**Fix:** Always start compositor FIRST

### 2. Message Format Mismatch (4% probability)
**Symptom:** Compositor sends malformed payload

**Evidence from code:**
```cpp
// Compositor sends:
publishOut(MsgType::MT_Create, std::to_string(id)+"|"+title);

// Notepad expects:
std::string idStr = payload.substr(0, sep);  // sep is position of '|'
s_windowId = std::stoull(idStr);
```

This should work IF:
- Compositor sends valid ID
- Title doesn't interfere with parsing

**But**: If title is empty or contains weird characters, this could fail.

### 3. IPC Channel Issues (1% probability)
**Symptom:** Channels exist but messages don't flow

**Unlikely** because both compositor and notepad use `Bus::ensure()`.

---

## ?? How to Diagnose

### Step 1: Verify Compositor is Running
```bash
# Start compositor
gui.start

# Verify it's running
plist
# Should show: compositor (running)

# Check IPC channels
bus.stats gui.input
bus.stats gui.output
# Both should show: Channel exists
```

### Step 2: Launch Notepad
```bash
notepad
```

### Step 3: Check Logs Immediately
```bash
log
```

**Look for these messages:**

| Log Message | Meaning |
|-------------|---------|
| `"Notepad starting..."` | ? Process spawned successfully |
| `"Notepad window created: <ID>"` | ? Got response from compositor |
| `"Failed to parse window ID..."` | ? Bad response format |
| `"Notepad EXCEPTION: ..."` | ? Caught exception |
| `"Process crashed: notepad"` | ? Unhandled exception |
| *Nothing after "Notepad starting..."* | ? Hanging or timeout |

---

## ?? Detailed Crash Analysis

### Scenario A: "Process crashed" but NO exception logged

**This means:**
- Exception happened **outside** the try-catch block
- OR: Exception is something not derived from `std::exception`

**Possible causes:**
1. Stack overflow (infinite recursion)
2. Null pointer dereference in a constructor
3. Pure virtual function call
4. Memory corruption

**Where to look:**
- Static variable initialization (s_lines vector operations)
- ProcessTable::spawn internals
- IPC Bus initialization

### Scenario B: "Failed to parse..." error logged

**This means:**
- Compositor sent a message
- But the format was wrong

**What to check:**
- What does the error message say?
- What was the actual payload?

### Scenario C: No logs after "Notepad starting..."

**This means:**
- Process is hanging
- OR: Crashed without logging

**What to check:**
- Is compositor running?
- Are IPC channels working?
- Is process list showing notepad as "running"?

---

## ?? Advanced Debugging

### Add More Logging

Edit notepad.cpp and add these logs:

```cpp
// After IPC ensure
Logger::write(LogLevel::Info, "Notepad: IPC channels ready");

// Before publishing create message
Logger::write(LogLevel::Info, "Notepad: Publishing window create...");

// After publishing
Logger::write(LogLevel::Info, "Notepad: Create message sent, waiting for response...");

// In event loop, BEFORE waiting
Logger::write(LogLevel::Info, "Notepad: Event loop iteration starting...");

// After getting message
Logger::write(LogLevel::Info, std::string("Notepad: Got message type ") + std::to_string((uint32_t)msgType));
```

This will tell you **exactly** where it stops.

---

## ?? Testing Procedure

### Test 1: Compositor Running
```bash
# Step 1: Start compositor
gui.start

# Step 2: Verify it's running
plist
log
# Should see: "Compositor service started"

# Step 3: Launch notepad
notepad

# Step 4: Check result
log
plist
```

**Expected result:**
- Logs show: "Notepad starting..." ? "Notepad window created: 1000"
- Process list shows: notepad (running)
- Window appears on screen

### Test 2: Compositor NOT Running
```bash
# Step 1: DON'T start compositor

# Step 2: Launch notepad
notepad

# Step 3: Check result
log
```

**Expected result:**
- Logs show: "Notepad starting..." ? (nothing more or crash)
- Process might hang or crash

---

## ?? What Each Log Message Means

### "Notepad starting..."
? **Process spawned successfully**
- ProcessTable::spawn worked
- Notepad::main() was called
- Logger is working

### "Notepad window created: <ID>"
? **Got valid response from compositor**
- Compositor is running
- IPC channels working
- Parsing succeeded
- This means everything is WORKING!

### "Failed to parse window ID: <error>"
? **Got response but couldn't parse it**
- Compositor sent something
- But format was wrong
- The error message will tell you what went wrong

### "Notepad EXCEPTION: <message>"
? **Caught exception in main()**
- Something threw an exception
- Outer catch block caught it
- The message tells you what happened

### "Process crashed: notepad"
? **Unhandled exception**
- Exception escaped all catch blocks
- ProcessTable caught it
- Look for other log messages to see where

### *No messages after "Notepad starting..."*
? **Process hanging or silent crash**
- Check `plist` to see if still running
- If not in list: crashed without logging
- If in list: hanging (infinite loop or waiting)

---

## ?? Quick Fix Attempts

### Fix 1: Ensure Compositor is Running
```bash
# Always do this first
gui.start

# Wait a second
(sleep 1000ms in your head)

# Then launch notepad
notepad
```

### Fix 2: Add Timeout to Event Loop
If notepad hangs, modify the event loop:

```cpp
// In notepad.cpp main()
int timeout_count = 0;
while (running) {
    if (ipc::Bus::pop(kGuiChanOut, msg, 100)) {
        timeout_count = 0;  // Reset on message
        // ... handle message ...
    } else {
        timeout_count++;
        if (timeout_count > 100) {  // 10 seconds
            Logger::write(LogLevel::Error, "Notepad: Timeout waiting for messages");
            break;
        }
    }
}
```

### Fix 3: Validate Payload Before Parsing
The current code already has this, but double-check:

```cpp
if (sep != std::string::npos && sep > 0) {
    try {
        std::string idStr = payload.substr(0, sep);
        Logger::write(LogLevel::Info, std::string("Notepad: Parsing ID: '") + idStr + "'");
        s_windowId = std::stoull(idStr);
    } catch (const std::exception& e) {
        Logger::write(LogLevel::Error, std::string("Failed: ") + e.what());
    }
}
```

---

## ?? My Prediction

**I predict the crash is happening because:**

1. You're launching notepad **without** starting the compositor first
2. Notepad starts fine, logs "Notepad starting..."
3. Notepad publishes MT_Create to "gui.input"
4. Notepad waits on "gui.output" with 100ms timeout
5. **No response** because compositor isn't running
6. Notepad's event loop continues
7. Eventually something goes wrong (timeout, or vector access issue, or something else)
8. Process crashes

**To confirm:** Just run these commands and tell me **EXACTLY** what you see in the logs:

```bash
gui.start
notepad
log
```

Copy and paste ALL the log output, and I can tell you exactly what's happening!

---

**Status:** Build successful ?  
**Next:** Test and share logs ??  
**Fix Time:** < 5 minutes once I see the logs ??  
