# Phase 5 - Quick Reference

## Desktop Service CLI Commands

```bash
# View all registered applications
desktop.apps

# Pin/unpin applications
desktop.pinapp Calculator
desktop.pinapp Notepad
desktop.unpin Calculator

# Pin files
desktop.pinfile MyScript data/script.gxm

# Launch applications (adds to recent)
desktop.launch Calculator
desktop.launch Paint

# View pinned items with details
desktop.pinned

# View recent programs and documents  
desktop.recent

# Show full desktop configuration
desktop.showconfig
```

## Start Menu Features

### Keyboard Shortcuts
- **?/?** - Navigate items
- **Enter** - Launch selected item
- **Tab** - Toggle between Recent and All Programs
- **Esc** - Close start menu

### Mouse Interaction
- **Single-click** - Select item
- **Double-click** - Launch item
- **Right-click** - Pin/Unpin item

### Layout
```
Left Column:
  - Pinned items (marked with *)
  - Recent programs
  OR
  - All programs (alphabetically sorted)
  
Right Column:
  - Computer Files
  - Console  
  - Recent Documents

Bottom Bar:
  - "All Programs >" / "< Back" toggle
  - Shutdown button
```

## Testing Workflow

1. **Start compositor:**
   ```bash
   gui.start
   ```

2. **Pin some apps:**
   ```bash
   desktop.pinapp Calculator
   desktop.pinapp Notepad
   desktop.pinapp Paint
   ```

3. **Launch apps to build recent list:**
   ```bash
   desktop.launch Calculator
   desktop.launch Console
   ```

4. **Open start menu:**
   - Click start button on taskbar
   - Click items to navigate
   - Double-click to launch
   - Click "All Programs >" to see sorted list

5. **Use shortcuts:**
   - Click "Computer Files" to open file browser
   - Click "Console" to open terminal
   - Click "Shutdown" to trigger shutdown

6. **Verify persistence:**
   ```bash
   desktop.showconfig
   ```

## Data Persistence

**File:** `desktop.json`

**Content:**
```json
{
  "wallpaper": "assets/wallpaper.bmp",
  "pinned": ["Calculator", "Notepad", "Paint"],
  "recent": ["Calculator", "Console"],
  "windows": [ /* saved window states */ ]
}
```

**Auto-save triggers:**
- Pin/unpin action
- Launch application
- Add recent program/document

## What's New in This Update

? Desktop Service - Pinned/recent management
? Enhanced Start Menu - Two columns with shortcuts
? All Programs view - Alphabetically sorted
? Keyboard navigation - Arrow keys, Tab, Enter
? Power menu - Shutdown button
? Persistence - Automatic save to desktop.json
? CLI commands - 6 new desktop.* commands

## Known Limitations

?? Recent Documents popout not yet implemented (placeholder)
?? Taskbar right-click menu not yet implemented
?? Frame caching optimization not yet implemented
?? Blur effect for taskbar not yet implemented

## Next Phase

Phase 6 will add default applications:
- Notepad
- File Explorer
- Console window
