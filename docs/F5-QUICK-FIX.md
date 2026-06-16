# ? QUICK FIX - Make F5 Work Right Now

## The Problem
Launch profiles dropdown isn't showing in Visual Studio.

## The Solution ?
**Use Project Properties to set external program!**

---

## ?? **Steps to Make F5 Work (2 Minutes)**

### 1. Open Project Properties

- **Right-click** `guideXOSServer` project in Solution Explorer
- **Select:** Properties

### 2. Configure Debug Settings

- **Go to:** Debug ? General
- **Launch:** Select **"Executable"**
- **Executable:** Click browse and select:
  ```
  D:\devgitlab\guideXOS\guideXOSServer\RUN-KERNEL.bat
  ```
- **Working Directory:** 
  ```
  D:\devgitlab\guideXOS\guideXOSServer
  ```

### 3. Save and Test

- **Click:** Save (Ctrl+S)
- **Press:** F5 or Ctrl+F5
- **Result:** Kernel builds and QEMU launches!

---

## ? **This Works Immediately!**

No need to:
- ? Reload solution
- ? Delete .vs folder
- ? Wait for IntelliSense
- ? Configure launch profiles

Just set the external program and **F5 works!**

---

## ??? **Visual Guide**

```
Solution Explorer
?? guideXOSServer (right-click)
   ?? Properties
      ?? Debug
         ?? General
            ?? Launch: [Executable        ?]
            ?? Executable: [Browse...      ] ? Click here
            ?                                  Select RUN-KERNEL.bat
            ?? Working Directory: [        ]
                                    ?
                              Project directory
```

---

## ?? **Alternative: Use Post-Build Event**

Make it **automatic** after every build:

### Steps:

1. Project Properties ? **Build Events**
2. **Post-build event command line:**

```cmd
cd "$(ProjectDir)kernel"
call build-x86.bat
if errorlevel 0 (
    start "" "$(ProjectDir)scripts\run-qemu-x86.bat"
)
```

3. **Press Ctrl+Shift+B** (Build)
4. **QEMU launches automatically!**

---

## ?? **Comparison**

| Method | Setup Time | Works Now? |
|--------|-----------|------------|
| Launch Profiles | 5+ min | ? Not showing |
| **External Program** | **2 min** | **? Yes!** |
| **Post-Build Event** | **2 min** | **? Yes!** |
| Batch File | 0 min | ? Yes! |
| Tools Menu | 3 min | ? Yes! |

---

## ?? **Bottom Line**

**Stop waiting for launch profiles!**

Use **External Program** or **Post-Build Event** and F5 works RIGHT NOW!

---

## ?? **Complete Configuration**

### For External Program:

1. **Executable:** `$(ProjectDir)RUN-KERNEL.bat`
2. **Arguments:** (leave empty)
3. **Working Directory:** `$(ProjectDir)`
4. **Save** ? **Press F5** ? **Done!**

### For Post-Build:

1. **Post-build command:** See above
2. **Save** ? **Build** ? **Done!**

---

**Pick one method and F5 will work in 2 minutes!** ??
