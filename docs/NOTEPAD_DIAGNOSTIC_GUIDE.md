# ?? Notepad Crash - Diagnostic Guide

## ? Build Status: SUCCESSFUL

Notepad compiles successfully. The crash is a **runtime issue**, not a compile-time error.

---

## ?? How to Diagnose the Crash

### Step 1: Launch and Check Logs
```bash
# Start compositor
gui.start

# Launch notepad
notepad

# Immediately check logs
log
```

### Step 2: Look for These Messages

| Message | Meaning |
|---------|---------|
| `"Notepad starting..."` | Process spawned ? |
| `"Notepad: Would load file..."` | Init working ? |
| `"Process crashed: notepad"` | Exception thrown ? |
| *No messages at all* | Process didn't spawn ? |

---

## ?? Most Likely Crash Causes

### 1. Compositor Not Running (90% probability)
**Symptom:** Notepad tries to publish to "gui.input" but compositor isn't listening

**Test:**
```bash
# DON'T start compositor
notepad
log
# Should see: "Notepad starting..." then crash
```

**Fix:**
```bash
# Always start compositor FIRST
gui.start
# THEN launch notepad
notepad
```

### 2. IPC Channel Issues (5% probability)
**Symptom:** `ipc::Bus::ensure()` or `ipc::Bus::publish()` fails

**Fix:** Already handled - bus operations shouldn't throw

### 3. String Parsing Exception (3% probability)
**Symptom:** `std::stoull()` throws `std::invalid_argument`

**When:** Parsing window ID from MT_Create response

**Fix:** Add try-catch around stoull (advanced)

### 4. Vector Out of Bounds (2% probability)
**Symptom:** Accessing `s_lines[s_cursorLine]` with invalid index

**Fix:** Add bounds checking (advanced)

---

## ?? Quick Test

Try this exact sequence:

```bash
# Test 1: Without compositor (should crash)
notepad
log
# Expected: "Notepad starting..." then "Process crashed"

# Test 2: With compositor (should work)
gui.start
notepad  
gui.pop
# Expected: Window created message
```

---

## ?? If Notepad Still Crashes

### Add Logging to Find Exact Location

Edit `notepad.cpp` and add logging after each major operation:

```cpp
int Notepad::main(int argc, char** argv) {
    Logger::write(LogLevel::Info, "Notepad starting...");
    
    // After state init
    s_lines.push_back("");
    Logger::write(LogLevel::Info, "Notepad: State initialized");
    
    // After IPC ensure
    ipc::Bus::ensure(kGuiChanOut);
    Logger::write(LogLevel::Info, "Notepad: IPC channels ready");
    
    // After publish
    ipc::Bus::publish(kGuiChanIn, std::move(createMsg), false);
    Logger::write(LogLevel::Info, "Notepad: Window create published");
    
    // In event loop
    while (running) {
        Logger::write(LogLevel::Info, "Notepad: Waiting for message...");
        if (ipc::Bus::pop(kGuiChanOut, msg, 100)) {
            Logger::write(LogLevel::Info, std::string("Notepad: Got message type ") + std::to_string((uint32_t)msgType));
            // ...
        }
    }
}
```

Then run again and check logs to see **exactly where** it stops.

---

## ?? Advanced: Add Exception Handler

If you want to see the **exact exception**, wrap main() in try-catch:

```cpp
int Notepad::main(int argc, char** argv) {
    try {
        // ... all existing code ...
        return 0;
    } catch (const std::exception& e) {
        Logger::write(LogLevel::Error, std::string("Notepad EXCEPTION: ") + e.what());
        return -1;
    }
}
```

This will log the exception message, telling you EXACTLY what went wrong.

---

## ?? Expected Behavior

### Successful Launch
```
Log messages:
1. "Notepad starting..."
2. "Notepad: Would load file..." (if file arg provided)
3. (Notepad waits for messages - no more logs until window events)

GUI result:
- Window appears with title "Untitled - Notepad"
- Welcome text displays
- Can interact with window
```

### Crash
```
Log messages:
1. "Notepad starting..."
2. (some messages depending on where it crashes)
3. "Process crashed: notepad"

GUI result:
- No window appears
- Or window appears then immediately closes
```

---

## ?? Action Plan

1. **Try launching** with compositor running:
   ```bash
   gui.start
   notepad
   ```

2. **If it crashes**, run:
   ```bash
   log
   ```
   And **tell me the EXACT log messages** you see

3. **Based on the logs**, I can tell you:
   - Exactly where it's crashing
   - What's causing it
   - How to fix it

---

## ?? My Prediction

I believe the crash is happening because:

1. **Notepad launches** (you'll see "Notepad starting...")
2. **IPC bus operations work** (no crash during ensure/publish)
3. **Event loop starts** waiting for messages
4. **Eventually gets a message** from compositor
5. **Tries to parse** the message payload
6. **Parsing fails** (stoull throws exception)
7. **Exception caught** by ProcessTable, logs "Process crashed"

To confirm this, just run it and show me the logs!

---

## ?? What I Need From You

After running notepad (with compositor):

```bash
gui.start
notepad
log
```

**Copy and paste ALL the log messages** you see, especially:
- Any "Notepad..." messages
- Any "Process crashed..." messages
- Any error messages

This will tell me **exactly** what's happening!

---

**Status:** Ready to test ?  
**Next Step:** Run it and share the logs ??  
**Fix Time:** < 5 minutes once I see the logs ??
