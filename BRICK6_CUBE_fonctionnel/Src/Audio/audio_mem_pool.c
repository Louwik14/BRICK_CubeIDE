#include "audio_mem_pool.h"

#include "Storage/memory_layout.h"

#define AUDIO_MEM_MIN_ALIGN 32u
#define AUDIO_MEM_MIN_ORDER 5u
#define AUDIO_MEM_MAX_ORDERS 24u
#define AUDIO_MEM_PREFIX_MAGIC 0xA0D10EEFu

#if ((AUDIO_MEM_FAST_BYTES & (AUDIO_MEM_FAST_BYTES - 1u)) != 0u)
#error "AUDIO_MEM_FAST_BYTES must be a power-of-two"
#endif

#if ((AUDIO_MEM_SLOW_BYTES & (AUDIO_MEM_SLOW_BYTES - 1u)) != 0u)
#error "AUDIO_MEM_SLOW_BYTES must be a power-of-two"
#endif

typedef struct buddy_block {
    struct buddy_block* next;
} buddy_block_t;

typedef struct {
    uint8_t order;
    uint8_t reserved[7];
} alloc_header_t;

typedef struct {
    uint32_t magic;
    uint32_t offset;
} alloc_prefix_t;

typedef struct {
    uint8_t* base;
    size_t size;
    uint8_t max_order;
    buddy_block_t* free_lists[AUDIO_MEM_MAX_ORDERS];
    size_t used_bytes;
} buddy_pool_t;

AUDIO_WARM static uint8_t g_fast_pool_mem[AUDIO_MEM_FAST_BYTES] __attribute__((aligned(AUDIO_MEM_MIN_ALIGN)));
AUDIO_COLD_SDRAM static uint8_t g_slow_pool_mem[AUDIO_MEM_SLOW_BYTES] __attribute__((aligned(AUDIO_MEM_MIN_ALIGN)));

static buddy_pool_t g_fast_pool;
static buddy_pool_t g_slow_pool;

static uint8_t pool_order_for_size(size_t size)
{
    uint8_t order = AUDIO_MEM_MIN_ORDER;
    size_t block = (size_t)1u << order;

    while (block < size)
    {
        ++order;
        block <<= 1u;
    }

    return order;
}

static size_t align_up_size(size_t value, size_t align)
{
    return (value + (align - 1u)) & ~(align - 1u);
}

static uint8_t* align_up_ptr(uint8_t* p, size_t align)
{
    uintptr_t v = (uintptr_t)p;
    v = (v + (align - 1u)) & ~(uintptr_t)(align - 1u);
    return (uint8_t*)v;
}

static void pool_push_block(buddy_pool_t* pool, uint8_t order, uint8_t* block_addr)
{
    buddy_block_t* block = (buddy_block_t*)block_addr;
    block->next = pool->free_lists[order];
    pool->free_lists[order] = block;
}

static uint8_t* pool_pop_block(buddy_pool_t* pool, uint8_t order)
{
    buddy_block_t* head = pool->free_lists[order];

    if (!head)
        return NULL;

    pool->free_lists[order] = head->next;
    return (uint8_t*)head;
}

static uint8_t* pool_remove_block(buddy_pool_t* pool, uint8_t order, uint8_t* addr)
{
    buddy_block_t* prev = NULL;
    buddy_block_t* cur = pool->free_lists[order];

    while (cur)
    {
        if ((uint8_t*)cur == addr)
        {
            if (prev)
                prev->next = cur->next;
            else
                pool->free_lists[order] = cur->next;

            return (uint8_t*)cur;
        }

        prev = cur;
        cur = cur->next;
    }

    return NULL;
}

static void pool_init(buddy_pool_t* pool, uint8_t* mem, size_t size)
{
    uint8_t i = 0u;

    pool->base = mem;
    pool->size = size;
    pool->max_order = pool_order_for_size(size);
    pool->used_bytes = 0u;

    for (i = 0u; i < AUDIO_MEM_MAX_ORDERS; ++i)
        pool->free_lists[i] = NULL;

    pool_push_block(pool, pool->max_order, pool->base);
}

