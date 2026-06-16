# ?? Launch Profiles Not Showing? - Quick Fix

## ? **Problem**

You see:
- "Local Windows Debugger"
- "Remote Windows Debugger"
- "Web Browser Debugger"

But **NOT** the QEMU profiles we created.

---

## ? **Solution - 3 Quick Fixes**

### **Fix 1: Reload Solution** ? **Try This First!**

1. **Close all files** in Visual Studio
2. **Close the solution:** File ? Close Solution
3. **Delete `.vs` folder:**
   - Navigate to: `D:\devgitlab\guideXOS\guideXOSServer\`
   - Delete the `.vs` folder (it's hidden)
   - Or run: `rmdir /s /q .vs` from command prompt
4. **Reopen solution:** File ? Open ? Project/Solution
5. **Check dropdown again**

The profiles should now appear!

---

### **Fix 2: Use "Start External Program"** ? **Works Immediately!**

Since launch profiles aren't showing, use the project properties instead:

1. **Right-click** `guideXOSServer` project in Solution Explorer
2. **Select:** Properties
3. **Go to:** Debug ? General
4. **Click:** "Open debug launch profiles UI"

**OR** use the old method:

1. **Right-click** project ? Properties
2. **Debug tab**
3. **Start external program:** Browse to `RUN-KERNEL.bat`
4. **Press F5** - it will run the batch file!

---

### **Fix 3: Use Post-Build Event** ? **Automatic!**

Make QEMU launch automatically after every build:

1. **Right-click** project ? Properties
2. **Build Events** tab
3. **Post-build event command line:**

```cmd
cd "$(ProjectDir)kernel"
call build-x86.bat
if errorlevel 0 start "" "$(ProjectDir)scripts\run-qemu-x86-with-build.bat"
```

4. **Build project (Ctrl+Shift+B)**
5. **QEMU launches automatically!**

---

## ?? **Why It's Not Showing**

The dropdown you're seeing is the **debugger type selector**, not the **launch profiles dropdown**.

For launch profiles to appear, you need:
1. ? `Properties/launchSettings.json` file (you have this)
2. ? Proper JSON format (just fixed this)
3. ? Visual Studio to reload the file (do Fix 1)

---

## ??? **Where to Look**

### **Wrong Dropdown (What You're Seeing):**

```
???????????????????????????????????
? Local Windows Debugger      ?  ?  ? Debugger type
???????????????????????????????????
```

### **Correct Dropdown (What You Need):**

```
???????????????????????????????????
? ? [guideXOSServer         ?]   ?  ? Launch profile
???????????????????????????????????
     ?
     This should be to the LEFT of the debugger dropdown
```

Look for **TWO dropdowns** in the toolbar!

---

## ?? **Immediate Solution - Use This Now!**

Since the dropdown isn't working, use the **fastest method**:

### **Method A: Configure Startup**

1. Right-click project ? **Set as Startup Project**
2. Right-click project ? **Properties**
3. **Debug** ? **General**
4. **Launch:** Select "Executable"
5. **Executable:** `$(ProjectDir)RUN-KERNEL.bat`
6. **Press F5!**

### **Method B: External Tools (Still Works!)**

1. **Tools** ? **External Tools**
2. **Add** new tool:
   - Title: `Run Kernel`
   - Command: `$(ProjectDir)RUN-KERNEL.bat`
3. **Tools** ? **Run Kernel**

### **Method C: Just Use The Batch File!**

The batch file works perfectly:

```
D:\devgitlab\guideXOS\guideXOSServer\RUN-KERNEL.bat
```

**Double-click it!** It's faster than F5 anyway. ??

---

## ?? **Diagnostic Steps**

### **Check 1: Is launchSettings.json being read?**

1. Open: `Properties\launchSettings.json`
2. Make a small change (add a comment)
3. Save
4. Reload solution
5. Check dropdown again

### **Check 2: Is it a C# project?**

The launch profiles dropdown **only works with C# projects** that have proper project structure.

Your project is `.csproj` based, so it should work.

### **Check 3: Visual Studio version**

Launch profiles require:
- Visual Studio 2019 16.8+
- Visual Studio 2022 (any version)

---

## ?? **What Actually Works Right Now**

### **? Methods That Work:**

1. **Double-click `RUN-KERNEL.bat`** - Always works
2. **Tools ? External Tools** - If configured
3. **Keyboard shortcut** (`Ctrl+Shift+K`) - If configured
4. **Post-build event** - If configured
5. **Project Properties ? Start External Program**

### **? Methods That Need Fixing:**

1. **Launch profiles dropdown** - Requires solution reload
2. **F5 with profile** - Requires dropdown to work

---

## ?? **Quick Test - Try This NOW**

### **Test 1: Simplest Method**

```cmd
# Open command prompt in project directory
cd D:\devgitlab\guideXOS\guideXOSServer

# Just run the batch file
RUN-KERNEL.bat
```

Does it work? Then the problem is just Visual Studio integration.

### **Test 2: Project Properties**

1. Right-click project ? Properties
2. Debug ? General
3. Launch: **Executable**
4. Executable path: `D:\devgitlab\guideXOS\guideXOSServer\RUN-KERNEL.bat`
5. Press **F5**

Does it work? Then launch profiles are just not showing.

---

## ?? **Step-by-Step: Make F5 Work**

### **Complete Setup:**

1. **Close Visual Studio completely**

2. **Delete `.vs` folder:**
```cmd
cd D:\devgitlab\guideXOS\guideXOSServer
rmdir /s /q .vs
```

3. **Verify `launchSettings.json` exists:**
```cmd
type Properties\launchSettings.json
```

4. **Reopen Visual Studio**

5. **Open solution/project**

6. **Wait for IntelliSense to load** (bottom left: "Ready")

7. **Look for TWO dropdowns in toolbar:**
   - One for launch profile (left)
   - One for debugger type (right)

8. **If still not showing:**
   - Right-click project ? Properties
   - Debug ? Open debug launch profiles UI
   - You should see the profiles there

---

## ?? **Bottom Line**

**The kernel and QEMU work fine!** The only issue is Visual Studio UI.

**Use any of these RIGHT NOW:**

1. **Double-click:** `RUN-KERNEL.bat` ?
2. **Command line:** `cd kernel && build-x86.bat` then run QEMU ?
3. **Tools menu:** If you configured it ?
4. **Project Properties ? Debug:** Set external program ?

**For the dropdown to work:**
- Delete `.vs` folder
- Reload solution
- Wait for VS to detect the launch profiles

---

## ?? **If Nothing Works**

Try the **guaranteed method**:

```cmd
# Build kernel
cd D:\devgitlab\guideXOS\guideXOSServer\kernel
build-x86.bat

# Run QEMU
cd ..
scripts\run-qemu-x86-with-build.bat
```

This **always works** regardless of Visual Studio configuration!

---

**Try Fix 1 (delete .vs folder and reload), then use the batch file while waiting for VS to reload!** ??
