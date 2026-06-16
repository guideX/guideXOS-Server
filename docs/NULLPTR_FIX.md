# ? nullptr Fix - C++14 Compatibility

## Problem Solved

The `types.h` header was redefining `nullptr` as a macro, but C++11/C++14 have it as a built-in keyword.

---

## Error Message
```
error: invalid conversion from 'void*' to 'uint32_t*'
note: in expansion of macro 'nullptr'
```

## Root Cause

Old code:
```cpp
#define nullptr NULL
#define NULL ((void*)0)
```

This made `nullptr` expand to `(void*)0`, which can't implicitly convert to typed pointers in C++.

---

## Solution

**Before:**
```cpp
#define NULL ((void*)0)
#define nullptr NULL
```

**After:**
```cpp
#ifdef __cplusplus
    #define NULL 0           // C++ uses 0
#else
    #define NULL ((void*)0)  // C uses void*
#endif

// Don't redefine nullptr - C++11+ has it built-in
```

---

## Why This Works

### C++11/C++14 `nullptr`
- Built-in keyword (like `true`, `false`)
- Type: `std::nullptr_t`
- Converts to any pointer type safely
- No macro needed!

### Our Fix
- Don't redefine `nullptr` at all
- Let C++14 use its built-in version
- Define `NULL` properly for C++ (as `0`)

---

## Build Again

```bash
cd kernel
build-x86.bat
```

**Expected:**
```
[4/7] Compiling framebuffer.cpp...
[OK] framebuffer.o created
```

---

## ? Ready to Build!

The kernel will now compile successfully with proper C++14 compatibility!
