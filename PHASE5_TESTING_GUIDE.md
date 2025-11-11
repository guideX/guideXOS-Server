# ?? Phase 5 - Testing Guide

## Quick Start (5 Minutes)

### 1. Launch Compositor
```bash
gui.start
```
**Expected:** Compositor window opens with taskbar

---

### 2. Test Desktop Service
```bash
# View registered apps
desktop.apps
```
**Expected Output:**
```
Registered Applications (6):
  Calculator
  Clock
  Console
  Notepad
  Paint
  TaskManager
```

```bash
# Pin an app
desktop.pinapp Calculator
desktop.pinapp Notepad
```
**Expected:** "Pinned app: Calculator" messages

```bash
# View pinned items
desktop.pinned
```
**Expected Output:**
```
Pinned Items (2):
  Calculator (App)
  Notepad (App)
```

```bash
# Launch an app (adds to recent)
desktop.launch Calculator
```
**Expected:** "Desktop launch successful: Calculator"

```bash
# View recent programs
desktop.recent
```
**Expected Output:**
```
Recent Programs (1):
  Calculator
Recent Documents (0):
```

---

### 3. Test Enhanced Start Menu

**3a. Open Start Menu**
- Click the Start button (left side of taskbar)
- **Expected:** Start menu opens with two columns

**3b. Test Navigation**
- Press ?/? arrow keys
- **Expected:** Selection moves up/down, highlighted in blue

**3c. Toggle All Programs**
- Click "All Programs >" button at bottom-left
- **Expected:** Menu shows alphabetically sorted apps (Calculator, Clock, Console, Notepad, Paint, TaskManager)
- Click "< Back" button
- **Expected:** Returns to Recent/Pinned view

**3d. Test Tab Toggle**
- With menu open, press Tab key
- **Expected:** Toggles between Recent and All Programs views

**3e. Launch from Menu**
- Select an item with arrows
- Press Enter
- **Expected:** Item launches, menu closes

**3f. Test Shortcuts**
- Click "Computer Files" in right column
- **Expected:** Computer Files action published
- Click "Console" in right column
- **Expected:** Console action published

**3g. Test Shutdown**
- Click "Shutdown" button (bottom-right)
- **Expected:** "Shutdown requested" logged, menu closes

**3h. Test Right-Click Pin/Unpin**
- Right-click on a non-pinned item
- **Expected:** Item becomes pinned (gets `*` marker)
- Right-click on a pinned item
- **Expected:** Item becomes unpinned (loses `*` marker)

**3i. Close Menu**
- Press Esc
- **Expected:** Menu closes
- Click outside menu
- **Expected:** Menu closes

---

### 4. Test Workspace Manager

```bash
# Switch to next workspace
workspace.next
```
**Expected:** Log shows "Switched to workspace 1"

```bash
# Switch to previous workspace
workspace.prev
```
**Expected:** Log shows "Switched to workspace 0"

```bash
# Switch to specific workspace
workspace.switch 2
```
**Expected:** Log shows "Switched to workspace 2"

```bash
# Query current workspace
workspace.current
gui.pop
```
**Expected Output:**
```
GUI: type=16 payload=WS_CURRENT|2
```

---

### 5. Test Persistence

```bash
# Pin several apps
desktop.pinapp Calculator
desktop.pinapp Paint
desktop.pinapp Console

# Launch apps
desktop.launch Notepad
desktop.launch Clock

# Verify saved to desktop.json
desktop.showconfig
```

**Expected Output:**
```
Wallpaper: (empty or path)
Pinned:
  Calculator
  Paint
  Console
Recent:
  Notepad
  Clock
```

**Restart compositor:**
```bash
# Close compositor window, then:
gui.start
desktop.pinned
```
**Expected:** Pinned items persist across restarts

---

## Advanced Testing (10 Minutes)

### 6. Test Window Creation & Start Menu Integration

```bash
gui.win TestApp 400 300
```
**Expected:** Window created

