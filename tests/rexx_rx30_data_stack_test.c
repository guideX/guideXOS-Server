#include "../rexx_data_stack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROPERTY_SEED ((uint32_t)0x52583330u)
#define PROPERTY_OPERATIONS ((size_t)10000u)
#define MODEL_CAPACITY ((size_t)32u)
#define MODEL_ITEM_CAPACITY ((size_t)48u)

typedef struct TestAllocator {
    size_t fail_on;
    size_t attempts;
    size_t live_blocks;
} TestAllocator;

typedef struct ModelItem {
    unsigned char bytes[MODEL_ITEM_CAPACITY];
    size_t length;
} ModelItem;

static size_t assertions;
static size_t failures;

static void check(int condition, const char *message)
{
    ++assertions;
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL: %s\n", message);
}

static void *test_allocate(void *context, size_t size)
{
    TestAllocator *allocator = (TestAllocator *)context;
    void *allocation;
    ++allocator->attempts;
    if (allocator->fail_on != 0u &&
        allocator->attempts == allocator->fail_on) return NULL;
    allocation = malloc(size);
    if (allocation != NULL) ++allocator->live_blocks;
    return allocation;
}

static void test_deallocate(void *context, void *allocation)
{
    TestAllocator *allocator = (TestAllocator *)context;
    if (allocation == NULL) return;
    if (allocator->live_blocks != 0u) --allocator->live_blocks;
    free(allocation);
}

static int init_stack(RexxDataStack *stack, TestAllocator *allocator)
{
    return rexx_data_stack_init(stack, test_allocate, test_deallocate,
                                allocator) == REXX_DATA_STACK_OK;
}

static void expect_count(const RexxDataStack *stack, uint32_t expected,
                         const char *message)
{
    uint32_t actual = UINT32_MAX;
    RexxDataStackStatus status = rexx_data_stack_get_count(stack, &actual);
    check(status == REXX_DATA_STACK_OK && actual == expected, message);
}

static void expect_take(RexxDataStack *stack, const void *expected,
                        size_t expected_length, const char *message)
{
    RexxDataStackItem item = REXX_DATA_STACK_ITEM_INIT;
    RexxDataStackStatus status = rexx_data_stack_take_head(stack, &item);
    int equal = status == REXX_DATA_STACK_OK &&
                item.length == expected_length &&
                (expected_length == 0u ||
                 memcmp(item.bytes, expected, expected_length) == 0);
    check(equal, message);
    rexx_data_stack_item_clear(&item);
}

