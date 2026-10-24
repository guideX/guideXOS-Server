#include "rexx_data_stack.h"

#include <string.h>

#define REXX_DATA_STACK_INITIALIZED ((uint32_t)0x52584453u)

typedef struct RexxDataStackNode {
    struct RexxDataStackNode *previous;
    struct RexxDataStackNode *next;
    size_t length;
    unsigned char bytes[];
} RexxDataStackNode;

static int stack_is_initialized(const RexxDataStack *stack)
{
    return stack != NULL &&
           stack->initialized == REXX_DATA_STACK_INITIALIZED &&
           stack->allocate != NULL && stack->deallocate != NULL;
}

RexxDataStackStatus rexx_data_stack_init(
    RexxDataStack *stack, RexxDataStackAllocate allocate,
    RexxDataStackDeallocate deallocate, void *allocator_context)
{
    if (stack == NULL || allocate == NULL || deallocate == NULL)
        return REXX_DATA_STACK_INVALID_ARGUMENT;
    memset(stack, 0, sizeof(*stack));
    stack->allocate = allocate;
    stack->deallocate = deallocate;
    stack->allocator_context = allocator_context;
    stack->initialized = REXX_DATA_STACK_INITIALIZED;
    return REXX_DATA_STACK_OK;
}

void rexx_data_stack_destroy(RexxDataStack *stack)
{
    RexxDataStackNode *node;
    RexxDataStackNode *next;
    RexxDataStackDeallocate deallocate;
    void *allocator_context;

    if (stack == NULL) return;
    deallocate = stack->deallocate;
    allocator_context = stack->allocator_context;
    node = (RexxDataStackNode *)stack->head;
    while (node != NULL) {
        next = node->next;
        if (deallocate != NULL) deallocate(allocator_context, node);
        node = next;
    }
    memset(stack, 0, sizeof(*stack));
}

static RexxDataStackStatus insert(RexxDataStack *stack,
                                  const void *bytes,
                                  size_t length,
                                  int at_head)
{
    RexxDataStackNode *node;
    size_t allocation_size;

    if (!stack_is_initialized(stack) || (bytes == NULL && length != 0u))
        return REXX_DATA_STACK_INVALID_ARGUMENT;
    if (stack->count > REXX_DATA_STACK_MAX_ELEMENTS ||
        stack->total_bytes > REXX_DATA_STACK_MAX_TOTAL_BYTES ||
        (stack->head == NULL) != (stack->tail == NULL) ||
        (stack->count == 0u) != (stack->head == NULL))
        return REXX_DATA_STACK_INVALID_STATE;
    if (length > REXX_DATA_STACK_MAX_ELEMENT_BYTES ||
        stack->count == REXX_DATA_STACK_MAX_ELEMENTS ||
        length > REXX_DATA_STACK_MAX_TOTAL_BYTES - stack->total_bytes)
        return REXX_DATA_STACK_RESOURCE_LIMIT;
    if (length > (size_t)-1 - sizeof(*node))
        return REXX_DATA_STACK_RESOURCE_LIMIT;
    allocation_size = sizeof(*node) + length;
    node = (RexxDataStackNode *)stack->allocate(stack->allocator_context,
                                                 allocation_size);
    if (node == NULL) return REXX_DATA_STACK_OUT_OF_MEMORY;
    node->length = length;
    if (length != 0u) memcpy(node->bytes, bytes, length);

    if (at_head) {
        node->previous = NULL;
        node->next = (RexxDataStackNode *)stack->head;
        if (node->next != NULL) node->next->previous = node;
        else stack->tail = node;
        stack->head = node;
    } else {
        node->next = NULL;
        node->previous = (RexxDataStackNode *)stack->tail;
        if (node->previous != NULL) node->previous->next = node;
        else stack->head = node;
        stack->tail = node;
    }
    ++stack->count;
    stack->total_bytes += length;
    return REXX_DATA_STACK_OK;
}

