#include "Sampler/sample_stream_transport.h"

#include <string.h>
#include <stddef.h>

#include "SD/sd_scheduler_runtime.h"
#include "Platform/memory_layout.h"
#include "stm32h7xx.h"

typedef enum
{
    SAMPLE_STREAM_TRANSPORT_EMPTY = 0,
    SAMPLE_STREAM_TRANSPORT_COMMAND_READY,
    SAMPLE_STREAM_TRANSPORT_IO_ACTIVE,
    SAMPLE_STREAM_TRANSPORT_RESULT_READY
} sample_stream_transport_state_t;

typedef struct
{
    volatile uint32_t state;
    uint32_t abi_version;
    uint32_t sequence;
    sample_stream_io_command_t command;
    sample_stream_io_result_t result;
    ALIGN32 uint8_t decoded_page[SAMPLE_PAGE_BYTES];
} ALIGN32 sample_stream_transport_mailbox_t;

#define SAMPLE_STREAM_TRANSPORT_MAILBOX_COUNT (2U)

SDRAM_STREAM_SERVICE static sample_stream_transport_mailbox_t
    g_sample_stream_transport_mailbox[SAMPLE_STREAM_TRANSPORT_MAILBOX_COUNT];
static ALIGN32 sample_stream_transport_stats_t g_sample_stream_transport_stats;
static ALIGN32 volatile uint32_t g_sample_stream_transport_worker_protocol_errors;

#define SAMPLE_STREAM_TRANSPORT_RELEASE_CAPACITY (16U)
typedef struct
{
    ALIGN32 sample_audio_key_t keys[SAMPLE_STREAM_TRANSPORT_RELEASE_CAPACITY];
    ALIGN32 volatile uint32_t head;
    ALIGN32 volatile uint32_t tail;
} ALIGN32 sample_stream_transport_release_queue_t;
SDRAM_STREAM_SERVICE static sample_stream_transport_release_queue_t
    g_sample_stream_transport_release_queue;

static void sample_stream_transport_clean(const volatile void *address, uint32_t bytes)
{
#if (__DCACHE_PRESENT == 1U)
    const uintptr_t begin = (uintptr_t)address & ~(uintptr_t)31U;
    const uintptr_t end = ((uintptr_t)address + bytes + 31U) & ~(uintptr_t)31U;
    SCB_CleanDCache_by_Addr((uint32_t *)begin, (int32_t)(end - begin));
#else
    (void)address; (void)bytes;
#endif
    __DMB();
}

static void sample_stream_transport_invalidate(const volatile void *address, uint32_t bytes)
{
#if (__DCACHE_PRESENT == 1U)
    const uintptr_t begin = (uintptr_t)address & ~(uintptr_t)31U;
    const uintptr_t end = ((uintptr_t)address + bytes + 31U) & ~(uintptr_t)31U;
    SCB_InvalidateDCache_by_Addr((uint32_t *)begin, (int32_t)(end - begin));
#else
    (void)address; (void)bytes;
#endif
    __DMB();
}

void sample_stream_transport_init(void)
{
    memset(g_sample_stream_transport_mailbox, 0,
           sizeof(g_sample_stream_transport_mailbox));
    memset(&g_sample_stream_transport_stats, 0,
           sizeof(g_sample_stream_transport_stats));
    g_sample_stream_transport_worker_protocol_errors = 0U;
    memset(&g_sample_stream_transport_release_queue, 0,
           sizeof(g_sample_stream_transport_release_queue));
    for (uint32_t i = 0U; i < SAMPLE_STREAM_TRANSPORT_MAILBOX_COUNT; ++i)
    {
        g_sample_stream_transport_mailbox[i].abi_version =
            SAMPLE_STREAM_TRANSPORT_ABI_VERSION;
    }
    g_sample_stream_transport_stats.next_sequence = 1U;
    sample_stream_transport_clean(g_sample_stream_transport_mailbox,
                                  sizeof(g_sample_stream_transport_mailbox));
    sample_stream_transport_clean(&g_sample_stream_transport_release_queue,
                                  sizeof(g_sample_stream_transport_release_queue));
    sample_stream_transport_clean(&g_sample_stream_transport_worker_protocol_errors,
                                  sizeof(g_sample_stream_transport_worker_protocol_errors));
    __DMB();
}

