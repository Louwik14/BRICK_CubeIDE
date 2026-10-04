#include "App/power_shutdown.h"

#include "Audio/audio.h"
#include "Board/board_power.h"
#include "Seq/seq_runtime.h"
#include "Storage/project_load_quiesce.h"
#include "Storage/project_product.h"
#include "Storage/sd_access_gate.h"
#include "drv_display.h"

static power_shutdown_phase_t g_shutdown_phase;

uint8_t power_shutdown_service(uint32_t now_ms)
{
    if (g_shutdown_phase == POWER_SHUTDOWN_IDLE)
    {
        if (board_power_shutdown_request_poll(now_ms) == 0U) return 0U;
        resource_mutation_ingress_close();
        seq_runtime_stop();
        g_shutdown_phase = POWER_SHUTDOWN_WAIT_STORAGE;
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
            resource_mutation_ingress_open();
            g_shutdown_phase = POWER_SHUTDOWN_FAILED;
            return 0U;
        }
        if (project_product_resume_save_begin() != 0U)
            g_shutdown_phase = POWER_SHUTDOWN_CAPTURE_PATTERN;
        return 0U;
    }
    if (g_shutdown_phase == POWER_SHUTDOWN_CAPTURE_PATTERN
        || g_shutdown_phase == POWER_SHUTDOWN_WRITE_RESUME)
    {
        uint8_t success=0U;
        g_shutdown_phase = POWER_SHUTDOWN_WRITE_RESUME;
        if (project_product_resume_save_take_result(&success) == 0U) return 0U;
        if (success == 0U)
        {
            resource_mutation_ingress_open();
            g_shutdown_phase = POWER_SHUTDOWN_FAILED;
            return 0U;
        }
        g_shutdown_phase = POWER_SHUTDOWN_FINAL;
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
        if (board_power_shutdown_request_poll(now_ms) == 0U)
            g_shutdown_phase = POWER_SHUTDOWN_IDLE;
    }
    return 0U;
}

power_shutdown_phase_t power_shutdown_phase(void){return g_shutdown_phase;}
uint8_t power_shutdown_mutations_frozen(void)
{
    return (uint8_t)(g_shutdown_phase >= POWER_SHUTDOWN_WAIT_STORAGE
        && g_shutdown_phase <= POWER_SHUTDOWN_FINAL);
}
