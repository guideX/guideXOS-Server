# ?? Visual Studio Tools Menu Setup

Since this is a C# project, the standard F5 debugging doesn't directly support running external programs. Here's how to set it up properly:

---

## ? **Quick Solution - Use External Tools**

### **Method 1: Add to Tools Menu (Recommended)**

1. **Open Visual Studio 2022**
2. **Go to:** Tools ? External Tools...
3. **Click:** Add (bottom of window)
4. **Configure as follows:**

#### **Tool 1: Build and Run Kernel**

```
Title:       Build and Run C++ Kernel
Command:     cmd.exe
Arguments:   /c "cd /d "$(SolutionDir)" && RUN-KERNEL.bat"
Initial dir: $(SolutionDir)
? Use Output window
? Close on exit
```

**Click OK**

Now you can run it from: **Tools ? Build and Run C++ Kernel**

#### **Tool 2: Build Kernel Only**

```
Title:       Build C++ Kernel Only
Command:     cmd.exe
Arguments:   /c "cd /d "$(SolutionDir)kernel" && build-x86.bat && pause"
Initial dir: $(SolutionDir)
? Use Output window
```

#### **Tool 3: Run in QEMU (No Build)**

```
Title:       Run Kernel in QEMU
Command:     $(SolutionDir)scripts\run-qemu-x86.bat
Arguments:   
Initial dir: $(SolutionDir)kernel
```

---

## ?? **Method 2: Use RUN-KERNEL.bat**

I created a simple batch file in your solution root:

**Just double-click:** `RUN-KERNEL.bat`

This will:
1. Build the C++ kernel
2. Launch QEMU automatically

You can pin this to your taskbar for quick access!

---

## ?? **Method 3: Add Toolbar Button**

1. **Right-click on toolbar** ? Customize
2. **Commands tab** ? Toolbar: Standard
3. **Add Command** ? Tools ? External Command 1
4. **Drag button** to convenient location
5. **Right-click button** ? Change icon (choose rocket ??)

Now you have a one-click button to run your kernel!

---

## ? **Method 4: Keyboard Shortcut**

1. **Tools ? Options** ? Environment ? Keyboard
2. **Search for:** `Tools.ExternalCommand1`
3. **Assign shortcut:** `Ctrl+Shift+K` (or your preference)
4. **Click Assign**

Now press `Ctrl+Shift+K` to build and run!

---

## ?? **Quick Access Methods Comparison**

| Method | Speed | Setup | Best For |
|--------|-------|-------|----------|
| External Tools Menu | ??? | 2 min | Organized workflow |
| RUN-KERNEL.bat | ????? | 0 min | Quick testing |
| Toolbar Button | ????? | 3 min | Visual appeal |
| Keyboard Shortcut | ????? | 2 min | Power users |

---

## ?? **Recommended Setup (All Methods!)**

Use **all four methods** for maximum convenience:

1. **Add to Tools menu** - For structured access
2. **Keep RUN-KERNEL.bat** - For quick double-click testing
3. **Add toolbar button** - For visual workflow
4. **Assign Ctrl+Shift+K** - For rapid iteration

---

## ??? **Visual Studio External Tools Screenshot**

When configured, it looks like this:

```
Tools
  ?? Options...
  ?? NuGet Package Manager
  ?? External Tools...
  ?? ?????????????????????????
  ?? Build and Run C++ Kernel    ? Your new command!
  ?? Build C++ Kernel Only
  ?? Run Kernel in QEMU
  ?? ...
```

---

## ? **Test Your Setup**

After configuring:

1. **Open** Visual Studio with `guideXOSServer.sln`
2. **Click** Tools ? Build and Run C++ Kernel
3. **Wait** for build output in Output window
4. **See** QEMU window appear with kernel!

---

## ?? **Troubleshooting**

### Issue: "Command not found"

**Solution:** Make sure you used `$(SolutionDir)` in the Initial directory field.

### Issue: "Window closes too fast"

**Solution:** Add `pause` to the Arguments field:
```
Arguments: /c "cd /d "$(SolutionDir)" && RUN-KERNEL.bat && pause"
```

### Issue: "Output not visible"

**Solution:** Enable "Use Output window" checkbox in External Tools.

---

## ?? **Pro Tips**

### Tip 1: Create Multiple Configurations

Add separate tools for:
- Debug build (with -g -O0)
- Release build (with -O2)
- QEMU with debugging (-s -S flags)

### Tip 2: Use Batch File Arguments

Edit `RUN-KERNEL.bat` to accept arguments:
```cmd
if "%1"=="debug" (
    REM Launch QEMU with debugging
    qemu-system-i386 -kernel ... -s -S
) else (
    REM Normal launch
)
```

Then in External Tools:
```
Arguments: /c "cd /d "$(SolutionDir)" && RUN-KERNEL.bat debug"
```

### Tip 3: Add Pre-Build Cleaning

Create a tool to clean before building:
```
Title:       Clean and Build Kernel
Arguments:   /c "cd /d "$(SolutionDir)kernel" && clean.bat && build-x86.bat && pause"
```

---

## ?? **You're Ready!**

Your Visual Studio now has multiple ways to launch the kernel:

? **Tools menu** - Professional workflow
? **Double-click batch file** - Quick testing  
? **Toolbar button** - Visual access
? **Keyboard shortcut** - Power user mode

Choose whichever method you prefer!

---

## ?? **Related Files**

- `RUN-KERNEL.bat` - Quick launcher (just created)
- `kernel/build-x86.bat` - Kernel build script
- `scripts/run-qemu-x86-with-build.bat` - QEMU launcher
- `scripts/run-qemu-x86.bat` - QEMU only (no build)

---

**Pick your favorite method and start building! ??**