uint8_t sample_stream_transport_submit(const sample_stream_io_command_t *command,
                                       uint32_t *out_sequence)
{
    if ((command == 0) || (out_sequence == 0))
    {
        return 0U;
    }
    __DMB();
    sample_stream_transport_mailbox_t *mailbox = 0;
    for (uint32_t i = 0U; i < SAMPLE_STREAM_TRANSPORT_MAILBOX_COUNT; ++i)
    {
        sample_stream_transport_invalidate(&g_sample_stream_transport_mailbox[i].state,
                                           sizeof(uint32_t));
        if (g_sample_stream_transport_mailbox[i].state == SAMPLE_STREAM_TRANSPORT_EMPTY)
        {
            mailbox = &g_sample_stream_transport_mailbox[i];
            break;
        }
    }
    if (mailbox == 0)
    {
        g_sample_stream_transport_stats.busy_rejections++;
        return 0U;
    }

    uint32_t sequence = g_sample_stream_transport_stats.next_sequence++;
    if (sequence == 0U)
    {
        sequence = g_sample_stream_transport_stats.next_sequence++;
    }
    mailbox->abi_version = SAMPLE_STREAM_TRANSPORT_ABI_VERSION;
    mailbox->sequence = sequence;
    mailbox->command = *command;
    sample_stream_transport_clean(mailbox, (uint32_t)offsetof(
        sample_stream_transport_mailbox_t, decoded_page));
    __DMB();
    mailbox->state = SAMPLE_STREAM_TRANSPORT_COMMAND_READY;
    sample_stream_transport_clean(&mailbox->state, sizeof(mailbox->state));
    __DMB();
    g_sample_stream_transport_stats.submitted++;
    *out_sequence = sequence;
    return 1U;
}

uint8_t sample_stream_transport_can_submit(void)
{
    __DMB();
    for (uint32_t i = 0U; i < SAMPLE_STREAM_TRANSPORT_MAILBOX_COUNT; ++i)
    {
        sample_stream_transport_invalidate(&g_sample_stream_transport_mailbox[i].state,
                                           sizeof(uint32_t));
        if (g_sample_stream_transport_mailbox[i].state
                == SAMPLE_STREAM_TRANSPORT_EMPTY)
        {
            return 1U;
        }
    }
    return 0U;
}