static void* pool_alloc(buddy_pool_t* pool, size_t size, size_t align)
{
    uint8_t want_order = 0u;
    uint8_t order = 0u;
    size_t req_align = align;
    size_t need = 0u;
    uint8_t* block = NULL;
    alloc_header_t* header = NULL;
    uint8_t* user = NULL;
    alloc_prefix_t* prefix = NULL;

    if (size == 0u)
        return NULL;

    if (req_align < AUDIO_MEM_MIN_ALIGN)
        req_align = AUDIO_MEM_MIN_ALIGN;

    need = size + sizeof(alloc_header_t) + sizeof(alloc_prefix_t) + req_align;
    need = align_up_size(need, AUDIO_MEM_MIN_ALIGN);

    want_order = pool_order_for_size(need);
    if (want_order > pool->max_order)
        return NULL;

    order = want_order;
    while (order <= pool->max_order)
    {
        block = pool_pop_block(pool, order);
        if (block)
            break;
        ++order;
    }

    if (!block)
        return NULL;

    while (order > want_order)
    {
        uint8_t* buddy = NULL;
        --order;
        buddy = block + ((size_t)1u << order);
        pool_push_block(pool, order, buddy);
    }

    header = (alloc_header_t*)block;
    header->order = order;

    user = align_up_ptr(block + sizeof(alloc_header_t) + sizeof(alloc_prefix_t), req_align);
    prefix = (alloc_prefix_t*)(user - sizeof(alloc_prefix_t));
    prefix->magic = AUDIO_MEM_PREFIX_MAGIC;
    prefix->offset = (uint32_t)(user - block);

    pool->used_bytes += ((size_t)1u << order);
    return (void*)user;
}

static void pool_free(buddy_pool_t* pool, void* ptr)
{
    alloc_prefix_t* prefix = NULL;
    uint8_t* block = NULL;
    alloc_header_t* header = NULL;
    uint8_t order = 0u;

    if (!ptr)
        return;

    prefix = (alloc_prefix_t*)((uint8_t*)ptr - sizeof(alloc_prefix_t));
    if (prefix->magic != AUDIO_MEM_PREFIX_MAGIC)
        return;

    block = ((uint8_t*)ptr - prefix->offset);
    if (block < pool->base || block >= (pool->base + pool->size))
        return;

    header = (alloc_header_t*)block;
    order = header->order;
    if (order < AUDIO_MEM_MIN_ORDER || order > pool->max_order)
        return;

    if (pool->used_bytes >= ((size_t)1u << order))
        pool->used_bytes -= ((size_t)1u << order);
    else
        pool->used_bytes = 0u;

    while (order < pool->max_order)
    {
        uintptr_t rel = (uintptr_t)(block - pool->base);
        uint8_t* buddy = pool->base + (rel ^ ((uintptr_t)1u << order));

        if (!pool_remove_block(pool, order, buddy))
            break;

        if (buddy < block)
            block = buddy;

        ++order;
    }

    pool_push_block(pool, order, block);
}

void audio_mem_init(void)
{
    pool_init(&g_fast_pool, g_fast_pool_mem, sizeof(g_fast_pool_mem));
    pool_init(&g_slow_pool, g_slow_pool_mem, sizeof(g_slow_pool_mem));
}

void* audio_mem_alloc_fast(size_t size, size_t align)
{
    return pool_alloc(&g_fast_pool, size, align);
}

void audio_mem_free_fast(void* ptr)
{
    pool_free(&g_fast_pool, ptr);
}

void* audio_mem_alloc_slow(size_t size, size_t align)
{
    return pool_alloc(&g_slow_pool, size, align);
}

void audio_mem_free_slow(void* ptr)
{
    pool_free(&g_slow_pool, ptr);
}

size_t audio_mem_get_fast_used(void)
{
    return g_fast_pool.used_bytes;
}

size_t audio_mem_get_fast_free(void)
{
    return g_fast_pool.size - g_fast_pool.used_bytes;
}

size_t audio_mem_get_slow_used(void)
{
    return g_slow_pool.used_bytes;
}

size_t audio_mem_get_slow_free(void)
{
    return g_slow_pool.size - g_slow_pool.used_bytes;
}