static uint32_t next_random(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static void model_push(ModelItem *items, size_t *count,
                       const unsigned char *bytes, size_t length)
{
    size_t index = *count;
    while (index != 0u) {
        items[index] = items[index - 1u];
        --index;
    }
    if (length != 0u) memcpy(items[0].bytes, bytes, length);
    items[0].length = length;
    ++*count;
}

static void model_queue(ModelItem *items, size_t *count,
                        const unsigned char *bytes, size_t length)
{
    if (length != 0u) memcpy(items[*count].bytes, bytes, length);
    items[*count].length = length;
    ++*count;
}

static void model_take(ModelItem *items, size_t *count)
{
    size_t index;
    for (index = 1u; index < *count; ++index)
        items[index - 1u] = items[index];
    --*count;
}

static void check_model(const RexxDataStack *stack, const ModelItem *model,
                        size_t model_count, size_t operation)
{
    uint32_t count = UINT32_MAX;
    size_t index;
    RexxDataStackStatus status = rexx_data_stack_get_count(stack, &count);
    check(status == REXX_DATA_STACK_OK && count == model_count,
          "property count agrees with independent reference deque");
    for (index = 0u; index < model_count; ++index) {
        const unsigned char *bytes = NULL;
        size_t length = 0u;
        status = rexx_data_stack_peek(stack, (uint32_t)index,
                                     &bytes, &length);
        check(status == REXX_DATA_STACK_OK &&
                  length == model[index].length &&
                  (length == 0u ||
                   memcmp(bytes, model[index].bytes, length) == 0),
              "property contents and ordering agree with reference deque");
    }
    (void)operation;
}

static void test_basic_order_and_binary(void)
{
    TestAllocator allocator = {0u, 0u, 0u};
    RexxDataStack stack = REXX_DATA_STACK_INIT;
    RexxDataStackItem item = REXX_DATA_STACK_ITEM_INIT;
    static const unsigned char nul_bytes[] = { 'A', 0u, 'b', 0x80u, 0xffu };
    static const unsigned char empty[] = "";

    check(init_stack(&stack, &allocator), "empty initialization succeeds");
    expect_count(&stack, 0u, "new stack count is zero");
    check(rexx_data_stack_take_head(&stack, &item) == REXX_DATA_STACK_EMPTY,
          "empty removal reports empty");
    rexx_data_stack_item_clear(&item);

    check(rexx_data_stack_queue(&stack, "tail-a", 6u) == REXX_DATA_STACK_OK,
          "tail insertion succeeds");
    check(rexx_data_stack_queue(&stack, "tail-b", 6u) == REXX_DATA_STACK_OK,
          "second tail insertion succeeds");
    check(rexx_data_stack_push(&stack, "head-a", 6u) == REXX_DATA_STACK_OK,
          "head insertion succeeds");
    check(rexx_data_stack_push(&stack, "head-b", 6u) == REXX_DATA_STACK_OK,
          "second head insertion succeeds");
    expect_count(&stack, 4u, "mixed insertions increment count");
    expect_take(&stack, "head-b", 6u, "PUSH order is LIFO at the head");
    expect_take(&stack, "head-a", 6u, "head entries precede queued entries");
    expect_take(&stack, "tail-a", 6u, "QUEUE order is FIFO at the tail");
    expect_take(&stack, "tail-b", 6u, "tail FIFO is preserved");
    expect_count(&stack, 0u, "removals decrement count");

    check(rexx_data_stack_queue(&stack, empty, 0u) == REXX_DATA_STACK_OK,
          "zero-length data element can be queued");
    check(rexx_data_stack_queue(&stack, nul_bytes, sizeof(nul_bytes)) ==
              REXX_DATA_STACK_OK,
          "NUL and high-bit bytes can be queued");
    expect_count(&stack, 2u, "empty and binary entries count as elements");
    {
        const unsigned char *view = NULL;
        size_t length = SIZE_MAX;
        check(rexx_data_stack_peek(&stack, 0u, &view, &length) ==
                  REXX_DATA_STACK_OK && length == 0u,
              "empty element retains exact zero byte length");
        check(rexx_data_stack_peek(&stack, 1u, &view, &length) ==
                  REXX_DATA_STACK_OK && length == sizeof(nul_bytes) &&
                  memcmp(view, nul_bytes, sizeof(nul_bytes)) == 0,
              "embedded NUL and high-bit bytes are exact");
    }
    expect_take(&stack, empty, 0u, "empty element is removed distinctly");
    expect_take(&stack, nul_bytes, sizeof(nul_bytes),
                "binary entry is removed without string termination");
    rexx_data_stack_destroy(&stack);
    check(allocator.live_blocks == 0u, "destroy releases all queue nodes");
}

static void test_allocation_failure_and_isolation(void)
{
    TestAllocator first_allocator = {0u, 0u, 0u};
    TestAllocator second_allocator = {0u, 0u, 0u};
    RexxDataStack first = REXX_DATA_STACK_INIT;
    RexxDataStack second = REXX_DATA_STACK_INIT;
    const unsigned char *view = NULL;
    size_t length = 0u;

    check(init_stack(&first, &first_allocator), "first queue initializes");
    check(init_stack(&second, &second_allocator), "second queue initializes");
    check(rexx_data_stack_queue(&first, "kept", 4u) == REXX_DATA_STACK_OK,
          "first queue receives anchor");
    second_allocator.fail_on = 1u;
    check(rexx_data_stack_push(&second, "lost", 4u) ==
              REXX_DATA_STACK_OUT_OF_MEMORY,
          "injected allocation failure is reported");
    expect_count(&second, 0u, "failed insertion leaves queue empty");
    check(rexx_data_stack_peek(&first, 0u, &view, &length) ==
              REXX_DATA_STACK_OK && length == 4u &&
              memcmp(view, "kept", 4u) == 0,
          "allocation failure in one queue cannot affect another");
    second_allocator.fail_on = 0u;
    check(rexx_data_stack_push(&second, "recovered", 9u) ==
              REXX_DATA_STACK_OK,
          "queue accepts data after allocation recovery");
    expect_count(&first, 1u, "first VM queue retains its own count");
    expect_count(&second, 1u, "second VM queue has an independent count");
    rexx_data_stack_destroy(&first);
    rexx_data_stack_destroy(&second);
    check(first_allocator.live_blocks == 0u &&
              second_allocator.live_blocks == 0u,
          "isolated queue teardown releases both allocators");
}

static void test_resource_limits(void)
{
    TestAllocator allocator = {0u, 0u, 0u};
    RexxDataStack stack = REXX_DATA_STACK_INIT;
    unsigned char *large = NULL;
    uint32_t index;
    uint32_t count = 0u;
    const unsigned char *view = NULL;
    size_t length = 0u;

    check(init_stack(&stack, &allocator), "limit queue initializes");
    large = (unsigned char *)malloc(REXX_DATA_STACK_MAX_ELEMENT_BYTES);
    check(large != NULL, "large binary test buffer allocates");
    if (large != NULL) {
        for (index = 0u; index < REXX_DATA_STACK_MAX_ELEMENT_BYTES; ++index)
            large[index] = (unsigned char)((index * 131u) & 0xffu);
        check(rexx_data_stack_queue(&stack, large,
                                   REXX_DATA_STACK_MAX_ELEMENT_BYTES) ==
                  REXX_DATA_STACK_OK,
              "maximum permitted binary element inserts");
        check(rexx_data_stack_peek(&stack, 0u, &view, &length) ==
                  REXX_DATA_STACK_OK &&
                  length == REXX_DATA_STACK_MAX_ELEMENT_BYTES &&
                  memcmp(view, large, length) == 0,
              "large binary element keeps all bytes");
        check(rexx_data_stack_get_count(&stack, &count) ==
                  REXX_DATA_STACK_OK && count == 1u,
              "large entry contributes exactly one element");
        check(rexx_data_stack_queue(&stack, NULL, 0u) ==
                  REXX_DATA_STACK_OK,
              "zero-length item fits at aggregate byte limit");
        check(rexx_data_stack_queue(&stack, "x", 1u) ==
                  REXX_DATA_STACK_RESOURCE_LIMIT,
              "aggregate byte limit rejects additional data");
        expect_count(&stack, 2u, "aggregate limit rejection is atomic");
        free(large);
        large = NULL;
    }
    rexx_data_stack_destroy(&stack);
    check(allocator.live_blocks == 0u, "large queue teardown is clean");

    stack = (RexxDataStack)REXX_DATA_STACK_INIT;
    check(init_stack(&stack, &allocator), "element limit queue initializes");
    for (index = 0u; index < REXX_DATA_STACK_MAX_ELEMENTS; ++index) {
        RexxDataStackStatus status = rexx_data_stack_push(&stack, NULL, 0u);
        if (status != REXX_DATA_STACK_OK) break;
    }
    check(index == REXX_DATA_STACK_MAX_ELEMENTS,
          "maximum element count is reachable using empty strings");
    expect_count(&stack, REXX_DATA_STACK_MAX_ELEMENTS,
                 "empty entries count toward element ceiling");
    check(rexx_data_stack_queue(&stack, NULL, 0u) ==
              REXX_DATA_STACK_RESOURCE_LIMIT,
          "maximum element count rejects the next insertion");
    {
        RexxDataStackItem item = REXX_DATA_STACK_ITEM_INIT;
        check(rexx_data_stack_take_head(&stack, &item) == REXX_DATA_STACK_OK,
              "removal succeeds at count ceiling");
        rexx_data_stack_item_clear(&item);
    }
    expect_count(&stack, REXX_DATA_STACK_MAX_ELEMENTS - 1u,
                 "count ceiling removal updates authoritative count");
    rexx_data_stack_destroy(&stack);
    check(allocator.live_blocks == 0u, "count ceiling teardown is clean");
}

static void test_reference_model(void)
{
    TestAllocator allocator = {0u, 0u, 0u};
    RexxDataStack stack = REXX_DATA_STACK_INIT;
    ModelItem model[MODEL_CAPACITY];
    size_t model_count = 0u;
    uint32_t random_state = PROPERTY_SEED;
    size_t operation;

    memset(model, 0, sizeof(model));
    check(init_stack(&stack, &allocator), "property queue initializes");
    for (operation = 0u; operation < PROPERTY_OPERATIONS; ++operation) {
        uint32_t choice = next_random(&random_state) % 5u;
        if ((choice == 0u || choice == 1u) &&
            model_count < MODEL_CAPACITY) {
            unsigned char bytes[MODEL_ITEM_CAPACITY];
            size_t length = next_random(&random_state) %
                            (MODEL_ITEM_CAPACITY + 1u);
            size_t index;
            for (index = 0u; index < length; ++index)
                bytes[index] = (unsigned char)next_random(&random_state);
            if (choice == 0u) {
                check(rexx_data_stack_queue(&stack, bytes, length) ==
                          REXX_DATA_STACK_OK,
                      "property QUEUE insertion succeeds");
                model_queue(model, &model_count, bytes, length);
            } else {
                check(rexx_data_stack_push(&stack, bytes, length) ==
                          REXX_DATA_STACK_OK,
                      "property PUSH insertion succeeds");
                model_push(model, &model_count, bytes, length);
            }
        } else if (choice == 2u || choice == 3u ||
                   model_count == MODEL_CAPACITY) {
            RexxDataStackItem item = REXX_DATA_STACK_ITEM_INIT;
            RexxDataStackStatus status =
                rexx_data_stack_take_head(&stack, &item);
            if (model_count == 0u) {
                check(status == REXX_DATA_STACK_EMPTY,
                      "property empty PULL preserves empty state");
            } else {
                check(status == REXX_DATA_STACK_OK &&
                          item.length == model[0].length &&
                          (item.length == 0u ||
                           memcmp(item.bytes, model[0].bytes,
                                  item.length) == 0),
                      "property PULL matches independent deque head");
                model_take(model, &model_count);
            }
            rexx_data_stack_item_clear(&item);
        } else {
            uint32_t count = UINT32_MAX;
            check(rexx_data_stack_get_count(&stack, &count) ==
                      REXX_DATA_STACK_OK && count == model_count,
                  "property QUEUED count matches independent model");
        }
        check_model(&stack, model, model_count, operation);
    }
    while (model_count != 0u) {
        RexxDataStackItem item = REXX_DATA_STACK_ITEM_INIT;
        RexxDataStackStatus status = rexx_data_stack_take_head(&stack, &item);
        check(status == REXX_DATA_STACK_OK &&
                  item.length == model[0].length &&
                  (item.length == 0u ||
                   memcmp(item.bytes, model[0].bytes, item.length) == 0),
              "final property drain matches reference order");
        model_take(model, &model_count);
        rexx_data_stack_item_clear(&item);
        check_model(&stack, model, model_count, PROPERTY_OPERATIONS);
    }
    rexx_data_stack_destroy(&stack);
    check(allocator.live_blocks == 0u,
          "property queue teardown releases all allocations");
}

static void test_reinitialization(void)
{
    TestAllocator allocator = {0u, 0u, 0u};
    RexxDataStack stack = REXX_DATA_STACK_INIT;
    unsigned int cycle;
    for (cycle = 0u; cycle < 8u; ++cycle) {
        check(init_stack(&stack, &allocator),
              "stack initializes after zeroed state or destroy");
        check(rexx_data_stack_queue(&stack, "cycle", 5u) ==
                  REXX_DATA_STACK_OK,
              "reinitialized stack accepts data");
        rexx_data_stack_destroy(&stack);
        check(allocator.live_blocks == 0u,
              "repeated destroy releases each cycle allocation");
    }
}

int main(void)
{
    test_basic_order_and_binary();
    test_allocation_failure_and_isolation();
    test_resource_limits();
    test_reference_model();
    test_reinitialization();
    printf("RX30 data stack: seed=0x%08x operations=%lu assertions=%lu failures=%lu max_elements=%u max_element_bytes=%lu max_total_bytes=%lu\n",
           (unsigned int)PROPERTY_SEED,
           (unsigned long)PROPERTY_OPERATIONS,
           (unsigned long)assertions,
           (unsigned long)failures,
           (unsigned int)REXX_DATA_STACK_MAX_ELEMENTS,
           (unsigned long)REXX_DATA_STACK_MAX_ELEMENT_BYTES,
           (unsigned long)REXX_DATA_STACK_MAX_TOTAL_BYTES);
    return failures == 0u ? 0 : 1;
}
