#include "Storage/monkey_test_log.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "Core/crash_capsule.h"
#include "Storage/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "ff.h"
#include "stm32h7xx_hal.h"

#define MONKEY_TEST_LOG_DIR             "0:/BRICK/DIAG"
#define MONKEY_TEST_LOG_PATH            "0:/BRICK/DIAG/MONKEY.LOG"
#define MONKEY_TEST_LOG_OLD_PATH        "0:/BRICK/DIAG/MONKEY.OLD"
#define MONKEY_TEST_LOG_MAX_BYTES       (256UL * 1024UL)
#define MONKEY_TEST_LOG_RETRY_MS        5000U
#define MONKEY_TEST_LOG_LINE_BYTES      320U

#if defined(DEBUG)
#define MONKEY_TEST_LOG_BUILD_TYPE "Debug"
#else
#define MONKEY_TEST_LOG_BUILD_TYPE "Test"
#endif

#if defined(BRICK6_VARIANT_PREMIUM)
#define MONKEY_TEST_LOG_VARIANT "Premium"
#else
#define MONKEY_TEST_LOG_VARIANT "LowCost"
#endif

typedef struct
{
    monkey_test_log_event_t type;
    uint32_t seed;
    uint32_t elapsed_ms;
    uint32_t action_count;
    uint32_t warning_count;
    uint32_t error_count;
    uint32_t crash_count;
    uint32_t last_issue;
} monkey_test_log_event_record_t;

STORAGE_STATE_SDRAM static crash_capsule_snapshot_t g_recovery;
STORAGE_STATE_SDRAM static monkey_test_log_event_record_t g_event;
STORAGE_STATE_SDRAM static monkey_test_log_status_t g_status;
STORAGE_STATE_SDRAM static FIL g_file;
STORAGE_STATE_SDRAM static FILINFO g_file_info;
static uint32_t g_retry_after_ms;
STORAGE_SCRATCH_SDRAM static char g_line[MONKEY_TEST_LOG_LINE_BYTES];
STORAGE_SCRATCH_SDRAM static char g_tail[513];

static uint8_t monkey_test_log_result_ok_or_exists(FRESULT result)
{
    return ((result == FR_OK) || (result == FR_EXIST)) ? 1U : 0U;
}

static uint8_t monkey_test_log_write(FIL *file, const char *text)
{
    const size_t length = (text != NULL) ? strlen(text) : 0U;
    UINT written = 0U;
    return ((file != NULL) && (length != 0U)
            && (length <= (size_t)UINT_MAX)
            && (f_write(file, text, (UINT)length, &written) == FR_OK)
            && (written == (UINT)length)) ? 1U : 0U;
}

static uint8_t monkey_test_log_writef(FIL *file, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(g_line, sizeof(g_line), format, args);
    va_end(args);
    if ((length <= 0) || ((uint32_t)length >= sizeof(g_line)))
    {
        return 0U;
    }
    return monkey_test_log_write(file, g_line);
}

static uint8_t monkey_test_log_prepare_directory(void)
{
    if (monkey_test_log_result_ok_or_exists(f_mkdir("0:/BRICK")) == 0U)
    {
        return 0U;
    }
    return monkey_test_log_result_ok_or_exists(
        f_mkdir(MONKEY_TEST_LOG_DIR));
}

static uint8_t monkey_test_log_rotate_if_needed(void)
{
    const FRESULT stat_result =
        f_stat(MONKEY_TEST_LOG_PATH, &g_file_info);
    if (stat_result == FR_NO_FILE)
    {
        return 1U;
    }
    if (stat_result != FR_OK)
    {
        return 0U;
    }
    if (g_file_info.fsize < MONKEY_TEST_LOG_MAX_BYTES)
    {
        return 1U;
    }

    const FRESULT unlink_result = f_unlink(MONKEY_TEST_LOG_OLD_PATH);
    if ((unlink_result != FR_OK) && (unlink_result != FR_NO_FILE))
    {
        return 0U;
    }
    return (f_rename(MONKEY_TEST_LOG_PATH,
                     MONKEY_TEST_LOG_OLD_PATH) == FR_OK) ? 1U : 0U;
}

