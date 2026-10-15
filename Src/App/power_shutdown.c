#include "App/power_shutdown.h"

#include "Audio/audio.h"
#include "Board/board_power.h"
#include "Seq/seq_runtime.h"
#include "Storage/project_load_quiesce.h"
#include "Storage/project_product.h"
#include "Storage/sd_access_gate.h"
#include "drv_display.h"

#define POWER_SHUTDOWN_DEBUG_MAGIC UINT32_C(0x50575244)
#define POWER_SHUTDOWN_DEBUG_VERSION 1U

_Static_assert(sizeof(power_shutdown_debug_t) == 64U,
               "POWER shutdown debug ABI changed");

static power_shutdown_phase_t g_shutdown_phase;
static uint8_t g_shutdown_quiesce_owned;
volatile power_shutdown_debug_t g_power_shutdown_debug;

static void power_shutdown_debug_init(void)
{
    if ((g_power_shutdown_debug.magic == POWER_SHUTDOWN_DEBUG_MAGIC)
        && (g_power_shutdown_debug.version
            == POWER_SHUTDOWN_DEBUG_VERSION)) return;
    g_power_shutdown_debug = (power_shutdown_debug_t){
        .magic = POWER_SHUTDOWN_DEBUG_MAGIC,
        .version = POWER_SHUTDOWN_DEBUG_VERSION,
        .current_phase = POWER_SHUTDOWN_IDLE,
        .last_phase_reached = POWER_SHUTDOWN_IDLE,
    };
}

static void power_shutdown_set_phase(power_shutdown_phase_t phase)
{
    g_shutdown_phase = phase;
    g_power_shutdown_debug.current_phase = (uint32_t)phase;
    if ((phase != POWER_SHUTDOWN_IDLE)
        && (phase != POWER_SHUTDOWN_FAILED))
        g_power_shutdown_debug.last_phase_reached = (uint32_t)phase;
}

static void power_shutdown_debug_refresh(void)
{
    resource_mutation_quiesce_debug_t quiesce;
    resource_mutation_quiesce_debug_snapshot(&quiesce);
    g_power_shutdown_debug.current_phase = (uint32_t)g_shutdown_phase;
    g_power_shutdown_debug.last_storage_status =
        (uint32_t)sd_access_storage_status();
    g_power_shutdown_debug.quiesce_requested = quiesce.requested;
    g_power_shutdown_debug.quiesce_panic_published =
        quiesce.panic_published;
    g_power_shutdown_debug.quiesce_audio_acknowledged =
        quiesce.panic_acknowledged;
    g_power_shutdown_debug.quiesce_reservations_cancelled =
        quiesce.reservations_cancelled;
}

static void power_shutdown_abort(power_shutdown_failure_t reason)
{
    g_power_shutdown_debug.failure_phase = (uint32_t)g_shutdown_phase;
    g_power_shutdown_debug.failure_reason = (uint32_t)reason;
    g_power_shutdown_debug.abort_count++;
    g_power_shutdown_debug.last_storage_status =
        (uint32_t)sd_access_storage_status();
    if ((reason == POWER_SHUTDOWN_FAILURE_RESUME_BEGIN)
        || (reason == POWER_SHUTDOWN_FAILURE_RESUME_SAVE))
    {
        g_power_shutdown_debug.resume_error =
            (uint32_t)project_product_save_last_error();
        g_power_shutdown_debug.resume_detail =
            project_product_save_last_detail();
    }
    else
    {
        g_power_shutdown_debug.resume_error = 0U;
        g_power_shutdown_debug.resume_detail = 0;
    }
    if (g_shutdown_quiesce_owned != 0U)
    {
        resource_mutation_quiesce_end();
        g_shutdown_quiesce_owned = 0U;
    }
    resource_mutation_ingress_open();
    power_shutdown_set_phase(POWER_SHUTDOWN_FAILED);
}

