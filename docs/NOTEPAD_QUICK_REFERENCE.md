# ?? Notepad Quick Reference - guideXOSServer

**Version:** 1.0 (Phase 6 - 81% C#/C++ Parity)  
**Status:** ? Production Ready  
**Last Updated:** Current Session

---

## ? Quick Start

### Launch Notepad
```bash
# Start compositor first
gui.start

# Launch empty notepad
notepad

# Launch with file
notepad data/myfile.txt

# Via desktop service
desktop.launch Notepad
```

---

## ?? Keyboard Shortcuts

### Text Editing
| Key | Action |
|-----|--------|
| **A-Z, 0-9** | Type characters |
| **Shift + Letter** | Uppercase letter |
| **Shift + Number** | Symbols (!@#$%^&*()) |
| **Caps Lock** | Toggle caps lock |
| **Tab** | Insert 4 spaces |
| **Enter** | New line |
| **Backspace** | Delete previous character |
| **Space** | Insert space |

### Navigation
| Key | Action |
|-----|--------|
| **? ?** | Move cursor left/right |
| **? ?** | Move cursor up/down |
| **Home** | Beginning of line |
| **End** | End of line |
| **Page Up** | Scroll up 10 lines |
| **Page Down** | Scroll down 10 lines |

### Special Keys
| Key | Action |
|-----|--------|
| **Escape** | Reserved for dialogs |
| **Shift** | Modifier (shown in status) |
| **Ctrl** | Modifier (shown in status) |

---

## ??? Mouse Controls

### Menu Buttons
| Button | Action |
|--------|--------|
| **New** | Clear text, start fresh |
| **Open** | Load file (future: file browser) |
| **Save** | Save to current file |
| **Save As** | Save to default path (data/untitled.txt) |
| **Wrap** | Toggle text wrapping on/off |

### Window Controls
- **Drag titlebar** - Move window
- **Double-click titlebar** - Maximize/restore
- **Click X button** - Close notepad

---

## ?? File Operations

### Saving Files
```bash
# Default save location
data/untitled.txt

# Current behavior:
# 1. Type text
# 2. Click "Save As"
# 3. File saved to data/untitled.txt
# 4. Appears in Recent Documents

# Future: Save Dialog will allow custom path
```

### Loading Files
```bash
# Method 1: Command line
notepad data/myfile.txt

# Method 2: Open button (future)
# Click "Open"
# Browse files
# Select file
```

### File Format
- **Plain text** - UTF-8 compatible
- **Line endings** - \n (Unix style)
- **Tabs** - Converted to 4 spaces
- **Encoding** - ASCII (32-126)

---

## ?? Status Bar Information

### Line and Column
```
Line 5, Col 12
```
Shows current cursor position (1-based)

### Modified Indicator
```
Line 5, Col 12 (Modified)
```
Asterisk (*) in title when unsaved changes

### Modifier Keys
```
Line 5, Col 12 [CAPS] [SHIFT]
```
Shows active modifier keys

---

## ?? Features

### ? Working Features
- Multi-line text editing
- Character insertion (all printable ASCII)
- Backspace and delete
- Enter for new lines
- Arrow key navigation
- Tab key (4 spaces)
- Shift key for uppercase/symbols
- Caps Lock toggle
- File save to VFS
- File load from VFS
- Cursor visualization (|)
- Line/column tracking
- Modified state tracking
- Recent documents integration
- Text wrapping (80 chars)
- Scrolling (Page Up/Down)
- Home/End keys
- Key debouncing

### ? Future Features
- Save Dialog (file browser)
- Save Changes prompt
- Copy/Paste
- Find/Replace
- Undo/Redo
- Select text
- Word wrapping (word-aware)
- Syntax highlighting
- Line numbers

---

## ?? Known Limitations

1. **Simple Wrapping** - Wraps at 80 chars, not word-aware
2. **No File Browser** - Save As uses default path
3. **No Unsaved Prompt** - Closing loses unsaved changes
4. **No Clipboard** - Copy/Paste not implemented
5. **No Selection** - Can't select text blocks
6. **Fixed Window Size** - 640x480 pixels
7. **Max Visible Lines** - 25 lines at once

---

## ?? Technical Details

### Architecture
```
Notepad Process
    ?
Event Loop (IPC)
    ?
Compositor (Rendering)
```

### State Management
```cpp
static uint64_t s_windowId;      // Window ID
static std::string s_filePath;   // Current file
static vector<string> s_lines;   // Text content
static int s_cursorLine;         // Cursor line (0-based)
static int s_cursorCol;          // Cursor col (0-based)
static bool s_modified;          // Unsaved changes
static int s_scrollOffset;       // Scroll position
static bool s_wrapText;          // Wrap enabled
```

### Message Types
```cpp
MT_Create       // Window creation
MT_Close        // Window close
MT_InputKey     // Keyboard input
MT_WidgetEvt    // Button clicks
MT_DrawText     // Content rendering
MT_SetTitle     // Title updates
```

---

## ?? Examples

### Example 1: Create and Save File
```bash
# 1. Launch notepad
gui.start
notepad

# 2. Type content
"This is my first file.
It has multiple lines.
Notepad is working!"

# 3. Save
# Click "Save As" button
# File saved to: data/untitled.txt

# 4. Verify
desktop.recent
# Shows: data/untitled.txt
```

### Example 2: Edit Existing File
```bash
# 1. Launch with file
notepad data/readme.txt

# 2. Edit content
# Move cursor with arrows
# Type new text
# Use Home/End to navigate

# 3. Save changes
# Click "Save" button

# 4. Verify
# Close and reopen
notepad data/readme.txt
# Changes preserved ?
```

### Example 3: Navigate Large File
```bash
# 1. Create large file (50+ lines)
# Type or load large file

# 2. Scroll
# Press Page Down - scrolls down 10 lines
# Press Page Up - scrolls up 10 lines

# 3. Navigate
# Use arrow keys
# Cursor auto-scrolls into view

# 4. Jump to line start/end
# Press Home - cursor to start
# Press End - cursor to end
```

---

## ?? Tips & Tricks

### Tip 1: Use Tab for Indentation
```
# Instead of pressing space 4 times
Press Tab once
# Inserts 4 spaces automatically
```

### Tip 2: Navigate with Home/End
```
# Long line? Don't use arrows!
Press Home - jump to start
Press End - jump to end
```

### Tip 3: Toggle Text Wrapping
```
# Long lines going off screen?
Click "Wrap" button
# Lines truncate at 80 chars

# Need to see full line?
Click "Wrap" again
# Shows full line
```

### Tip 4: Check Recent Documents
```bash
# Can't remember file path?
desktop.recent
# Shows all recent files
```

### Tip 5: Use Page Keys for Large Files
```
# 100+ line file?
Page Down - quick scroll down
Page Up - quick scroll up
# Much faster than arrow keys
```

---

## ?? Troubleshooting

### Problem: Notepad Won't Launch
```bash
# Check if compositor is running
gui.start

# Try launching again
notepad

# Check logs
log
# Look for "Notepad starting..."
```

### Problem: Can't Type Text
```bash
# Make sure window has focus
# Click in notepad window

# Try pressing a key
# Should see character appear
```

### Problem: File Not Saving
```bash
# Check logs after clicking Save
log
# Should see: "Notepad: Saved to..."

# Check VFS
# File should be in data/ directory
```

### Problem: Cursor Not Visible
```bash
# Cursor shows as | character
# Make sure cursor is within text
# Try pressing Home key
```

### Problem: Text Disappears When Scrolling
```bash
# This is normal - only 25 lines visible
# Use Page Up/Down to scroll
# Content is preserved in memory
```

---

## ?? Related Documentation

1. **PHASE6_NOTEPAD_COMPLETE.md** - Full feature documentation
2. **NOTEPAD_PARITY_PROGRESS.md** - Implementation details
3. **NOTEPAD_PARITY_ANALYSIS.md** - C#/C++ comparison
4. **PHASE6_IMPLEMENTATION.md** - Phase 6 overview

---

## ? Version History

### Version 1.0 (Current) - 81% Parity
- ? File save/load via VFS
- ? Recent documents tracking
- ? Key debouncing
- ? Scrolling support
- ? Text wrapping (80 chars)
- ? Home/End/Page keys
- ? Modified state tracking
- ? Multi-line editing
- ? Full keyboard support

### Version 0.9 - Basic Functionality
- ? Window creation
- ? Text display
- ? Keyboard input
- ? Cursor navigation
- ? Menu buttons
- ? Status bar

---

## ?? Learning Resources

### For Users
- Try all keyboard shortcuts
- Experiment with wrapping toggle
- Create and save files
- Check recent documents integration

### For Developers
- Study VFS integration pattern
- Review IPC message handling
- Examine state management
- Learn app development pattern

---

## ?? Achievement Checklist

- [ ] Launch Notepad
- [ ] Type your first text
- [ ] Use arrow keys to navigate
- [ ] Try Shift + letters for uppercase
- [ ] Insert Tab (4 spaces)
- [ ] Press Enter for new line
- [ ] Save a file (Save As button)
- [ ] Check recent documents
- [ ] Load saved file
- [ ] Edit and save again
- [ ] Try Page Up/Down scrolling
- [ ] Use Home/End keys
- [ ] Toggle text wrapping
- [ ] Check status bar
- [ ] Close and reopen file
- [ ] Verify changes persisted

**Complete all 16?** ?? **You're a Notepad expert!**

---

**Quick Help:**
- **Stuck?** Check troubleshooting section
- **Question?** Read full documentation
- **Bug?** Check logs with `log` command
- **Feedback?** Document your experience

---

**Notepad v1.0 - Ready for Production Use!** ?

**Enjoy editing with guideXOSServer!** ???