static uint8_t monkey_test_log_open_append(void)
{
    if ((monkey_test_log_prepare_directory() == 0U)
        || (monkey_test_log_rotate_if_needed() == 0U)
        || (f_open(&g_file, MONKEY_TEST_LOG_PATH,
                   FA_OPEN_ALWAYS | FA_WRITE) != FR_OK))
    {
        return 0U;
    }
    if (f_lseek(&g_file, f_size(&g_file)) != FR_OK)
    {
        (void)f_close(&g_file);
        return 0U;
    }
    return 1U;
}

static uint32_t monkey_test_log_report_id(
    const crash_capsule_snapshot_t *snapshot)
{
    uint32_t value = 2166136261UL;
    const uint32_t words[] = {
        snapshot->seed, snapshot->action_index, snapshot->logical_tick,
        snapshot->reset_flags, snapshot->fault_type, snapshot->pc,
        snapshot->lr, snapshot->cfsr, snapshot->hfsr
    };
    for (uint32_t index = 0U;
         index < (uint32_t)(sizeof(words) / sizeof(words[0])); ++index)
    {
        value ^= words[index];
        value *= 16777619UL;
    }
    return value;
}

static uint8_t monkey_test_log_file_has_report(const char *path,
                                               uint32_t report_id)
{
    if (f_open(&g_file, path, FA_READ) != FR_OK)
    {
        return 0U;
    }
    const FSIZE_t size = f_size(&g_file);
    const UINT amount = (size > 512U) ? 512U : (UINT)size;
    uint8_t found = 0U;
    if ((f_lseek(&g_file, size - amount) == FR_OK) && (amount != 0U))
    {
        UINT read = 0U;
        if ((f_read(&g_file, g_tail, amount, &read) == FR_OK)
            && (read == amount))
        {
            g_tail[read] = '\0';
            (void)snprintf(g_line, sizeof(g_line), "END id=%08lX",
                           (unsigned long)report_id);
            found = (strstr(g_tail, g_line) != NULL) ? 1U : 0U;
        }
    }
    (void)f_close(&g_file);
    return found;
}