RexxDataStackStatus rexx_data_stack_push(RexxDataStack *stack,
                                         const void *bytes, size_t length)
{
    return insert(stack, bytes, length, 1);
}

RexxDataStackStatus rexx_data_stack_queue(RexxDataStack *stack,
                                         const void *bytes, size_t length)
{
    return insert(stack, bytes, length, 0);
}

RexxDataStackStatus rexx_data_stack_take_head(RexxDataStack *stack,
                                              RexxDataStackItem *item)
{
    RexxDataStackNode *node;

    if (!stack_is_initialized(stack) || item == NULL)
        return REXX_DATA_STACK_INVALID_ARGUMENT;
    memset(item, 0, sizeof(*item));
    if (stack->count > REXX_DATA_STACK_MAX_ELEMENTS ||
        stack->total_bytes > REXX_DATA_STACK_MAX_TOTAL_BYTES ||
        (stack->head == NULL) != (stack->tail == NULL) ||
        (stack->count == 0u) != (stack->head == NULL))
        return REXX_DATA_STACK_INVALID_STATE;
    node = (RexxDataStackNode *)stack->head;
    if (node == NULL) return REXX_DATA_STACK_EMPTY;
    if (node->length > stack->total_bytes || stack->count == 0u)
        return REXX_DATA_STACK_INVALID_STATE;

    stack->head = node->next;
    if (stack->head != NULL)
        ((RexxDataStackNode *)stack->head)->previous = NULL;
    else
        stack->tail = NULL;
    --stack->count;
    stack->total_bytes -= node->length;

    item->bytes = node->bytes;
    item->length = node->length;
    item->storage = node;
    item->deallocate = stack->deallocate;
    item->allocator_context = stack->allocator_context;
    return REXX_DATA_STACK_OK;
}

void rexx_data_stack_item_clear(RexxDataStackItem *item)
{
    if (item == NULL) return;
    if (item->storage != NULL && item->deallocate != NULL)
        item->deallocate(item->allocator_context, item->storage);
    memset(item, 0, sizeof(*item));
}

RexxDataStackStatus rexx_data_stack_get_count(const RexxDataStack *stack,
                                              uint32_t *count_out)
{
    if (!stack_is_initialized(stack) || count_out == NULL)
        return REXX_DATA_STACK_INVALID_ARGUMENT;
    if (stack->count > REXX_DATA_STACK_MAX_ELEMENTS ||
        stack->total_bytes > REXX_DATA_STACK_MAX_TOTAL_BYTES ||
        (stack->head == NULL) != (stack->tail == NULL) ||
        (stack->count == 0u) != (stack->head == NULL))
        return REXX_DATA_STACK_INVALID_STATE;
    *count_out = stack->count;
    return REXX_DATA_STACK_OK;
}

RexxDataStackStatus rexx_data_stack_peek(const RexxDataStack *stack,
                                         uint32_t offset,
                                         const unsigned char **bytes_out,
                                         size_t *length_out)
{
    const RexxDataStackNode *node;

    if (!stack_is_initialized(stack) || bytes_out == NULL ||
        length_out == NULL)
        return REXX_DATA_STACK_INVALID_ARGUMENT;
    if (stack->count > REXX_DATA_STACK_MAX_ELEMENTS ||
        stack->total_bytes > REXX_DATA_STACK_MAX_TOTAL_BYTES ||
        (stack->head == NULL) != (stack->tail == NULL) ||
        (stack->count == 0u) != (stack->head == NULL))
        return REXX_DATA_STACK_INVALID_STATE;
    if (offset >= stack->count) return REXX_DATA_STACK_EMPTY;
    node = (const RexxDataStackNode *)stack->head;
    while (offset != 0u && node != NULL) {
        node = node->next;
        --offset;
    }
    if (node == NULL) return REXX_DATA_STACK_INVALID_STATE;
    *bytes_out = node->bytes;
    *length_out = node->length;
    return REXX_DATA_STACK_OK;
}
