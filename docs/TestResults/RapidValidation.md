# ?? Phase 7: Rapid Validation Session

**Goal:** Quick validation that all 6 apps launch and work  
**Time:** 1-2 hours  
**Date:** [Today's Date]

---

## ?? Quick Test Plan

This is a **streamlined** version of the full test plan for rapid validation.

---

## ? Pre-Flight Check

**Before starting:**
- [ ] Clean build successful
- [ ] guideXOSServer.exe running
- [ ] Ready to take notes

---

## ?? Test Sequence

### Step 1: Launch Compositor
```sh
gui.start
```

**Expected:** 
```
Compositor pid=XXX (proto=X)
```

**Result:** ? Pass / ? Fail

---

### Step 2: Test Notepad (5 minutes)

```sh
notepad
```

**Quick Tests:**
1. Window appears
2. Type "Hello World"
3. Press Enter (new line)
4. Press Ctrl+S (should prompt Save As)
5. Press Ctrl+N (should clear to new file)
6. Close window

**Result:** ? Pass / ? Fail  
**Notes:** _______________________

---

### Step 3: Test Calculator (3 minutes)

```sh
calculator
```

**Quick Tests:**
1. Window appears
2. Click: 5 + 3 = (should show 8)
3. Click: 10 / 0 = (should show "Error")
4. Press C (should clear)
5. Close window

**Result:** ? Pass / ? Fail  
**Notes:** _______________________

---

### Step 4: Test Console (3 minutes)

```sh
console
```

**Quick Tests:**
1. Window appears
2. Welcome messages visible
3. Type "test" and press Enter
4. Press Up arrow (should recall "test")
5. Press Escape (should clear input)
6. Close window

**Result:** ? Pass / ? Fail  
**Notes:** _______________________

---

### Step 5: Test File Explorer (5 minutes)

```sh
files
```

**Quick Tests:**
1. Window appears
2. Files/folders listed
3. Arrow Down (selection moves)
4. Click "New Folder" (folder created)
5. Click "Refresh" (list updates)
6. Close window

**Result:** ? Pass / ? Fail  
**Notes:** _______________________

---

### Step 6: Test Clock (2 minutes)

```sh
clock
```

**Quick Tests:**
1. Window appears
2. Time displays (HH:MM:SS)
3. Date displays
4. Wait 5 seconds (time should update)
5. Close window

**Result:** ? Pass / ? Fail  
**Notes:** _______________________

---

### Step 7: Test Task Manager (5 minutes)

```sh
taskmgr
```

**Quick Tests:**
1. Window appears
2. Process list visible
3. System stats shown
4. Press F5 (should refresh)
5. Wait 2+ seconds (should auto-refresh)
6. Close window

**Result:** ? Pass / ? Fail  
**Notes:** _______________________

---

## ?? System Checks

### Check Logs
```sh
log
```

**Look for:**
- [ ] No ERROR messages
- [ ] No EXCEPTION messages
- [ ] All apps started successfully
- [ ] All apps closed cleanly

**Issues Found:** _______________________

---

### Check Memory
```sh
mem
```

**Record:**
- Total Used: _______ KB
- Peak: _______ KB

```sh
pbytes
```

**Per-process breakdown:**
```
(Paste output here)
```

---

## ?? Bugs Found

### Bug #1 (if any)
**App:** _______  
**Issue:** _______  
**Severity:** Critical / High / Medium / Low

### Bug #2 (if any)
**App:** _______  
**Issue:** _______  
**Severity:** Critical / High / Medium / Low

---

## ?? Results Summary

### Test Results
- **Apps Tested:** 6
- **Apps Passed:** ___
- **Apps Failed:** ___
- **Bugs Found:** ___

### Overall Status
- [ ] ? **All Passed** - Move to Phase 8!
- [ ] ?? **Minor Issues** - Fix and retest
- [ ] ? **Critical Issues** - Debug and fix

---

## ?? Next Actions

### If All Passed ?
1. **Celebrate!** ??
2. Document success
3. Plan Phase 8 (Polish & UX)
4. Optional: Build Paint

### If Issues Found ??
1. Document all bugs
2. Prioritize by severity
3. Fix critical bugs first
4. Retest
5. Then move forward

---

## ?? Quick Decisions

### Ready for Phase 8?
- [ ] Yes - All apps work great
- [ ] Almost - Minor bugs to fix
- [ ] Not yet - Need more testing

### Want to Build Paint?
- [ ] Yes - Let's complete Phase 6 to 100%
- [ ] Later - After Phase 8
- [ ] Skip - Focus on other features

### Testing Depth?
- [ ] This was enough
- [ ] Want more detailed testing
- [ ] Want integration scenarios

---

## ?? Session Complete!

**Time Spent:** _______ minutes  
**Confidence Level:** High / Medium / Low  
**Production Ready:** Yes / Almost / Not Yet

**Tester:** _______  
**Date:** _______

---

**Status:** Ready for next phase!  
**Next:** Based on results, decide next steps

**Great job testing!** ???
