// intrinsics.cpp
// Provide memcpy and memset implementations for UEFI bootloader
// Required because we build with /NODEFAULTLIB

extern "C" {

// Simple byte-by-byte copy implementation
void* __cdecl memcpy(void* dest, const void* src, unsigned long long count) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    while (count--) {
        *d++ = *s++;
    }
    return dest;
}

// Simple byte-by-byte set implementation
void* __cdecl memset(void* dest, int value, unsigned long long count) {
    unsigned char* d = (unsigned char*)dest;
    unsigned char val = (unsigned char)value;
    while (count--) {
        *d++ = val;
    }
    return dest;
}

} // extern "C"