```bash
# Pin the app name
desktop.pinapp TestApp

# Reopen start menu
# Expected: TestApp appears in pinned section with * marker
```

### 7. Test File Pinning

```bash
desktop.pinfile MyScript data/test.gxm
desktop.pinned
```
**Expected Output:**
```
Pinned Items (?):
  ...
  MyScript (File: data/test.gxm)
```

### 8. Test Recent Tracking

```bash
# Launch multiple apps
desktop.launch Calculator
desktop.launch Paint
desktop.launch Console
desktop.launch Notepad

# View recent
desktop.recent
```
**Expected:** All 4 apps in recent list, newest first

### 9. Test Start Menu Scrolling (if > 14 items)

- Pin 15+ apps
- Open start menu
- Use ?/? arrows
- **Expected:** Menu scrolls when selection goes off-screen

### 10. Test Desktop Icon Click (if implemented)

- Click desktop icon
- **Expected:** Icon selects (highlighted)
- Double-click desktop icon
- **Expected:** Action launches

---

## Stress Testing (Optional)

### 11. Pin/Unpin Rapid Fire

```bash
for i in {1..20}; do
  desktop.pinapp Calculator
  desktop.unpin Calculator
done
```
**Expected:** No crashes, clean state

### 12. Recent Overflow Test

```bash
# Launch > 32 different apps
for app in Calculator Clock Console Notepad Paint TaskManager ...; do
  desktop.launch $app
done
desktop.recent
```
**Expected:** Only last 32 in list

### 13. Workspace Cycling

```bash
for i in {1..100}; do
  workspace.next
done
workspace.current
```
**Expected:** Cycles through workspaces 0-3 repeatedly

---

## Regression Testing

### 14. Existing Features Still Work

- ? Window creation: `gui.win Test 640 480`
- ? Window close: `gui.close <id>`
- ? Window move: `gui.move <id> 100 100`
- ? Window resize: `gui.resize <id> 800 600`
- ? Draw text: `gui.text <id> Hello`
- ? Draw rect: `gui.rect <id> 10 10 100 50 255 0 0`
- ? GXM load: `gxm.sample`
- ? Show desktop: Win+D in compositor window

### 15. Taskbar Still Works

- ? Start button toggles menu
- ? Window buttons show active windows
- ? Click window button focuses/restores
- ? Minimized windows show tombstoned state

---

## Performance Testing

### 16. Start Menu Responsiveness

- Open start menu
- Rapidly press arrow keys
- **Expected:** Smooth navigation, no lag

### 17. Pin/Launch Speed

```bash
time desktop.pinapp Calculator
time desktop.launch Calculator
```
**Expected:** < 50ms each

### 18. Persistence Speed

```bash
# Pin 20 apps, then:
time desktop.showconfig
```
**Expected:** < 100ms to load/display

---

## Known Limitations

?? **Not Yet Implemented:**
- Taskbar right-click menu (backend ready, UI pending)
- Workspace switcher visual button (backend ready)
- Recent Documents popout
- Desktop file browser integration
- Blur effect on taskbar

---

## Success Criteria

? All 18 test scenarios pass
? No crashes or hangs
? Smooth UX
? Persistence works across sessions
? CLI commands respond quickly
? Start menu navigable and functional

---

## Troubleshooting

### Issue: Start menu doesn't open
**Solution:** Check compositor is running (`gui.start`)

### Issue: Pinned items don't persist
**Solution:** Check desktop.json exists and is writable

### Issue: Workspace commands don't work
**Solution:** Ensure compositor restarted after adding workspace manager

### Issue: Recent list not updating
**Solution:** Use `desktop.launch` instead of direct action

---

## Reporting Issues

If you find bugs:
1. Note the exact command/action
2. Check logs: `log` command
3. Verify build: Should be successful
4. Check desktop.json for corruption

---

## ?? Happy Testing!

Enjoy exploring the new desktop features!
