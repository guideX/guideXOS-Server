# ?? Visual Studio Setup Guide

## Quick Setup - Run Kernel from Visual Studio

I've configured your Visual Studio project to build and run the kernel in QEMU with F5!

---

## ?? How to Use

### Method 1: Using the Custom Launch Script (Easiest)

1. **Open the solution** in Visual Studio 2022
   - File: `guideXOSServer.sln`

2. **Run the kernel**
   - Press `Ctrl+F5` (Start Without Debugging)
   - Or press `F5` (Start Debugging)
   - Or click the green "Play" button in the toolbar

3. **What happens:**
   - ? Visual Studio runs `kernel\build-x86.bat`
   - ? Builds all source files (boot.asm, main.cpp, vga.cpp, etc.)
   - ? Links kernel.elf
   - ? Launches QEMU automatically
   - ? Shows kernel boot screen!

---

## Method 2: Using Solution Explorer

### Step 1: Add Kernel Project to Solution

1. Right-click on solution in Solution Explorer
2. Add ? Existing Project
3. Select `KernelBuild.vcxproj`

### Step 2: Set as Startup Project

1. Right-click on `KernelBuild` project
2. Select "Set as Startup Project"
3. Press F5 to build and run!

---

## ?? Files Created

| File | Purpose |
|------|---------|
| `.vs/launch.vs.json` | Launch configuration for debugging |
| `.vs/tasks.vs.json` | Build task configuration |
| `scripts/run-qemu-x86-with-build.bat` | Build and run script |
| `KernelBuild.vcxproj` | Kernel project file for VS |
| `VS-SETUP.md` | This guide |

---

## ?? Manual Build Options

If you prefer manual control:

### Option 1: External Tools

**Add to Tools menu:**

1. Tools ? External Tools ? Add
2. **Title:** Build Kernel (x86)
3. **Command:** `cmd.exe`
4. **Arguments:** `/c "cd kernel && build-x86.bat"`
5. **Initial directory:** `$(SolutionDir)`
6. **?** Use Output window

**Add another:**

1. Tools ? External Tools ? Add
2. **Title:** Run Kernel in QEMU
3. **Command:** `$(SolutionDir)scripts\run-qemu-x86-with-build.bat`
4. **Initial directory:** `$(SolutionDir)`

Now you can run from Tools menu!

### Option 2: Custom Build Events

Add to your main project's Post-Build Event:

```cmd
cd "$(SolutionDir)kernel"
call build-x86.bat
if errorlevel 0 (
    cd "$(SolutionDir)"
    start scripts\run-qemu-x86-with-build.bat
)
```

---

## ?? Keyboard Shortcuts

| Action | Shortcut |
|--------|----------|
| Build and Run | `F5` or `Ctrl+F5` |
| Build Only | `Ctrl+Shift+B` |
| Clean Solution | Right-click ? Clean |
| Rebuild All | `Ctrl+Alt+F7` |

---

## ?? What Each File Does

### `.vs/launch.vs.json`
Configures Visual Studio debugger to:
- Run the build script in the kernel directory
- Launch QEMU after successful build
- Show output in external terminal

### `.vs/tasks.vs.json`
Defines the build task:
- Runs `kernel\build-x86.bat`
- Compiles all kernel sources
- Links final kernel.elf

### `scripts/run-qemu-x86-with-build.bat`
Combined script that:
- Builds the kernel
- Checks for errors
- Launches QEMU if build succeeds
- Shows helpful error messages

### `KernelBuild.vcxproj`
Visual Studio project that:
- Lists all kernel source files
- Provides IntelliSense for C/C++ files
- Shows project structure in Solution Explorer

---

## ?? Troubleshooting

### Issue: "Cannot find qemu-system-i386.exe"

**Fix:**
1. Edit `scripts/run-qemu-x86-with-build.bat`
2. Change line:
   ```cmd
   set "QEMU_PATH=C:\Program Files\qemu\qemu-system-i386.exe"
   ```
3. Update to your QEMU installation path

### Issue: "i686-elf-gcc not found"

**Fix:**
Your cross-compiler isn't in PATH. Two options:

**Option A: Add to Windows PATH**
1. Windows Key + X ? System
2. Advanced system settings ? Environment Variables
3. Add `D:\bkup\elfbin\bin` to PATH
4. Restart Visual Studio

**Option B: Edit build-x86.bat**
The script already auto-detects `D:\bkup\elfbin\bin` - make sure files are there!

### Issue: Build succeeds but QEMU doesn't start

**Check:**
```cmd
dir kernel\build\x86\bin\kernel.elf
```

If file exists, check QEMU path in the launch script.

