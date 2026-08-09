#include "Sampler/sample_stream_transport.h"

#include <string.h>

#include "Storage/memory_layout.h"
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
} sample_stream_transport_mailbox_t;

SDRAM_STREAM_SERVICE static sample_stream_transport_mailbox_t
    g_sample_stream_transport_mailbox;
static sample_stream_transport_stats_t g_sample_stream_transport_stats;

void sample_stream_transport_init(void)
{
    memset(&g_sample_stream_transport_mailbox, 0,
           sizeof(g_sample_stream_transport_mailbox));
    memset(&g_sample_stream_transport_stats, 0,
           sizeof(g_sample_stream_transport_stats));
    g_sample_stream_transport_mailbox.abi_version = SAMPLE_STREAM_TRANSPORT_ABI_VERSION;
    g_sample_stream_transport_stats.next_sequence = 1U;
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
    if (g_sample_stream_transport_mailbox.state != SAMPLE_STREAM_TRANSPORT_EMPTY)
    {
        g_sample_stream_transport_stats.busy_rejections++;
        return 0U;
    }

    uint32_t sequence = g_sample_stream_transport_stats.next_sequence++;
    if (sequence == 0U)
    {
        sequence = g_sample_stream_transport_stats.next_sequence++;
    }
    g_sample_stream_transport_mailbox.abi_version = SAMPLE_STREAM_TRANSPORT_ABI_VERSION;
    g_sample_stream_transport_mailbox.sequence = sequence;
    g_sample_stream_transport_mailbox.command = *command;
    __DMB();
    g_sample_stream_transport_mailbox.state = SAMPLE_STREAM_TRANSPORT_COMMAND_READY;
    __DMB();
    g_sample_stream_transport_stats.submitted++;
    *out_sequence = sequence;
    return 1U;
}

void sample_stream_transport_worker_poll(void)
{
    __DMB();
    if (g_sample_stream_transport_mailbox.state == SAMPLE_STREAM_TRANSPORT_IO_ACTIVE)
    {
        if (sample_stream_io_poll(&g_sample_stream_transport_mailbox.result) == 0U)
        {
            return;
        }
        __DMB();
        g_sample_stream_transport_mailbox.state = SAMPLE_STREAM_TRANSPORT_RESULT_READY;
        __DMB();
        return;
    }
    if (g_sample_stream_transport_mailbox.state != SAMPLE_STREAM_TRANSPORT_COMMAND_READY)
    {
        return;
    }
    memset(&g_sample_stream_transport_mailbox.result, 0,
           sizeof(g_sample_stream_transport_mailbox.result));
    if (g_sample_stream_transport_mailbox.abi_version
        != SAMPLE_STREAM_TRANSPORT_ABI_VERSION)
    {
        g_sample_stream_transport_stats.protocol_errors++;
        g_sample_stream_transport_mailbox.result.token =
            g_sample_stream_transport_mailbox.command.token;
        g_sample_stream_transport_mailbox.result.load_result =
            SAMPLE_PAGE_LOAD_INVALID_ARG;
    }
    else
    {
        if (sample_stream_io_begin(&g_sample_stream_transport_mailbox.command) == 0U)
        {
            g_sample_stream_transport_mailbox.result.token =
                g_sample_stream_transport_mailbox.command.token;
            g_sample_stream_transport_mailbox.result.load_result =
                SAMPLE_PAGE_LOAD_INVALID_ARG;
        }
        else
        {
            g_sample_stream_transport_mailbox.state = SAMPLE_STREAM_TRANSPORT_IO_ACTIVE;
            __DMB();
            if (sample_stream_io_poll(&g_sample_stream_transport_mailbox.result) == 0U)
            {
                return;
            }
        }
    }
    __DMB();
    g_sample_stream_transport_mailbox.state = SAMPLE_STREAM_TRANSPORT_RESULT_READY;
    __DMB();
}

uint8_t sample_stream_transport_take_result(uint32_t expected_sequence,
                                            sample_stream_io_result_t *out_result)
{
    if (out_result == 0)
    {
        return 0U;
    }
    __DMB();
    if ((g_sample_stream_transport_mailbox.state
         != SAMPLE_STREAM_TRANSPORT_RESULT_READY)
        || (g_sample_stream_transport_mailbox.sequence != expected_sequence))
    {
        return 0U;
    }
    *out_result = g_sample_stream_transport_mailbox.result;
    g_sample_stream_transport_stats.completed_sequence = expected_sequence;
    g_sample_stream_transport_stats.completed++;
    __DMB();
    g_sample_stream_transport_mailbox.state = SAMPLE_STREAM_TRANSPORT_EMPTY;
    __DMB();
    return 1U;
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
    }
}
