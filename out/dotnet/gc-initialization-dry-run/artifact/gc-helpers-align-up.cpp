#include <stdint.h>

uintptr_t ALIGN_UP(uintptr_t value, uintptr_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}