uint8_t power_shutdown_service(uint32_t now_ms)
{
    power_shutdown_debug_init();
    if ((g_shutdown_phase >= POWER_SHUTDOWN_WAIT_STORAGE)
        && (g_shutdown_phase <= POWER_SHUTDOWN_FINAL))
        power_shutdown_debug_refresh();
    if (g_shutdown_phase == POWER_SHUTDOWN_IDLE)
    {
        if (board_power_shutdown_request_poll(now_ms) == 0U) return 0U;
        g_power_shutdown_debug.request_count++;
        g_power_shutdown_debug.resume_result = POWER_SHUTDOWN_RESUME_NONE;
        g_power_shutdown_debug.quiesce_requested = 0U;
        g_power_shutdown_debug.quiesce_panic_published = 0U;
        g_power_shutdown_debug.quiesce_audio_acknowledged = 0U;
        g_power_shutdown_debug.quiesce_reservations_cancelled = 0U;
        resource_mutation_ingress_close();
        seq_runtime_stop();
        power_shutdown_set_phase(POWER_SHUTDOWN_WAIT_STORAGE);
        return 0U;
    }
    if (g_shutdown_phase == POWER_SHUTDOWN_WAIT_STORAGE)
    {
        if (project_product_save_busy() != 0U)
        {
            (void)project_product_save_take_result(NULL, NULL);
            return 0U;
        }
        if (sd_access_storage_status() == SD_STORAGE_STATUS_NO_MEDIA
            || sd_access_storage_status() == SD_STORAGE_STATUS_FAULT)
        {
            power_shutdown_abort((sd_access_storage_status()
                    == SD_STORAGE_STATUS_NO_MEDIA)
                ? POWER_SHUTDOWN_FAILURE_STORAGE_NO_MEDIA
                : POWER_SHUTDOWN_FAILURE_STORAGE_FAULT);
            return 0U;
        }
        if ((project_product_load_busy() != 0U)
            || (project_product_rename_busy() != 0U)) return 0U;
        if (project_load_allowed() == 0U) return 0U;
        resource_mutation_quiesce_request();
        g_shutdown_quiesce_owned = 1U;
        power_shutdown_set_phase(POWER_SHUTDOWN_WAIT_QUIESCE);
        return 0U;
    }
    if (g_shutdown_phase == POWER_SHUTDOWN_WAIT_QUIESCE)
    {
        if (resource_mutation_quiesce_safe() == 0U) return 0U;
        const project_product_resume_begin_result_t begin =
            project_product_resume_save_begin();
        if (begin == PROJECT_PRODUCT_RESUME_BEGIN_STARTED)
        {
            g_power_shutdown_debug.resume_result =
                POWER_SHUTDOWN_RESUME_STARTED;
            power_shutdown_set_phase(POWER_SHUTDOWN_CAPTURE_PATTERN);
        }
        else if (begin == PROJECT_PRODUCT_RESUME_BEGIN_FAILED)
        {
            g_power_shutdown_debug.resume_result =
                POWER_SHUTDOWN_RESUME_FAILED;
            power_shutdown_abort(POWER_SHUTDOWN_FAILURE_RESUME_BEGIN);
        }
        return 0U;
    }
    if (g_shutdown_phase == POWER_SHUTDOWN_CAPTURE_PATTERN
        || g_shutdown_phase == POWER_SHUTDOWN_WRITE_RESUME)
    {
        uint8_t success=0U;
        power_shutdown_set_phase(POWER_SHUTDOWN_WRITE_RESUME);
        if (project_product_resume_save_take_result(&success) == 0U) return 0U;
        if (success == 0U)
        {
            g_power_shutdown_debug.resume_result =
                POWER_SHUTDOWN_RESUME_FAILED;
            power_shutdown_abort(POWER_SHUTDOWN_FAILURE_RESUME_SAVE);
            return 0U;
        }
        g_power_shutdown_debug.resume_result =
            POWER_SHUTDOWN_RESUME_SUCCEEDED;
        power_shutdown_set_phase(POWER_SHUTDOWN_FINAL);
    }
    if (g_shutdown_phase == POWER_SHUTDOWN_FINAL)
    {
        audio_stop();
        drv_display_off();
        board_power_usb_host_off();
        board_power_shutdown_cut();
        return 1U;
    }
    if (g_shutdown_phase == POWER_SHUTDOWN_FAILED)
    {
        (void)board_power_shutdown_request_poll(now_ms);
        if (board_power_shutdown_button_released() != 0U)
            power_shutdown_set_phase(POWER_SHUTDOWN_IDLE);
    }
    return 0U;
}

power_shutdown_phase_t power_shutdown_phase(void){return g_shutdown_phase;}
power_shutdown_failure_t power_shutdown_failure(void)
{
    return (power_shutdown_failure_t)g_power_shutdown_debug.failure_reason;
}
uint8_t power_shutdown_mutations_frozen(void)
{
    return (uint8_t)(g_shutdown_phase >= POWER_SHUTDOWN_WAIT_STORAGE
        && g_shutdown_phase <= POWER_SHUTDOWN_FINAL);
}
