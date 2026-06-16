# ?? F5 QEMU Integration - COMPLETE!

## ? What Was Set Up

Your `guideXOSServer` project now has **full F5 integration** just like the main `guideXOS` project!

---

## ?? **How to Use F5 to Launch QEMU**

### **Method 1: Launch Profile Dropdown** ? **RECOMMENDED**

1. **Open Visual Studio 2022**
2. **Open:** `guideXOSServer.sln` (or `.csproj`)
3. **Look at the toolbar** - Find the dropdown next to the green "Play" button
4. **Select:** "QEMU x86 Kernel" or "Build and Run C++ Kernel in QEMU"
5. **Press F5** or click the green play button!

**What happens:**
- Builds C++ kernel automatically (if using "Build and Run" profile)
- Launches QEMU with kernel.elf
- Shows boot screen!

---

### **Method 2: Build Property (Advanced)**

Run from command line with:

```cmd
dotnet build --property:vm=qemu
```

Or in Visual Studio:
1. Right-click project ? Properties
2. Build ? Advanced
3. Add: `-p:vm=qemu` to additional MSBuild arguments

---

### **Method 3: Debug Configuration**

1. **Solution Configurations dropdown** ? Configuration Manager
2. **Add new configuration:** "QEMU"
3. **Select "QEMU" configuration**
4. **Press F5**

Kernel builds and QEMU launches automatically!

---

## ?? **Files Created**

| File | Purpose |
|------|---------|
| `guideXOSServer.csproj` | MSBuild targets for kernel build + QEMU |
| `Properties/launchSettings.json` | F5 launch profiles |
| `F5-QEMU-SETUP.md` | This guide |

---

## ?? **Launch Profiles Explained**

### **Profile 1: "guideXOSServer"** (Default)
- Standard .NET application
- Use for C# development
- Does NOT launch kernel

### **Profile 2: "Build and Run C++ Kernel in QEMU"**
- Runs `RUN-KERNEL.bat`
- Builds C++ kernel first
- Then launches QEMU
- **Best for development**

### **Profile 3: "QEMU x86 Kernel"**
- Directly launches QEMU
- Assumes kernel is already built
- **Best for quick testing**

---

## ?? **How It Works**

### **MSBuild Integration**

The `.csproj` file now includes:

```xml
<Target Name="BuildCppKernel" BeforeTargets="BeforeBuild">
  <!-- Builds C++ kernel before C# project -->
</Target>

<Target Name="RunQemu" AfterTargets="Build" Condition="'$(vm)'=='qemu'">
  <!-- Launches QEMU after build -->
</Target>
```

### **Launch Profiles**

`Properties/launchSettings.json` defines:
- Executable path (QEMU or batch file)
- Command line arguments
- Working directory

---

## ?? **Comparison with guideXOS Project**

Your setup now has the **same functionality** as the main guideXOS project!

| Feature | guideXOS | guideXOSServer |
|---------|----------|----------------|
| F5 to launch QEMU | ? | ? **NEW!** |
| Build C++ kernel | ? | ? **NEW!** |
| Multiple launch profiles | ? | ? **NEW!** |
| MSBuild integration | ? | ? **NEW!** |
| Custom configurations | ? | ? **NEW!** |

---

## ?? **Quick Start**

### **Right Now - Test It!**

1. **Open Visual Studio 2022**
2. **Open:** `guideXOSServer.csproj` or `guideXOSServer.sln`
3. **Look at toolbar:** Find dropdown next to green play button
4. **Select:** "QEMU x86 Kernel"
5. **Press F5**
6. **QEMU window opens** with your kernel!

---

## ??? **Visual Studio UI**

### **Toolbar Should Show:**

```
???????????????????????????????????????????
? ? [QEMU x86 Kernel ?]  Any CPU ?       ?
?   ?                                      ?
?   ??? Click this dropdown to select     ?
????????????????????????????????????????????
```

### **Available Options:**

```
Dropdown Menu:
?? guideXOSServer (default C# app)
?? Build and Run C++ Kernel in QEMU  ? Builds first
?? QEMU x86 Kernel                    ? Direct launch
```

---

## ?? **Pro Tips**

### **Tip 1: Create Custom Build Configuration**

1. Build ? Configuration Manager
2. Active Solution Configuration ? New
3. Name: "QEMU"
4. Copy settings from: Debug

Now select "QEMU" configuration and press F5!

### **Tip 2: Keyboard Shortcut**

