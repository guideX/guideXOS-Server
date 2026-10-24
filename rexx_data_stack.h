#ifndef GUIDEXOS_REXX_DATA_STACK_H
#define GUIDEXOS_REXX_DATA_STACK_H

/*
 * Bounded, VM-owned REXX external data stack. Elements are byte strings and
 * may contain NULs. The queue stores its own copy on insertion. A removed
 * item owns its former queue allocation until rexx_data_stack_item_clear().
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REXX_DATA_STACK_MAX_ELEMENTS ((uint32_t)4096u)
#define REXX_DATA_STACK_MAX_ELEMENT_BYTES ((size_t)16777216u)
#define REXX_DATA_STACK_MAX_TOTAL_BYTES ((size_t)16777216u)

typedef void *(*RexxDataStackAllocate)(void *context, size_t size);
typedef void (*RexxDataStackDeallocate)(void *context, void *allocation);

typedef enum RexxDataStackStatus {
    REXX_DATA_STACK_OK = 0,
    REXX_DATA_STACK_INVALID_ARGUMENT,
    REXX_DATA_STACK_OUT_OF_MEMORY,
    REXX_DATA_STACK_RESOURCE_LIMIT,
    REXX_DATA_STACK_EMPTY,
    REXX_DATA_STACK_INVALID_STATE
} RexxDataStackStatus;

typedef struct RexxDataStack {
    void *head;
    void *tail;
    uint32_t count;
    size_t total_bytes;
    RexxDataStackAllocate allocate;
    RexxDataStackDeallocate deallocate;
    void *allocator_context;
    uint32_t initialized;
} RexxDataStack;

#define REXX_DATA_STACK_INIT { 0 }

typedef struct RexxDataStackItem {
    unsigned char *bytes;
    size_t length;
    void *storage;
    RexxDataStackDeallocate deallocate;
    void *allocator_context;
} RexxDataStackItem;

#define REXX_DATA_STACK_ITEM_INIT { 0 }

/* The stack must be zero-initialized or destroyed before it is initialized. */
RexxDataStackStatus rexx_data_stack_init(
    RexxDataStack *stack, RexxDataStackAllocate allocate,
    RexxDataStackDeallocate deallocate, void *allocator_context);
void rexx_data_stack_destroy(RexxDataStack *stack);

RexxDataStackStatus rexx_data_stack_push(RexxDataStack *stack,
                                         const void *bytes, size_t length);
RexxDataStackStatus rexx_data_stack_queue(RexxDataStack *stack,
                                         const void *bytes, size_t length);
RexxDataStackStatus rexx_data_stack_take_head(RexxDataStack *stack,
                                              RexxDataStackItem *item);
void rexx_data_stack_item_clear(RexxDataStackItem *item);

RexxDataStackStatus rexx_data_stack_get_count(const RexxDataStack *stack,
                                              uint32_t *count_out);
RexxDataStackStatus rexx_data_stack_peek(const RexxDataStack *stack,
                                         uint32_t offset,
                                         const unsigned char **bytes_out,
                                         size_t *length_out);

#ifdef __cplusplus
}
#endif

#endif /* GUIDEXOS_REXX_DATA_STACK_H */
