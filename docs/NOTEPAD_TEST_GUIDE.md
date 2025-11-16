# ?? Notepad Testing Guide

**Status:** Ready for Testing  
**Build:** ? Successful  
**Crash Fix:** ? Applied  

---

## ?? Testing Objectives

1. Validate crash fix is working
2. Verify all text editing features work
3. Confirm menu buttons respond correctly
4. Ensure clean shutdown with no memory leaks
5. Validate IPC message handling

---

## ?? Test Execution Plan

### Prerequisites
```bash
# Ensure compositor is built
# Current directory: guideXOSServer/
```

### Test Session

**Terminal 1: Start Compositor**
```bash
# Start the compositor
gui.start

# Expected Output:
# [INFO] Compositor window created
# [INFO] Compositor running...
```

**Terminal 2: Launch Notepad**
```bash
# Launch Notepad application
notepad

# Expected Output:
# [INFO] Notepad starting...
# [INFO] Notepad window created: <window_id>
```

---

## ? Test Cases

### Test 1: Window Creation
**Objective:** Verify window appears without crash

**Steps:**
1. Run `notepad` command
2. Observe compositor window

**Expected Results:**
- ? Notepad window appears
- ? Window has title "Untitled - Notepad"
- ? Welcome text is displayed
- ? Menu buttons visible (New, Open, Save, Save As)
- ? No crash occurs
- ? Logs show "Notepad window created"

**Pass Criteria:**
- Window appears within 1 second
- No exceptions in logs
- Window is fully rendered

---

### Test 2: Basic Text Input
**Objective:** Verify typing text works

**Steps:**
1. Click inside Notepad window to focus
2. Type: `Hello World`
3. Observe text appears

**Expected Results:**
- ? Each character appears as typed
- ? Cursor indicator (|) moves with each character
- ? Text is readable and correctly positioned
- ? Status bar shows "Line 10, Col 12"

**Pass Criteria:**
- All characters display correctly
- No lag or missing characters
- Status bar updates in real-time

---

### Test 3: Text Navigation
**Objective:** Verify arrow key navigation

**Steps:**
1. Type some text
2. Press Left Arrow key
3. Press Right Arrow key
4. Press Up Arrow key
5. Press Down Arrow key

**Expected Results:**
- ? Left Arrow moves cursor left
- ? Right Arrow moves cursor right
- ? Up Arrow moves cursor up one line
- ? Down Arrow moves cursor down one line
- ? Cursor indicator updates position
- ? Status bar shows correct line/column

**Pass Criteria:**
- Cursor moves smoothly
- No jumps or incorrect positioning
- Status bar matches cursor position

---

### Test 4: Enter Key
**Objective:** Verify new line creation

**Steps:**
1. Type some text
2. Press Enter key
3. Type more text on new line

**Expected Results:**
- ? New line is created
- ? Cursor moves to beginning of new line
- ? Previous line text is preserved
- ? Status bar shows new line number

**Pass Criteria:**
- Lines are split correctly
- No text loss
- Clean line breaks

---

### Test 5: Backspace Key
**Objective:** Verify character deletion

**Steps:**
1. Type: `Hello World`
2. Press Backspace 5 times
3. Observe text changes

**Expected Results:**
- ? Characters are deleted one at a time
- ? Text becomes `Hello W`, then `Hello `, etc.
- ? Cursor moves left with each deletion
- ? Status bar updates column number

**Pass Criteria:**
- Deletion works correctly
- No crashes
- Cursor position correct

---

### Test 6: Menu Button - New
**Objective:** Verify "New" button clears text

**Steps:**
1. Type some text
2. Click "New" button
3. Observe window content

**Expected Results:**
- ? All text is cleared
- ? Window shows empty document
- ? Cursor at position (1, 1)
- ? Title changes to "Untitled - Notepad"
- ? Status bar shows "Line 1, Col 1"

**Pass Criteria:**
- Text cleared immediately
- No remnants of old text
- Clean state reset

---

### Test 7: Modified State
**Objective:** Verify modified indicator works

**Steps:**
1. Type any character
2. Observe window title
3. Click "New" button
4. Observe title again

**Expected Results:**
- ? After typing, title shows "Untitled* - Notepad" (note asterisk)
- ? After "New", title shows "Untitled - Notepad" (no asterisk)
- ? Status bar shows "(Modified)" when text changed

**Pass Criteria:**
- Modified indicator appears/disappears correctly
- Title updates in real-time
- Status bar reflects state

---

### Test 8: Multi-Line Editing
**Objective:** Verify editing across multiple lines

**Steps:**
1. Type: `Line 1`
2. Press Enter
3. Type: `Line 2`
4. Press Enter
5. Type: `Line 3`
6. Use Up Arrow to go back to Line 2
7. Add text: ` - Modified`

**Expected Results:**
- ? All 3 lines are created
- ? Navigation works between lines
- ? Editing works on any line
- ? Text is preserved correctly
- ? Cursor position updates correctly

**Pass Criteria:**
- Multi-line editing works smoothly
- No line corruption
- Navigation is accurate

---

### Test 9: Window Close
**Objective:** Verify clean shutdown

