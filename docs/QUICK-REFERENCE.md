# ? QUICK REFERENCE - F5 to QEMU

## ?? **What You Need to Do**

### **Setup (First Time Only - 2 Minutes):**

```
1. Open Visual Studio 2022
2. Open: guideXOSServer.sln
3. Right-click solution ? Add ? Existing Project
4. Select: guideXOSKernel.vcxproj
5. Right-click guideXOSKernel ? Set as Startup Project
```

**Done!** ?

---

## ?? **Daily Usage:**

```
1. Open Visual Studio
2. Select "QEMU" from dropdown
3. Press F5 (or Ctrl+F5)
4. Kernel boots in QEMU!
```

**That's it!** ?

---

## ?? **Configurations**

| Select | F5 Does |
|--------|---------|
| **QEMU** | ? **Standard QEMU** |
| QEMU with USB | USB support |
| QEMU with network | Network card |
| VMware | VMware Player |
| VirtualBox | VirtualBox |
| Debug | Build only |
| Release | Build optimized |

---

## ?? **Shortcuts**

| Key | Action |
|-----|--------|
| **F5** | Build + Run |
| **Ctrl+F5** | Build + Run (faster) |
| **Shift+F5** | Stop VM |
| **Ctrl+Shift+B** | Build only |
| **Ctrl+Alt+F7** | Rebuild |

---

## ?? **Files**

| File | Do |
|------|------|
| `guideXOSKernel.vcxproj` | Main project (add to solution) |
| `RUN-KERNEL.bat` | Double-click to run |
| `kernel/build-x86.bat` | Build kernel manually |

---

## ?? **Problems?**

| Problem | Fix |
|---------|-----|
| Dropdown wrong | Set `guideXOSKernel` as startup |
| Can't find kernel | Run `kernel\build-x86.bat` |
| QEMU not found | Update path in `.vcxproj` |
| Build fails | See `TOOLCHAIN-SETUP.md` |

---

## ?? **Documentation**

| Need | Read |
|------|------|
| Full guide | **COMPLETE-SETUP-SUMMARY.md** |
| Quick setup | **SETUP-FINAL.md** |
| Why this works | **NATIVE-CPP-PROJECT.md** |
| Toolchain help | **TOOLCHAIN-SETUP.md** |
| All docs | **INDEX.md** |

---

## ? **Success Check**

Works if:
- [x] Dropdown shows "QEMU"
- [x] F5 builds kernel
- [x] QEMU window opens
- [x] Kernel displays colored text

---

**TL;DR:**
1. Add `guideXOSKernel.vcxproj` to solution
2. Set as startup project
3. Select "QEMU"
4. Press F5
5. Done! ??
