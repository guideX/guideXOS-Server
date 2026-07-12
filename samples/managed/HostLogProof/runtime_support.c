void* memcpy(void* dest, const void* src, unsigned long long count)
{
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    unsigned long long i;
    for (i = 0; i < count; ++i) {
        d[i] = s[i];
    }
    return dest;
}

void* memset(void* dest, int value, unsigned long long count)
{
    unsigned char* d = (unsigned char*)dest;
    unsigned long long i;
    for (i = 0; i < count; ++i) {
        d[i] = (unsigned char)value;
    }
    return dest;
}

void* memmove(void* dest, const void* src, unsigned long long count)
{
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    unsigned long long i;

    if (d == s || count == 0) {
        return dest;
    }

    if (d < s) {
        for (i = 0; i < count; ++i) {
            d[i] = s[i];
        }
    } else {
        for (i = count; i > 0; --i) {
            d[i - 1] = s[i - 1];
        }
    }

    return dest;
}

int memcmp(const void* left, const void* right, unsigned long long count)
{
    const unsigned char* a = (const unsigned char*)left;
    const unsigned char* b = (const unsigned char*)right;
    unsigned long long i;
    for (i = 0; i < count; ++i) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

unsigned long long strlen(const char* s)
{
    unsigned long long length = 0;
    if (!s) return 0;
    while (s[length] != '\0') {
        ++length;
    }
    return length;
}

int strcmp(const char* a, const char* b)
{
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;

    while (*a != '\0' && *b != '\0' && *a == *b) {
        ++a;
        ++b;
    }

    return (unsigned char)(*a) - (unsigned char)(*b);
}

int strncmp(const char* a, const char* b, unsigned long long count)
{
    unsigned long long i;
    if (count == 0 || a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;

    for (i = 0; i < count; ++i) {
        const unsigned char left = (unsigned char)a[i];
        const unsigned char right = (unsigned char)b[i];
        if (left != right || left == '\0' || right == '\0') {
            return left - right;
        }
    }

    return 0;
}

char* strchr(const char* s, int c)
{
    const char target = (char)c;
    if (!s) return (char*)0;
    while (*s != '\0') {
        if (*s == target) return (char*)s;
        ++s;
    }
    return target == '\0' ? (char*)s : (char*)0;
}

char* strrchr(const char* s, int c)
{
    const char target = (char)c;
    const char* last = (const char*)0;
    if (!s) return (char*)0;
    while (*s != '\0') {
        if (*s == target) last = s;
        ++s;
    }
    if (target == '\0') return (char*)s;
    return (char*)last;
}

char* strstr(const char* haystack, const char* needle)
{
    if (!haystack || !needle) return (char*)0;
    if (*needle == '\0') return (char*)haystack;

    for (; *haystack != '\0'; ++haystack) {
        const char* a = haystack;
        const char* b = needle;
        while (*a != '\0' && *b != '\0' && *a == *b) {
            ++a;
            ++b;
        }
        if (*b == '\0') {
            return (char*)haystack;
        }
    }

    return (char*)0;
}

void _purecall(void)
{
    for (;;) {
    }
}

int __CxxFrameHandler4(void)
{
    return 0;
}

int __C_specific_handler(void)
{
    return 0;
}

int __vcrt_initialize(void)
{
    return 0;
}

int __vcrt_uninitialize(void)
{
    return 0;
}

int __vcrt_uninitialize_critical(void)
{
    return 0;
}

int __vcrt_thread_attach(void)
{
    return 0;
}

int __vcrt_thread_detach(void)
{
    return 0;
}

int __acrt_initialize(void)
{
    return 0;
}

int __acrt_uninitialize(void)
{
    return 0;
}

int __acrt_uninitialize_critical(void)
{
    return 0;
}

int __acrt_thread_attach(void)
{
    return 0;
}

int __acrt_thread_detach(void)
{
    return 0;
}

int _is_c_termination_complete(void)
{
    return 1;
}

void* __current_exception(void)
{
    return (void*)0;
}

void* __current_exception_context(void)
{
    return (void*)0;
}

void* __std_exception_copy(void)
{
    return (void*)0;
}

void __std_exception_destroy(void)
{
}

void _CxxThrowException(void)
{
    for (;;) {
    }
}