static uint8_t monkey_test_log_append_recovery(void)
{
    const uint32_t report_id = monkey_test_log_report_id(&g_recovery);
    if ((monkey_test_log_file_has_report(MONKEY_TEST_LOG_PATH,
                                         report_id) != 0U)
        || (monkey_test_log_file_has_report(MONKEY_TEST_LOG_OLD_PATH,
                                            report_id) != 0U))
    {
        return 1U;
    }

    if (monkey_test_log_open_append() == 0U)
    {
        return 0U;
    }

    const crash_capsule_snapshot_t *snapshot = &g_recovery;
    uint8_t ok = monkey_test_log_writef(
        &g_file,
        "BEGIN id=%08lX schema=1 build=%s variant=%s firmware=\"%s %s\"\r\n",
        (unsigned long)report_id, MONKEY_TEST_LOG_BUILD_TYPE,
        MONKEY_TEST_LOG_VARIANT, __DATE__, __TIME__);
    ok &= monkey_test_log_writef(
        &g_file,
        "CRASH seed=%08lX action=%lu tick=%lu fault=%lu reset=%08lX "
        "warnings=%lu errors=%lu crashes=%lu\r\n",
        (unsigned long)snapshot->seed,
        (unsigned long)snapshot->action_index,
        (unsigned long)snapshot->logical_tick,
        (unsigned long)snapshot->fault_type,
        (unsigned long)snapshot->reset_flags,
        (unsigned long)snapshot->warning_count,
        (unsigned long)snapshot->error_count,
        (unsigned long)snapshot->crash_count);
    ok &= monkey_test_log_writef(
        &g_file,
        "CORE pc=%08lX lr=%08lX sp=%08lX xpsr=%08lX exc=%08lX "
        "r0=%08lX r1=%08lX r2=%08lX r3=%08lX r12=%08lX\r\n",
        (unsigned long)snapshot->pc, (unsigned long)snapshot->lr,
        (unsigned long)snapshot->sp, (unsigned long)snapshot->xpsr,
        (unsigned long)snapshot->exc_return,
        (unsigned long)snapshot->r0, (unsigned long)snapshot->r1,
        (unsigned long)snapshot->r2, (unsigned long)snapshot->r3,
        (unsigned long)snapshot->r12);
    ok &= monkey_test_log_writef(
        &g_file,
        "FAULT cfsr=%08lX hfsr=%08lX dfsr=%08lX afsr=%08lX "
        "bfar=%08lX mmfar=%08lX icsr=%08lX shcsr=%08lX\r\n",
        (unsigned long)snapshot->cfsr, (unsigned long)snapshot->hfsr,
        (unsigned long)snapshot->dfsr, (unsigned long)snapshot->afsr,
        (unsigned long)snapshot->bfar, (unsigned long)snapshot->mmfar,
        (unsigned long)snapshot->icsr, (unsigned long)snapshot->shcsr);
    ok &= monkey_test_log_writef(
        &g_file,
        "HEARTBEAT armed=%lu count=%lu engine_tick=%lu breadcrumbs=%lu\r\n",
        (unsigned long)snapshot->watchdog_armed,
        (unsigned long)snapshot->heartbeat_count,
        (unsigned long)snapshot->heartbeat_engine_tick,
        (unsigned long)snapshot->breadcrumb_count);

    const uint32_t count =
        (snapshot->breadcrumb_count <= CRASH_CAPSULE_BREADCRUMB_CAPACITY)
            ? snapshot->breadcrumb_count
            : CRASH_CAPSULE_BREADCRUMB_CAPACITY;
    const uint32_t oldest =
        (count == CRASH_CAPSULE_BREADCRUMB_CAPACITY)
            ? snapshot->breadcrumb_write_index : 0U;
    for (uint32_t offset = 0U; (offset < count) && (ok != 0U); ++offset)
    {
        const uint32_t slot =
            (oldest + offset) % CRASH_CAPSULE_BREADCRUMB_CAPACITY;
        const crash_capsule_breadcrumb_t *breadcrumb =
            &snapshot->breadcrumbs[slot];
        ok &= monkey_test_log_writef(
            &g_file,
            "ACTION index=%lu tick=%lu delay=%lu type=%u target=%u value=%d\r\n",
            (unsigned long)breadcrumb->index,
            (unsigned long)breadcrumb->logical_tick,
            (unsigned long)breadcrumb->delay_ticks,
            (unsigned)breadcrumb->type, (unsigned)breadcrumb->target,
            (int)breadcrumb->value);
    }
    ok &= monkey_test_log_writef(&g_file, "END id=%08lX\r\n",
                                 (unsigned long)report_id);
    if ((ok != 0U) && (f_sync(&g_file) != FR_OK))
    {
        ok = 0U;
    }
    if (f_close(&g_file) != FR_OK)
    {
        ok = 0U;
    }
    return ok;
}

static const char *monkey_test_log_event_label(monkey_test_log_event_t event)
{
    switch (event)
    {
        case MONKEY_TEST_LOG_EVENT_START:
            return "START";
        case MONKEY_TEST_LOG_EVENT_PERIODIC:
            return "PERIODIC";
        case MONKEY_TEST_LOG_EVENT_STOP:
            return "STOP";
        case MONKEY_TEST_LOG_EVENT_REPLAY_START:
            return "REPLAY_START";
        case MONKEY_TEST_LOG_EVENT_REPLAY_TARGET:
            return "REPLAY_TARGET";
        default:
            return "UNKNOWN";
    }
}

