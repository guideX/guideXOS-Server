# ?? Phase 7 Quick Start Guide

**Welcome to Phase 7: Testing & Quality!**

This guide will help you start testing immediately.

---

## ? Quick Start (5 Minutes)

### Step 1: Ensure Clean Build
```sh
# Rebuild project
msbuild /t:Rebuild

# Or in Visual Studio:
# Build ? Rebuild Solution
```

**Expected:** ? Build succeeds with no errors

---

### Step 2: Launch the Server
```sh
# Run guideXOSServer.exe
# Or in Visual Studio: F5 (Debug) or Ctrl+F5 (Run)
```

**Expected:** Console appears with welcome message

---

### Step 3: Start Compositor
```sh
gui.start
```

**Expected:** 
```
Compositor pid=XXX (proto=X)
```

---

### Step 4: Quick Smoke Test (All Apps)
```sh
# Test each app
notepad
calculator
console
files
clock
taskmgr
```

**Expected:** All 6 apps launch without crashes

---

### Step 5: Check Logs
```sh
log
```

**Expected:** No error messages, only Info logs

---

### Step 6: Check Memory
```sh
mem
```

**Expected:** Memory usage reasonable (< 5 MB for all apps)

---

## ?? Testing Workflow

### For Each App:

1. **Launch**
```sh
[app_name]
```

2. **Test Basic Features**
   - Use the app as intended
   - Try keyboard shortcuts
   - Test edge cases

3. **Check Logs**
```sh
log
```

4. **Check Memory**
```sh
mem
pbytes  # Per-process breakdown
```

5. **Document Results**
   - Update `Docs/TestResults/Session1_SmokeTests.md`
   - Note any bugs found
   - Record memory usage

---

## ?? Today's Goals

### Minimal (1 hour)
- [ ] Launch all 6 apps
- [ ] Verify no immediate crashes
- [ ] Check basic functionality
- [ ] Document results

### Recommended (2 hours)
- [ ] Complete full smoke test
- [ ] Test key features in each app
- [ ] Find and document 3-5 bugs
- [ ] Record memory usage

### Comprehensive (4 hours)
- [ ] Detailed app-by-app testing
- [ ] Integration scenarios
- [ ] Performance testing
- [ ] Bug fixes and retests

---

## ?? Testing Checklist

### Notepad
```sh
notepad
# Type text
# Save file: Ctrl+S
# New file: Ctrl+N
# Close window
```

### Calculator
```sh
calculator
# Click: 5 + 3 =
# Result should be 8
# Try: 10 / 0
# Should show "Error"
# Close window
```

### Console
```sh
console
# Type: help
# Press Enter
# Press Up arrow (recalls command)
# Close window
```

### File Explorer
```sh
files
# Navigate with arrows
# Press Enter on folder
# Click "New Folder"
# Click "Refresh"
# Close window
```

### Clock
```sh
clock
# Observe time updates
# Wait 5 seconds
# Verify seconds increment
# Close window
```

### Task Manager
```sh
taskmgr
# View process list
# Press F5 to refresh
# Wait 2 seconds (auto-refresh)
# Close window
```

---

## ?? Recording Results

### Memory Tracking
```sh
# Before launching apps
mem

# Launch all 6 apps
notepad
calculator
console
files
clock
taskmgr

# Check memory again
mem

# Per-process breakdown
pbytes

# Compare initial vs final
```

### Log Analysis
```sh
# View all logs
log

# Look for:
# - Error messages (should be none)
# - Warning messages (investigate)
# - Exception messages (critical!)
# - Successful operations (good!)
```

---

## ?? Reporting Bugs

### Template
```markdown
### Bug #XXX: [Short Description]

**App:** [Notepad/Calculator/etc.]  
**Severity:** Critical/High/Medium/Low  
**Reproducible:** Always/Sometimes/Rare

**Steps:**
1. Do this
2. Then this
3. Bug occurs

**Expected:** [What should happen]  
**Actual:** [What actually happens]  
**Logs:** [Paste relevant logs]
```

### Where to Report
- Add to `Docs/TestResults/Session1_SmokeTests.md`
- Create new file `Docs/TestResults/BugXXX_Description.md` for detailed bugs
- Update `Docs/PHASE7_PLAN.md` with bug count

---

## ?? Visual Inspection

### What to Look For
- [ ] Windows appear correctly
- [ ] Text is readable
- [ ] Buttons are clickable
- [ ] Cursor is visible
- [ ] Status bars update
- [ ] Titles are correct
- [ ] No visual glitches

---

## ? Performance Notes

### What to Monitor
- [ ] App launch speed (< 1 second ideal)
- [ ] Input responsiveness (immediate)
- [ ] Auto-refresh timing (Clock: 1s, Task Manager: 2s)
- [ ] Memory usage (< 1 MB per app ideal)
- [ ] No lag or stuttering

---

## ?? Troubleshooting

### App Won't Launch
```sh
# Check logs
log

# Check if compositor running
plist

# Restart compositor
gui.start
```

### Crash or Hang
```sh
# Check logs for exception
log

# Check process list
plist

# Kill hung process
# (In Task Manager or via CLI if implemented)
```

### Memory Issues
```sh
# Check usage
mem

# Per-process breakdown
pbytes

# Look for leaks (memory not released after app closes)
```

---

## ?? Documentation

### Files to Update
1. `Docs/TestResults/Session1_SmokeTests.md` - Test results
2. `Docs/PHASE7_PLAN.md` - Overall progress
3. `Docs/TestResults/BugXXX.md` - Individual bug reports

---

## ?? Success Criteria

### Session 1 Complete When:
- [ ] All 6 apps tested
- [ ] Results documented
- [ ] Memory usage recorded
- [ ] Bugs logged (even if 0 bugs!)
- [ ] Next steps identified

---

## ?? Next Steps After Session 1

### If No Bugs Found
1. ? Move to detailed testing
2. ? Try integration scenarios
3. ? Start performance testing

### If Bugs Found
1. ?? Categorize by severity
2. ?? Fix critical bugs first
3. ?? Retest after fixes
4. ?? Then continue testing

---

## ?? Tips for Effective Testing

### Do's ?
- Test systematically
- Document everything
- Take breaks
- Test edge cases
- Be thorough

### Don'ts ?
- Skip steps
- Assume things work
- Ignore warnings
- Rush through tests
- Forget to document

---

## ?? Ready to Start!

**You have everything you need:**
- ? Test plans
- ? Templates
- ? Commands
- ? Documentation structure
- ? 6 amazing apps to test!

**Let's make them bulletproof!** ??

---

**Time to begin:** ?? **Start Testing Now!**  
**First file:** `Docs/TestResults/Session1_SmokeTests.md`  
**Estimated time:** 2 hours  

**Good luck and happy bug hunting!** ?????
