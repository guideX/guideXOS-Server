# ?? Notepad C++ vs C# Parity Analysis

**Date:** Current Session  
**Goal:** Achieve feature parity between C++ Notepad (guideXOSServer) and C# Notepad (guideXOS)

---

## ? Features Already Implemented in C++

### Core Text Editing ?
- ? Multi-line text editing
- ? Character insertion (A-Z, 0-9, space)
- ? Backspace to delete
- ? Enter for new lines
- ? Cursor navigation (arrow keys)
- ? Cursor visualization (| character)
- ? Tab key support (4 spaces)

### Keyboard Support ?
- ? Shift key modifier tracking
- ? Ctrl key modifier tracking
- ? Caps Lock toggle
- ? Shift + numbers = symbols (!@#$%^&*())
- ? Shift + special chars (;:,.<>/?etc.)
- ? Proper letter case with Shift/Caps

### UI Elements ?
- ? Window creation via IPC
- ? Title bar with filename
- ? Menu buttons (New, Open, Save, Save As, Wrap)
- ? Status bar with line/column
- ? Modified indicator (*)
- ? Modifier key badges ([CAPS], [SHIFT], [CTRL])

### State Management ?
- ? File path tracking
- ? Modified flag
- ? Lines vector storage
- ? Cursor position (line/col)
- ? Text wrap toggle

### Window Management ?
- ? Create window
- ? Update title
- ? Handle close event
- ? Redraw content

---

## ? Missing Features (From C# Version)

### 1. Key Debouncing ?
**C# Has:** De-bounce logic to prevent key repeats
```csharp
if (_keyDown && Keyboard.KeyInfo.ScanCode == _lastScan) return;
_keyDown = true; _lastScan = (byte)Keyboard.KeyInfo.ScanCode;
```
**C++ Status:** Not implemented
**Priority:** Medium
**Effort:** 15 minutes

---

### 2. Save Changes Dialog ?
**C# Has:** Prompt when closing with unsaved changes
```csharp
public override void OnSetVisible(bool value) {
    if (!value && _dirty) {
        _confirmDlg = new SaveChangesDialog(this, ...);
    }
}
```
**C++ Status:** Not implemented
**Priority:** High
**Effort:** 1-2 hours

---

### 3. Save Dialog ?
**C# Has:** File browser dialog for Save As
```csharp
_dlg = new SaveDialog(X + 40, Y + 40, 520, 360, Desktop.Dir, _fileName, ...);
```
**C++ Status:** Not implemented
**Priority:** High
**Effort:** 2-3 hours

---

### 4. File I/O Integration ?
**C# Has:** Load and save files via VFS
```csharp
byte[] data = File.ReadAllBytes(path);
File.WriteAllBytes(path, data);
```
**C++ Status:** Placeholder only
**Priority:** High
**Effort:** 1-2 hours

---

### 5. Escape Key Handling ?
**C# Has:** Escape key returns focus or cancels
```csharp
if (key.Key == ConsoleKey.Escape) { return; }
```
**C++ Status:** Not implemented
**Priority:** Low
**Effort:** 5 minutes

---

### 6. Click Lock (Mouse Debouncing) ?
**C# Has:** Prevents multiple clicks from single action
```csharp
if (!_clickLock) { /* handle click */ _clickLock = true; }
else { _clickLock = false; }
```
**C++ Status:** Not implemented (no mouse input yet)
**Priority:** Low (compositor handles this)
**Effort:** N/A

---

### 7. Recent Documents Tracking ?
**C# Has:** Adds opened files to recent documents
```csharp
RecentManager.AddDocument(path, Icons.DocumentIcon(32));
```
**C++ Status:** Not implemented
**Priority:** Medium
**Effort:** 15 minutes

---

### 8. Text Wrapping Logic ?
**C# Has:** Actual word wrap rendering
```csharp
if (_wrap) WindowManager.font.DrawString(tx + 6, ty + 6, _text, tw - 12, ...);
```
**C++ Status:** Toggle exists but doesn't affect rendering
**Priority:** Medium
**Effort:** 30 minutes

---

### 9. Scrolling Support ?
**C# Has:** Right-click drag to scroll
```csharp
if (_scrollDrag) {
    int dy = my - _scrollStartY;
    _scroll = _scrollStartScroll - (dy / _rowH);
}
```
**C++ Status:** s_scrollOffset exists but not used
**Priority:** Medium
**Effort:** 1 hour

---

### 10. Window Positioning ?
**C# Has:** Constructor takes x, y coordinates
```csharp
public Notepad(int x, int y) : base(x, y, 700, 460)
```
**C++ Status:** Window created at default position
**Priority:** Low
**Effort:** 10 minutes

---

### 11. Resizable Window ?
**C# Has:** Window properties set
```csharp
IsResizable = true;
ShowInTaskbar = true;
ShowMaximize = true;
ShowMinimize = true;
```
**C++ Status:** Window properties not customizable
**Priority:** Low (compositor default)
**Effort:** N/A

---

## ?? Priority Ranking

### Must Have (for basic parity)
1. **File I/O Integration** - Can't save/load without this
2. **Save Dialog** - Needed for Save As functionality
3. **Save Changes Dialog** - Prevents data loss

### Should Have (for good UX)
4. **Recent Documents Tracking** - Desktop integration
5. **Text Wrapping Logic** - Makes Wrap button functional
6. **Scrolling Support** - Needed for large files
7. **Key Debouncing** - Better keyboard feel

### Nice to Have (polish)
8. **Escape Key Handling** - Minor convenience
9. **Window Positioning** - Cosmetic
10. **Click Lock** - Handled by compositor

---

## ?? Implementation Roadmap

### Phase 1: Core File Operations (3-4 hours)
- [ ] Implement VFS read/write helpers
- [ ] Implement saveFile() with VFS
- [ ] Implement openFile() with VFS
- [ ] Parse file content into s_lines
- [ ] Serialize s_lines to string for save
- [ ] Test file save/load

### Phase 2: Dialogs (3-4 hours)
- [ ] Create SaveDialog class (C++ version)
- [ ] Implement file browser UI
- [ ] Handle dialog input events
- [ ] Wire to Save As button
- [ ] Create SaveChangesDialog class
- [ ] Hook to close event
- [ ] Test both dialogs

### Phase 3: Enhanced Features (2-3 hours)
- [ ] Implement text wrapping in redrawContent()
- [ ] Implement scroll offset handling
- [ ] Add scrolling keyboard shortcuts (Page Up/Down)
- [ ] Implement recent documents tracking
- [ ] Add key debouncing logic
- [ ] Add Escape key handler

### Phase 4: Testing & Polish (1-2 hours)
- [ ] Test all keyboard shortcuts
- [ ] Test file save/load
- [ ] Test dialogs
- [ ] Test large files (scrolling)
- [ ] Test unsaved changes prompt
- [ ] Fix any bugs found

**Total Estimated Time:** 9-13 hours

---

## ?? Feature Comparison Table

| Feature | C# Notepad | C++ Notepad | Priority | Effort |
|---------|-----------|-------------|----------|--------|
| Text editing | ? | ? | - | - |
| Keyboard input | ? | ? | - | - |
| Cursor navigation | ? | ? | - | - |
| Tab support | ? | ? | - | - |
| Shift/Caps support | ? | ? | - | - |
| Special characters | ? | ? | - | - |
| Menu buttons | ? | ? | - | - |
| Status bar | ? | ? | - | - |
| Modified indicator | ? | ? | - | - |
| Modifier badges | ? | ? | - | - |
| Wrap toggle button | ? | ? | - | - |
| **File save** | ? | ? | HIGH | 1-2h |
| **File load** | ? | ? | HIGH | 1-2h |
| **Save Dialog** | ? | ? | HIGH | 2-3h |
| **Save Changes Dialog** | ? | ? | HIGH | 1-2h |
| **Text wrapping logic** | ? | ? | MEDIUM | 30m |
| **Scrolling** | ? | ? | MEDIUM | 1h |
| **Recent docs tracking** | ? | ? | MEDIUM | 15m |
| **Key debouncing** | ? | ? | MEDIUM | 15m |
| Escape key | ? | ? | LOW | 5m |
| Window positioning | ? | ? | LOW | 10m |

**Parity Score: 11/21 = 52%**

---

## ?? Achievable Short-Term Goal

### Target: 80% Parity in 4-5 Hours

**Focus on:**
1. ? File I/O (2 hours)
2. ? Recent docs tracking (15 min)
3. ? Text wrapping logic (30 min)
4. ? Key debouncing (15 min)
5. ? Escape key (5 min)
6. ? Scrolling (1 hour)

**Defer to later:**
- Save Dialog (complex UI - 2-3 hours)
- Save Changes Dialog (complex UI - 1-2 hours)

This gets us **17/21 = 81% parity** with practical time investment.

---

## ?? Next Steps

### Immediate (Today):
1. Implement file I/O with VFS
2. Add recent documents tracking
3. Add key debouncing
4. Add Escape key handler

### Short-term (This Week):
5. Implement text wrapping logic
6. Implement scrolling support
7. Test all features thoroughly

### Medium-term (Next Week):
8. Create Save Dialog
9. Create Save Changes Dialog
10. Achieve 100% parity

---

## ? Success Criteria

**Minimum Viable Parity (80%):**
- ? Can save files
- ? Can load files
- ? Tracks recent documents
- ? Text wrapping works
- ? Scrolling works
- ? Clean keyboard input

**Full Parity (100%):**
- ? All of above
- ? Save Dialog working
- ? Save Changes prompt
- ? All UX polish complete

---

**Current Status: 52% Parity**  
**Target: 80% Parity Today**  
**Ultimate Goal: 100% Parity This Week**

Let's start implementing! ??