void sample_stream_transport_worker_poll(void)
{
    sample_stream_transport_invalidate((const void *)&g_sample_stream_transport_release_queue.head,
                                       sizeof(g_sample_stream_transport_release_queue.head));
    while (g_sample_stream_transport_release_queue.tail
           != g_sample_stream_transport_release_queue.head)
    {
        const uint32_t tail = g_sample_stream_transport_release_queue.tail;
        sample_stream_transport_invalidate(
            &g_sample_stream_transport_release_queue.keys[tail],
            sizeof(g_sample_stream_transport_release_queue.keys[tail]));
        sample_stream_io_release_key(g_sample_stream_transport_release_queue.keys[tail]);
        g_sample_stream_transport_release_queue.tail =
            (tail + 1U) % SAMPLE_STREAM_TRANSPORT_RELEASE_CAPACITY;
        sample_stream_transport_clean(
            (const void *)&g_sample_stream_transport_release_queue.tail,
            sizeof(g_sample_stream_transport_release_queue.tail));
    }
    __DMB();
    sample_stream_transport_mailbox_t *ready = 0;
    sample_stream_transport_mailbox_t *active = 0;
    for (uint32_t i = 0U; i < SAMPLE_STREAM_TRANSPORT_MAILBOX_COUNT; ++i)
    {
        sample_stream_transport_mailbox_t *const mailbox =
            &g_sample_stream_transport_mailbox[i];
        sample_stream_transport_invalidate(&mailbox->state, sizeof(mailbox->state));
        if ((mailbox->state == SAMPLE_STREAM_TRANSPORT_COMMAND_READY)
            && ((ready == 0) || (mailbox->sequence < ready->sequence)))
        {
            ready = mailbox;
        }
        if ((mailbox->state == SAMPLE_STREAM_TRANSPORT_IO_ACTIVE)
            && ((active == 0) || (mailbox->sequence < active->sequence)))
        {
            active = mailbox;
        }
    }

    if (ready != 0)
        sample_stream_transport_invalidate(ready, (uint32_t)offsetof(
            sample_stream_transport_mailbox_t, decoded_page));
    if ((ready != 0) && (ready->abi_version != SAMPLE_STREAM_TRANSPORT_ABI_VERSION))
    {
        g_sample_stream_transport_worker_protocol_errors++;
        sample_stream_transport_clean(&g_sample_stream_transport_worker_protocol_errors,
                                      sizeof(g_sample_stream_transport_worker_protocol_errors));
        ready->result.token = ready->command.token;
        ready->result.load_result = SAMPLE_PAGE_LOAD_INVALID_ARG;
        sample_stream_transport_clean(&ready->result, sizeof(ready->result));
        ready->state = SAMPLE_STREAM_TRANSPORT_RESULT_READY;
        sample_stream_transport_clean(&ready->state, sizeof(ready->state));
        ready = 0;
    }
    if ((ready != 0) && (active == 0))
    {
        memset(&ready->result, 0, sizeof(ready->result));
        if (sample_stream_io_begin_to(&ready->command,
                                      (float *)ready->decoded_page,
                                      sizeof(ready->decoded_page)) != 0U)
        {
            ready->state = SAMPLE_STREAM_TRANSPORT_IO_ACTIVE;
            sample_stream_transport_clean(&ready->state, sizeof(ready->state));
            if ((active == 0) || (ready->sequence < active->sequence))
            {
                active = ready;
            }
        }
    }

    if (active != 0)
    {
        sample_stream_io_result_t result;
        if (sample_stream_io_poll(&result) != 0U)
        {
            active->result = result;
            sample_stream_transport_clean(active->decoded_page,
                                          sizeof(active->decoded_page));
            sample_stream_transport_clean(&active->result, sizeof(active->result));
            active->state = SAMPLE_STREAM_TRANSPORT_RESULT_READY;
            sample_stream_transport_clean(&active->state, sizeof(active->state));
            __DMB();
        }
    }
}

uint8_t sample_stream_transport_take_result(uint32_t expected_sequence,
                                            sample_stream_io_result_t *out_result)
{
    if (out_result == 0)
    {
        return 0U;
    }
    __DMB();
    sample_stream_transport_mailbox_t *mailbox = 0;
    for (uint32_t i = 0U; i < SAMPLE_STREAM_TRANSPORT_MAILBOX_COUNT; ++i)
    {
        sample_stream_transport_invalidate(&g_sample_stream_transport_mailbox[i].state,
                                           sizeof(uint32_t));
        if ((g_sample_stream_transport_mailbox[i].state
             == SAMPLE_STREAM_TRANSPORT_RESULT_READY)
            && (g_sample_stream_transport_mailbox[i].sequence == expected_sequence))
        {
            mailbox = &g_sample_stream_transport_mailbox[i];
            break;
        }
    }
    if (mailbox == 0)
    {
        return 0U;
    }
    sample_stream_transport_invalidate(&mailbox->result, sizeof(mailbox->result));
    *out_result = mailbox->result;
    if (out_result->load_result == SAMPLE_PAGE_LOAD_OK)
    {
        sample_page_load_target_t target;
        const uint32_t decoded_bytes = mailbox->command.target.frame_count
            * mailbox->command.target.stride_floats * sizeof(float);
        sample_stream_transport_invalidate(mailbox->decoded_page,
                                           sizeof(mailbox->decoded_page));
        if ((decoded_bytes > sizeof(mailbox->decoded_page))
            || (sample_page_cache_resolve_loading_target(
                    &out_result->token, &target) == 0U)
            || (target.slot_index != mailbox->command.target.slot_index)
            || (target.page_generation != mailbox->command.target.page_generation)
            || (target.registration_epoch != mailbox->command.target.registration_epoch)
            || (target.frame_count != mailbox->command.target.frame_count)
            || (target.stride_floats != mailbox->command.target.stride_floats))
        {
            out_result->load_result = SAMPLE_PAGE_LOAD_INVALID_ARG;
            g_sample_stream_transport_stats.stale_completions++;
        }
        else
        {
            memcpy(target.frames_interleaved, mailbox->decoded_page, decoded_bytes);
            g_sample_stream_transport_stats.payload_bytes += decoded_bytes;
        }
    }
    g_sample_stream_transport_stats.completed_sequence = expected_sequence;
    g_sample_stream_transport_stats.completed++;
    __DMB();
    memset(mailbox, 0, sizeof(*mailbox));
    mailbox->abi_version = SAMPLE_STREAM_TRANSPORT_ABI_VERSION;
    sample_stream_transport_clean(mailbox, (uint32_t)offsetof(
        sample_stream_transport_mailbox_t, decoded_page));
    __DMB();
    return 1U;
}

