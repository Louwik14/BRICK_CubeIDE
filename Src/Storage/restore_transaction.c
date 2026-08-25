#include "Storage/restore_transaction.h"

#include "Storage/cache_maintenance.h"
#include "Storage/restore_audio_commit.h"
#include "stm32h7xx.h"

static uint32_t restore_transaction_next_seq(uint32_t previous)
{
    ++previous;
    return (previous != 0U) ? previous : 1U;
}

void restore_transaction_control_init(void)
{
    g_restore_plan_mailbox.request_seq = 0U;
    g_restore_plan_mailbox.completed_seq = 0U;
    g_restore_plan_mailbox.state = RESTORE_TRANSACTION_IDLE;
    g_restore_plan_mailbox.result = RESTORE_RESULT_NONE;
    g_restore_plan_mailbox.plan_crc32 = 0U;
    g_restore_plan_mailbox.plan_bytes = 0U;
    for (uint8_t i = 0U; i < 10U; ++i)
        g_restore_plan_mailbox.reserved[i] = 0U;
    __DMB();
}

uint8_t restore_transaction_control_publish(uint32_t *out_request_seq)
{
    if ((out_request_seq == NULL)
            || (g_restore_plan_mailbox.state != RESTORE_TRANSACTION_IDLE)
            || (g_restore_plan_mailbox.completed_seq
                != g_restore_plan_mailbox.request_seq)
            || (restore_audio_commit_validate(&g_restore_audio_plan) == 0U))
        return 0U;

    const uint32_t request = restore_transaction_next_seq(
        g_restore_plan_mailbox.request_seq);
    g_restore_audio_plan.header.request_seq = request;
    dcache_clean_by_addr_aligned(&g_restore_audio_plan,
                                 sizeof(g_restore_audio_plan));
    __DMB();
    g_restore_plan_mailbox.plan_crc32 =
        g_restore_audio_plan.header.payload_crc32;
    g_restore_plan_mailbox.plan_bytes = sizeof(g_restore_audio_plan);
    g_restore_plan_mailbox.result = RESTORE_RESULT_NONE;
    g_restore_plan_mailbox.request_seq = request;
    __DMB();
    g_restore_plan_mailbox.state = RESTORE_TRANSACTION_PREPARED;
    __DMB();
    *out_request_seq = request;
    return 1U;
}

uint8_t restore_transaction_audio_service(void)
{
    if (g_restore_plan_mailbox.state != RESTORE_TRANSACTION_PREPARED)
        return 0U;
    const uint32_t request = g_restore_plan_mailbox.request_seq;
    g_restore_plan_mailbox.state = RESTORE_TRANSACTION_COMMITTING;
    __DMB();
    dcache_invalidate_by_addr_aligned(&g_restore_audio_plan,
                                      sizeof(g_restore_audio_plan));
    __DMB();
    uint8_t valid = (uint8_t)((request != 0U)
        && (g_restore_audio_plan.header.request_seq == request)
        && (g_restore_plan_mailbox.plan_bytes == sizeof(g_restore_audio_plan))
        && (g_restore_plan_mailbox.plan_crc32
            == g_restore_audio_plan.header.payload_crc32));
    if (valid != 0U) valid = restore_audio_commit_apply(&g_restore_audio_plan);
    g_restore_plan_mailbox.result = (valid != 0U)
        ? RESTORE_RESULT_COMPLETE : RESTORE_RESULT_REJECTED_CONTRACT;
    __DMB();
    g_restore_plan_mailbox.completed_seq = request;
    __DMB();
    g_restore_plan_mailbox.state = RESTORE_TRANSACTION_IDLE;
    __DMB();
    return 1U;
}

uint8_t restore_transaction_control_completed(uint32_t request_seq,
                                               restore_result_t *out_result)
{
    if ((out_result == NULL) || (request_seq == 0U)
            || (g_restore_plan_mailbox.completed_seq != request_seq))
        return 0U;
    __DMB();
    *out_result = (restore_result_t)g_restore_plan_mailbox.result;
    return 1U;
}