**Steps:**
1. Click the X button on window
2. Observe logs
3. Check for memory leaks (if tools available)

**Expected Results:**
- ? Window closes immediately
- ? Logs show "Notepad closing..."
- ? No exception messages
- ? No crash on close
- ? Process exits cleanly

**Pass Criteria:**
- Clean shutdown
- No errors in logs
- No hanging processes

---

### Test 10: Logs Validation
**Objective:** Verify no unexpected errors

**Steps:**
1. After all tests, run: `log`
2. Review log output

**Expected Log Entries:**
```
[INFO] Notepad starting...
[INFO] Notepad window created: <id>
[INFO] Notepad: New file (if clicked New)
[INFO] Notepad closing...
```

**Should NOT See:**
```
[ERROR] Notepad EXCEPTION: ...
[ERROR] Failed to parse ...
[ERROR] Process crashed: notepad
```

**Pass Criteria:**
- Only INFO level logs for Notepad
- No ERROR or WARN messages
- Clean execution trace

---

## ?? Test Results Template

```markdown
## Notepad Test Results

**Date:** <date>
**Tester:** <name>
**Build:** <commit/version>

### Summary
- Total Tests: 10
- Passed: ___
- Failed: ___
- Blocked: ___

### Detailed Results

| Test | Status | Notes |
|------|--------|-------|
| 1. Window Creation | ? Pass ? Fail | |
| 2. Basic Text Input | ? Pass ? Fail | |
| 3. Text Navigation | ? Pass ? Fail | |
| 4. Enter Key | ? Pass ? Fail | |
| 5. Backspace Key | ? Pass ? Fail | |
| 6. Menu Button - New | ? Pass ? Fail | |
| 7. Modified State | ? Pass ? Fail | |
| 8. Multi-Line Editing | ? Pass ? Fail | |
| 9. Window Close | ? Pass ? Fail | |
| 10. Logs Validation | ? Pass ? Fail | |

### Issues Found
1. 
2. 
3. 

### Recommendations
1. 
2. 
3. 
```

---

## ?? Known Limitations

The following features are **not yet implemented** and should **not** cause test failures:

1. **File I/O**
   - Open button shows "not implemented" message
   - Save button shows "not implemented" message
   - Save As button shows "not implemented" message
   - This is expected and logged as INFO

2. **Copy/Paste**
   - No clipboard support yet
   - Ctrl+C, Ctrl+V do nothing
   - This is expected behavior

3. **Selection**
   - No text selection with mouse
   - No shift+arrow selection
   - This is expected behavior

4. **Advanced Editing**
   - No undo/redo
   - No find/replace
   - No word wrap
   - This is expected behavior

---

## ?? Critical Issues to Watch For

### Crash Indicators
- Window disappears suddenly
- "Process crashed: notepad" in logs
- "Notepad EXCEPTION:" in logs
- Compositor stops responding

### Performance Issues
- Slow typing response (>100ms delay)
- Cursor flicker
- Text rendering artifacts
- Memory leak (monitor with task manager)

### Functional Issues
- Lost characters when typing fast
- Incorrect cursor position
- Text corruption
- Wrong line/column in status bar

---

## ?? Debugging Tips

### If Notepad Crashes
1. Check logs with `log` command
2. Look for "EXCEPTION" messages
3. Note the exact payload that caused crash
4. Check if stoull/stoi exception occurred
5. Verify IPC message format

### If Text Editing Broken
1. Enable detailed logging in notepad.cpp
2. Log every key press received
3. Log cursor position changes
4. Check s_lines vector state
5. Verify redrawContent() is called

### If Window Doesn't Appear
1. Check compositor is running
2. Verify MT_Create message sent
3. Check MT_Create response received
4. Verify window ID parsing succeeded
5. Check compositor window list

---

## ? Test Completion Checklist

- [ ] All 10 tests executed
- [ ] Test results documented
- [ ] All tests passed OR issues documented
- [ ] Logs reviewed for errors
- [ ] Performance is acceptable
- [ ] No memory leaks detected
- [ ] Screenshots captured (if applicable)
- [ ] Results reported to team

---

## ?? Success Criteria

**Notepad is considered FULLY FUNCTIONAL when:**

? All 10 tests pass  
? No crashes occur  
? No exceptions in logs  
? Text editing is smooth and responsive  
? Menu buttons work correctly  
? Modified state tracking works  
? Window closes cleanly  
? Can be launched repeatedly without issues  

**When all criteria are met:** Notepad is ready for production use! ??

---

## ?? Next Steps After Testing

### If All Tests Pass ?
1. Document success
2. Update PHASE6_IMPLEMENTATION.md
3. Move to next app (Calculator)
4. Celebrate! ??

### If Some Tests Fail ??
1. Document failures
2. Prioritize fixes
3. Apply fixes
4. Re-test
5. Repeat until all pass

### If Critical Crash Occurs ?
1. Capture exact error message
2. Review exception handling code
3. Add more defensive checks
4. Test fix
5. Re-test full suite

---

**Happy Testing! ??**

Let's validate that our first application works perfectly!