uint8_t sample_stream_transport_request_release(sample_audio_key_t key)
{
    sample_stream_transport_invalidate((const void *)&g_sample_stream_transport_release_queue.tail,
                                       sizeof(g_sample_stream_transport_release_queue.tail));
    const uint32_t head = g_sample_stream_transport_release_queue.head;
    const uint32_t next = (head + 1U) % SAMPLE_STREAM_TRANSPORT_RELEASE_CAPACITY;
    if (next == g_sample_stream_transport_release_queue.tail) return 0U;
    g_sample_stream_transport_release_queue.keys[head] = key;
    sample_stream_transport_clean(&g_sample_stream_transport_release_queue.keys[head],
                                  sizeof(g_sample_stream_transport_release_queue.keys[head]));
    g_sample_stream_transport_release_queue.head = next;
    sample_stream_transport_clean((const void *)&g_sample_stream_transport_release_queue.head,
                                  sizeof(g_sample_stream_transport_release_queue.head));
    return 1U;
}

void sample_stream_transport_release_map(
    const sample_stream_physical_map_t *map)
{
    if (map == NULL) return;
    sample_stream_physical_map_t local = *map;
    sample_stream_physical_map_release(&local);
}

void sample_stream_transport_reset_storage_maps(void)
{
    sample_stream_physical_map_pool_reset();
}

void sample_stream_transport_execute_monocore(const sample_stream_io_command_t *command,
                                              sample_stream_io_result_t *out_result)
{
    if (out_result == 0)
    {
        return;
    }
    memset(out_result, 0, sizeof(*out_result));
    out_result->load_result = SAMPLE_PAGE_LOAD_INVALID_ARG;
    if (command == 0)
    {
        return;
    }
    out_result->token = command->token;
    uint32_t sequence = 0U;
    if (sample_stream_transport_submit(command, &sequence) == 0U)
    {
        return;
    }
    do
    {
        sd_scheduler_runtime_service();
        sample_stream_transport_worker_poll();
    } while (sample_stream_transport_take_result(sequence, out_result) == 0U);
    if (out_result->token.page_generation != command->token.page_generation)
    {
        g_sample_stream_transport_stats.protocol_errors++;
        out_result->token = command->token;
        out_result->load_result = SAMPLE_PAGE_LOAD_INVALID_ARG;
    }
}

void sample_stream_transport_get_stats(sample_stream_transport_stats_t *out_stats)
{
    if (out_stats != 0)
    {
        *out_stats = g_sample_stream_transport_stats;
        sample_stream_transport_invalidate(&g_sample_stream_transport_worker_protocol_errors,
                                           sizeof(g_sample_stream_transport_worker_protocol_errors));
        out_stats->protocol_errors += g_sample_stream_transport_worker_protocol_errors;
    }
}