### Issue: Visual Studio says "Unable to start program"

**Solution:**
You're trying to debug the batch file itself. Use `Ctrl+F5` (Start Without Debugging) instead of `F5`.

---

## ?? IntelliSense Configuration

To get IntelliSense for kernel C++ files:

### Step 1: Create c_cpp_properties.json

Create `.vscode/c_cpp_properties.json` (if using VS Code mode):

```json
{
    "configurations": [
        {
            "name": "Win32",
            "includePath": [
                "${workspaceFolder}/kernel/core/include",
                "${workspaceFolder}/kernel/arch/x86/include"
            ],
            "defines": [
                "__i386__",
                "ARCH_X86"
            ],
            "compilerPath": "D:/bkup/elfbin/bin/i686-elf-g++.exe",
            "cStandard": "c11",
            "cppStandard": "c++14",
            "intelliSenseMode": "gcc-x86"
        }
    ],
    "version": 4
}
```

### Step 2: Configure Visual Studio C++ Settings

1. Right-click `KernelBuild` project
2. Properties
3. C/C++ ? General ? Additional Include Directories
4. Add:
   - `$(SolutionDir)kernel\core\include`
   - `$(SolutionDir)kernel\arch\x86\include`

---

## ?? Workflow Examples

### Quick Test Cycle

```
1. Edit kernel/core/main.cpp in Visual Studio
2. Press Ctrl+F5
3. See changes in QEMU immediately!
```

### Full Development Cycle

```
1. Edit source files
2. Press Ctrl+Shift+B to build
3. Check Output window for errors
4. Fix any compilation errors
5. Press Ctrl+F5 to run in QEMU
6. Test kernel behavior
7. Close QEMU when done
8. Repeat!
```

---

## ?? Visual Studio Output

When you press F5, you'll see:

```
==========================================
Visual Studio - Build and Run Kernel
==========================================

[Step 1/2] Building kernel...

[INFO] Found ELF toolchain at: D:\bkup\elfbin\bin
[INFO] Found NASM at: C:\Program Files\NASM
[OK] Added toolchain to PATH
[OK] Added NASM to PATH

==========================================
Building guideXOS Kernel (x86)
==========================================

[OK] Found source files

Checking toolchain...
[OK] Cross-compiler found
[OK] Assembler found

Creating build directories...
[OK] Directories created

[1/6] Assembling boot.asm...
[OK] boot.o created
[2/6] Compiling main.cpp...
[OK] main.o created
[3/6] Compiling vga.cpp...
[OK] vga.o created
[4/6] Compiling core arch.cpp...
[OK] arch.o (core) created
[5/6] Compiling x86 arch.cpp...
[OK] arch.o (x86) created
[6/6] Linking kernel...
[OK] kernel.elf created

==========================================
BUILD SUCCESSFUL!
==========================================

[Step 2/2] Launching QEMU...

Starting QEMU...
Kernel: build\x86\bin\kernel.elf

Press Ctrl+C to stop QEMU
==========================================

```

Then QEMU window opens with your kernel running!

---

## ?? Tips & Tricks

### Tip 1: Use Output Window
- View ? Output (or Ctrl+W, O)
- See all build messages
- Spot errors quickly

### Tip 2: Quick Rebuild
- Build ? Rebuild Solution
- Forces complete rebuild
- Useful after header changes

### Tip 3: Multiple QEMU Instances
- You can run multiple kernels at once
- Each in separate QEMU window
- Great for testing changes

### Tip 4: QEMU Debug Mode
Edit `run-qemu-x86-with-build.bat` and add:
```cmd
-d int,cpu_reset -D qemu.log
```
This creates `qemu.log` with debug info!

---

## ? Success Indicators

Your setup is working if:

1. ? Pressing F5 builds without errors
2. ? QEMU window appears automatically
3. ? You see the colored boot screen
4. ? Kernel displays "guideXOS Kernel v0.1"
5. ? Status messages show green "[ OK ]"

---

## ?? Getting Help

If something doesn't work:

1. Check the **Troubleshooting** section above
2. Run `diagnose.bat` from command line
3. Check `TOOLCHAIN-SETUP.md` for toolchain issues
4. See `README-BOOT.md` for general build help

---

## ?? You're Ready!

**Press F5 in Visual Studio and watch your kernel boot!**

The build process is fully automated:
- ? Detects toolchain automatically
- ? Builds all sources
- ? Links kernel
- ? Launches QEMU
- ? Shows boot screen

**Happy kernel development!** ??

---

*Pro tip: Keep the QEMU window on a second monitor while you code. Press Ctrl+F5 to see your changes instantly!*