Default shortcuts:
- **F5** - Start Debugging (launches selected profile)
- **Ctrl+F5** - Start Without Debugging (faster)
- **Shift+F5** - Stop Debugging (closes QEMU)

### **Tip 3: Multiple QEMU Instances**

You can run multiple profiles simultaneously:
- Right-click project ? Debug ? Start New Instance
- Each opens a separate QEMU window

### **Tip 4: Add More QEMU Configurations**

Edit `Properties/launchSettings.json` to add:
- QEMU with debugging (`-s -S` flags)
- QEMU with serial output only
- QEMU with different memory sizes

Example:
```json
"QEMU Debug Mode": {
  "commandName": "Executable",
  "executablePath": "C:\\Program Files\\qemu\\qemu-system-i386.exe",
  "commandLineArgs": "-kernel \"kernel.elf\" -s -S -m 256M"
}
```

---

## ?? **Troubleshooting**

### **Issue: Dropdown doesn't show launch profiles**

**Solution:**
1. Close Visual Studio
2. Delete `.vs` folder in solution directory
3. Reopen solution
4. Profiles should appear

### **Issue: "QEMU not found" error**

**Solution:**
Check QEMU installation path in:
1. `guideXOSServer.csproj` (in `RunQemu` target)
2. `Properties/launchSettings.json` (in profile `executablePath`)

Update to your QEMU path if different.

### **Issue: Kernel not building**

**Solution:**
The `BuildCppKernel` target runs automatically. If it fails:
1. Check Output window for errors
2. Manually run: `kernel\build-x86.bat`
3. Fix any compilation errors

### **Issue: "Cannot find kernel.elf"**

**Solution:**
1. Build the kernel first: `cd kernel && build-x86.bat`
2. Verify file exists: `kernel\build\x86\bin\kernel.elf`
3. Check path in launch profile

---

## ?? **Recommended Workflow**

### **Development Cycle:**

```
1. Edit C++ kernel source (main.cpp, vga.cpp, etc.)
   ?
2. Select "Build and Run C++ Kernel in QEMU" from dropdown
   ?
3. Press F5 (or Ctrl+F5)
   ?
4. Watch Output window for build progress
   ?
5. QEMU launches with updated kernel
   ?
6. Test changes
   ?
7. Close QEMU (or Shift+F5)
   ?
8. Repeat from step 1!
```

### **Quick Testing:**

```
1. Kernel already built
   ?
2. Select "QEMU x86 Kernel" from dropdown
   ?
3. Press Ctrl+F5 (faster, no debugger)
   ?
4. QEMU launches immediately
```

---

## ?? **Comparison with Previous Setup**

| Method | Before | Now |
|--------|--------|-----|
| **F5 Launch** | ? "Local Windows Debugger" | ? **Works!** |
| **Build Integration** | ? Manual | ? **Automatic** |
| **Launch Profiles** | ? None | ? **3 profiles** |
| **Quick Testing** | ?? External tools only | ? **F5 or dropdown** |
| **MSBuild Targets** | ? Not integrated | ? **Fully integrated** |

---

## ?? **Success Checklist**

Your setup is working if:

- [x] Visual Studio shows launch profile dropdown
- [x] "QEMU x86 Kernel" option is visible
- [x] Pressing F5 builds the kernel
- [x] QEMU window opens automatically
- [x] Kernel displays colored boot screen
- [x] Status messages show "[ OK ]"

---

## ?? **Migration from Old Setup**

### **What Changed:**

**Old way (still works):**
- Double-click `RUN-KERNEL.bat`
- Tools menu ? External Tools
- `Ctrl+Shift+K` shortcut

**New way (F5):**
- Select profile from dropdown
- Press F5
- Integrated with Visual Studio

**Both methods work!** Use whichever you prefer.

---

## ?? **You're Ready!**

Your `guideXOSServer` project now has **the same F5 QEMU integration** as the main `guideXOS` project!

**Try it now:**
1. Open Visual Studio
2. Open `guideXOSServer` project
3. Select "QEMU x86 Kernel" from dropdown
4. Press F5
5. Watch your kernel boot in QEMU!

---

## ?? **Related Documentation**

- **VS-INTEGRATION-FIXED.md** - Previous setup (External Tools)
- **VS-TOOLS-MENU-SETUP.md** - Tools menu configuration
- **START-HERE.md** - General kernel overview
- **QUICKSTART-QEMU.md** - Command-line usage

---

**Congratulations! You now have full F5 integration like the main guideXOS project! ??**

*Press F5 and your kernel boots in QEMU!*