static uint8_t monkey_test_log_append_event(void)
{
    if (monkey_test_log_open_append() == 0U)
    {
        return 0U;
    }
    uint8_t ok = monkey_test_log_writef(
        &g_file,
        "SESSION event=%s build=%s variant=%s seed=%08lX elapsed_ms=%lu "
        "actions=%lu warnings=%lu errors=%lu crashes=%lu issue=%lu\r\n",
        monkey_test_log_event_label(g_event.type),
        MONKEY_TEST_LOG_BUILD_TYPE, MONKEY_TEST_LOG_VARIANT,
        (unsigned long)g_event.seed, (unsigned long)g_event.elapsed_ms,
        (unsigned long)g_event.action_count,
        (unsigned long)g_event.warning_count,
        (unsigned long)g_event.error_count,
        (unsigned long)g_event.crash_count,
        (unsigned long)g_event.last_issue);
    if ((ok != 0U) && (f_sync(&g_file) != FR_OK))
    {
        ok = 0U;
    }
    if (f_close(&g_file) != FR_OK)
    {
        ok = 0U;
    }
    return ok;
}

void monkey_test_log_init(void)
{
    memset(&g_recovery, 0, sizeof(g_recovery));
    memset(&g_event, 0, sizeof(g_event));
    memset(&g_status, 0, sizeof(g_status));
    g_retry_after_ms = 0U;
    if ((crash_capsule_get_recovery(&g_recovery) != 0U)
        && (g_recovery.session_state == CRASH_CAPSULE_SESSION_FAULTED))
    {
        g_status.recovery_pending = 1U;
    }
}

void monkey_test_log_queue_event(monkey_test_log_event_t event,
                                 uint32_t seed,
                                 uint32_t elapsed_ms,
                                 uint32_t action_count,
                                 uint32_t warning_count,
                                 uint32_t error_count,
                                 uint32_t crash_count,
                                 uint32_t last_issue)
{
    g_event.type = event;
    g_event.seed = seed;
    g_event.elapsed_ms = elapsed_ms;
    g_event.action_count = action_count;
    g_event.warning_count = warning_count;
    g_event.error_count = error_count;
    g_event.crash_count = crash_count;
    g_event.last_issue = last_issue;
    g_status.event_pending = 1U;
}

void monkey_test_log_service(void)
{
    if ((g_status.recovery_pending == 0U)
        && (g_status.event_pending == 0U))
    {
        return;
    }
    const uint32_t now_ms = HAL_GetTick();
    if ((int32_t)(now_ms - g_retry_after_ms) < 0)
    {
        return;
    }
    if (sd_access_gate_try_acquire(
            SD_ACCESS_CLIENT_DIAGNOSTIC_LOG) == 0U)
    {
        return;
    }

    uint8_t ok = 0U;
    if (sd_access_fs_mount_if_needed() != 0U)
    {
        if (g_status.recovery_pending != 0U)
        {
            ok = monkey_test_log_append_recovery();
            if ((ok != 0U)
                && (crash_capsule_recovery_mark_reported() != 0U))
            {
                g_status.recovery_pending = 0U;
            }
            else
            {
                ok = 0U;
            }
        }
        else
        {
            ok = monkey_test_log_append_event();
            if (ok != 0U)
            {
                g_status.event_pending = 0U;
            }
        }
    }
    if (ok == 0U)
    {
        sd_access_fs_invalidate_mount();
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_DIAGNOSTIC_LOG);

    if (ok != 0U)
    {
        g_status.write_count++;
        g_retry_after_ms = 0U;
    }
    else
    {
        g_status.error_count++;
        g_retry_after_ms = now_ms + MONKEY_TEST_LOG_RETRY_MS;
    }
}

void monkey_test_log_get_status(monkey_test_log_status_t *out_status)
{
    if (out_status != NULL)
    {
        *out_status = g_status;
    }
}
