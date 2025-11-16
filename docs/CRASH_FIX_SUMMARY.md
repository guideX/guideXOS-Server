# ?? CRASH FIX - Quick Summary

## Problem Identified ?

**The crash is being caught** by ProcessTable::spawn's exception handler:
```cpp
catch(...) { Logger::write(LogLevel::Error, "Process crashed: "+proc->name); proc->exitCode = -1; }
```

This means Notepad IS launching, but **throwing an exception** during execution.

---

## ?? Most Likely Causes

### 1. std::stoull() Exception ?? HIGH PROBABILITY
**Location:** When parsing window ID from MT_Create response
```cpp
s_windowId = std::stoull(payload.substr(0, sep));
```

**Problem:** If payload is malformed or empty, `stoull` throws `std::invalid_argument`

**Fix:**
```cpp
try {
    s_windowId = std::stoull(payload.substr(0, sep));
} catch (const std::exception& e) {
    Logger::write(LogLevel::Error, std::string("Notepad: Failed to parse window ID: ") + e.what());
    return -1;
}
```

### 2. String substr() Out of Range ?? MEDIUM PROBABILITY
**Location:** Multiple places where we use `substr()`
```cpp
payload.substr(0, sep)  // Could throw if sep > payload.size()
```

**Fix:** Add bounds checking

### 3. Vector Out of Bounds ?? LOW PROBABILITY
**Location:** When accessing s_lines
```cpp
s_lines[s_cursorLine].insert(s_cursorCol, 1, ch);
```

**Fix:** Add range checking

---

## ?? Quick Fix - Add Exception Safety

Replace the entire `Notepad::main()` with try-catch:

```cpp
int Notepad::main(int argc, char** argv) {
    try {
        Logger::write(LogLevel::Info, "Notepad starting...");
        
        // ... existing code ...
        
        Logger::write(LogLevel::Info, "Notepad stopped");
        return 0;
    }
    catch (const std::exception& e) {
        Logger::write(LogLevel::Error, std::string("Notepad exception: ") + e.what());
        return -1;
    }
    catch (...) {
        Logger::write(LogLevel::Error, "Notepad unknown exception");
        return -1;
    }
}
```

This will give us a **specific error message** instead of just "Process crashed".

---

## ?? Recommended Actions

### Immediate (Do this NOW):
1. **Add try-catch wrapper** to Notepad::main()
2. **Test again** and check logs
3. **Look for error message** in logs after crash

### Next Steps:
1. **Add defensive parsing** for all `stoull()`, `stoi()`, `substr()` calls
2. **Add bounds checking** for vector access
3. **Test each IPC message type** separately

---

## ?? Testing Command

```bash
# Clear logs first
# (no clear command, just note the time)

# Start compositor
gui.start

# Launch notepad
notepad

# Check logs immediately
log

# Look for:
# "Notepad starting..."
# "Notepad exception: <message>"  ? THIS is what we need!
# "Process crashed: notepad"
```

---

## ?? Why This Matters

Without the try-catch wrapper, we only see:
```
"Process crashed: notepad"
```

With the wrapper, we'll see:
```
"Notepad exception: invalid stoul argument"
```

This tells us EXACTLY what went wrong!

---

## ?? Priority Fix

**File:** notepad.cpp  
**Change:** Wrap entire `main()` in try-catch  
**Time:** 2 minutes  
**Impact:** Will reveal exact crash cause  

Do this FIRST before anything else!
