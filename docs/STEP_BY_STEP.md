# ?? Boot Your OS - Step by Step

## You Are Here: `scripts\` directory

---

## Step 1: Go Back to Root

```bash
cd ..
```

You should now be in: `D:\devgitlab\guideXOS\guideXOSServer\`

---

## Step 2: Go to Kernel Directory

```bash
cd kernel
```

You should now be in: `D:\devgitlab\guideXOS\guideXOSServer\kernel\`

---

## Step 3: Build the Kernel

```bash
build-x86.bat
```

Wait for it to complete. You should see:
```
==========================================
BUILD SUCCESSFUL!
==========================================
```

---

## Step 4: Go Back to Root

```bash
cd ..
```

You should now be in: `D:\devgitlab\guideXOS\guideXOSServer\`

---

## Step 5: Run Your OS!

```bash
scripts\run-qemu-x86.bat
```

**QEMU will open with your OS!** ??

---

## Or Do It All At Once

From the `scripts\` directory, run:

```bash
cd ..\kernel && build-x86.bat && cd .. && scripts\run-qemu-x86.bat
```

---

## Visual Guide

```
Where you are now:
guideXOSServer\scripts\   ? You are here
                ?
        cd ..   (go up one level)
                ?
guideXOSServer\           ? Now here
                ?
        cd kernel
                ?
guideXOSServer\kernel\    ? Build here
                ?
        build-x86.bat
                ?
        cd ..   (go back to root)
                ?
guideXOSServer\           ? Back to root
                ?
        scripts\run-qemu-x86.bat
                ?
        ?? OS BOOTS! ??
```

---

**Copy these commands one by one:**

```bash
cd ..
cd kernel
build-x86.bat
cd ..
scripts\run-qemu-x86.bat
```

**Or copy this single command:**

```bash
cd ..\kernel && build-x86.bat && cd .. && scripts\run-qemu-x86.bat
```

**Done!** ??
