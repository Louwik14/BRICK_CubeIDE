/**
 * @file ui_renderer_oled.c
 * @brief Module applicatif ui_renderer_oled.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à ui_renderer_oled.
 * - Fournir les services internes utilisés par le firmware utilisateur.
 *
 * Architecture:
 * - Appelé par: modules applicatifs selon l'orchestration du firmware.
 * - Appelle: dépendances matérielles et/ou modules utilisateur associés.
 *
 * Contraintes temps réel:
 * - IRQ: selon les API appelées.
 * - Hard realtime: selon le chemin d'exécution.
 * - malloc: éviter en chemin critique.
 *
 * Notes:
 * - Documentation ajoutée sans modification de la logique d'exécution.
 */

#include "ui_renderer_oled.h"

#include <stdio.h>
#include <string.h>
#include "cmsis_os.h"

#include "App/control_domain.h"
#include "IPC/audio_boot_diagnostic_reader.h"
#include "IPC/ui_visible_data.h"
#include "main.h"
#include "drv_display.h"
#include "font.h"
#include "Storage/settings_storage_service.h"
#include "ui_boot_loading.h"
#include "ui_page_manager.h"
#include "pages/ui_page_calibration.h"
#include "pages/ui_page_settings.h"
#include "ui_roll_popup.h"
#include "UI/ui_service_wakeup.h"
#include "ui_param.h"
#include "ui_hall_mode_projection.h"
#include "Seq/seq_runtime.h"
#include "UI/display_flush_service.h"
#include "UI/ui_core_feedback.h"
#include "UI/ui_tasklet.h"
#include "led_rgb.h"

#define UI_BOOT_RENDER_PERIOD_MS 100U
#define UI_ACTIVE_RENDER_PERIOD_MS 33U

static volatile uint8_t g_ui_rendering = 0U;
static uint32_t g_ui_next_deadline_ms;

static uint8_t ui_renderer_oled_cpu_load_visible(void)
{
    const uint8_t page_id = ui_page_get_id();
    return (uint8_t)((page_id >= UI_PAGE_TEMPLATE_ENV)
        && (page_id <= UI_PAGE_AUDIO_FX));
}

static const char *ui_audio_boot_error_label(board_audio_boot_error_t error)
{
    switch (error)
    {
        case BOARD_AUDIO_BOOT_CODEC_NOT_FOUND: return "CODEC NOT FOUND";
        case BOARD_AUDIO_BOOT_CODEC_RESET: return "CODEC RESET";
        case BOARD_AUDIO_BOOT_I2C: return "CODEC I2C";
        case BOARD_AUDIO_BOOT_VERIFY: return "CODEC VERIFY";
        case BOARD_AUDIO_BOOT_READY_TIMEOUT: return "CODEC TIMEOUT";
        case BOARD_AUDIO_BOOT_CLOCK: return "CODEC CLOCK";
        case BOARD_AUDIO_BOOT_TX_DMA: return "TX DMA";
        case BOARD_AUDIO_BOOT_RX_DMA: return "RX DMA";
        case BOARD_AUDIO_BOOT_SAI_SYNC: return "SAI SYNC";
        default: return "AUDIO HARDWARE";
    }
}

static void ui_renderer_oled_draw_project_busy(void)
{
    project_product_progress_t progress;
    project_product_command_t command = PROJECT_PRODUCT_COMMAND_NONE;
    char counter[24];

    if (ui_tasklet_project_presentation(&command, &progress) == 0U)
        memset(&progress, 0, sizeof(progress));
    const char *title = (command == PROJECT_PRODUCT_COMMAND_SAVE)
        ? "SAVING PROJECT"
        : (command == PROJECT_PRODUCT_COMMAND_LOAD)
            ? "LOADING PROJECT" : "PROJECT BUSY";
    drv_display_set_draw_color(1U);
    drv_display_set_font(&FONT_5X7);
    drv_display_draw_text((uint8_t)((OLED_WIDTH - drv_display_text_width(title)) / 2U),
                          8U, title);
    if (progress.total != 0U)
    {
        drv_display_draw_rect(8, 25, 112, 8);
        uint32_t done = progress.done;
        if (done > progress.total) done = progress.total;
        const uint8_t fill = (uint8_t)(((uint64_t)done * 108U)
                                       / progress.total);
        if (fill != 0U)
            drv_display_fill_rect(10, 27, fill, 4);
    }

    drv_display_set_font(&FONT_4X6);
    const char *phase = "PREPARING";
    switch (progress.phase)
    {
        case PROJECT_PRODUCT_PHASE_WRITING: phase = "WRITING"; break;
        case PROJECT_PRODUCT_PHASE_VERIFYING: phase = "VERIFYING"; break;
        case PROJECT_PRODUCT_PHASE_FINALIZING: phase = "FINALIZING"; break;
        case PROJECT_PRODUCT_PHASE_VALIDATING: phase = "VALIDATING"; break;
        case PROJECT_PRODUCT_PHASE_PREPARING_ASSETS: phase = "PREPARING ASSETS"; break;
        case PROJECT_PRODUCT_PHASE_INSTALLING: phase = "INSTALLING"; break;
        default: break;
    }
    drv_display_draw_text(
        (uint8_t)((OLED_WIDTH - drv_display_text_width(phase)) / 2U),
        37U, phase);
    if (progress.total != 0U)
    {
        (void)snprintf(counter, sizeof(counter), "%lu/%lu",
                       (unsigned long)progress.done,
                       (unsigned long)progress.total);
        drv_display_draw_text(
            (uint8_t)((OLED_WIDTH - drv_display_text_width(counter)) / 2U),
            46U, counter);
    }
    drv_display_draw_text(42U, 56U, "PLEASE WAIT");
}

/**
 * @brief Point d'entrée ui_renderer_oled_draw.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_renderer_oled_draw.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void ui_renderer_oled_draw(void)
{
    const ui_page_t *page = ui_page_get();

    ui_visible_data_cpu_set_visible(ui_renderer_oled_cpu_load_visible());

    g_ui_rendering = 1U;

    drv_display_clear();

    audio_boot_diag_snapshot_t audio_diag;
    audio_boot_diag_read(&audio_diag);
    if (audio_diag.state == AUDIO_INIT_ERROR)
    {
        const board_audio_boot_error_t error = audio_diag.error;
        drv_display_set_font(&FONT_5X7);
        char error_text[24];
        drv_display_draw_text(8U, 10U, "AUDIO INIT ERROR");
        drv_display_set_font(&FONT_4X6);
        drv_display_draw_text(8U, 25U, ui_audio_boot_error_label(error));
        (void)snprintf(error_text, sizeof(error_text), "BOOT ERROR: %u",
                       (unsigned)error);
        drv_display_draw_text(8U, 37U, error_text);
        drv_display_draw_text(8U, 49U, "REBOOT TO RETRY");
    }
    else if (ui_boot_loading_is_active() != 0U)
    {
        ui_boot_loading_render();
    }
    else if (control_domain_project_ui_busy() != 0U)
    {
        ui_renderer_oled_draw_project_busy();
    }
    else if ((page != 0) && (page->render != 0))
    {
        page->render();
        ui_roll_popup_render(HAL_GetTick());
        ui_core_feedback_render(HAL_GetTick());
    }

    g_ui_rendering = 0U;
}

/**
 * @brief Cadence le rendu UI à une fréquence adaptée à l'OLED.
 */
void ui_renderer_oled_service_render(void)
{
    if (ui_service_dirty_take() == 0U)
    {
        return;
    }

    ui_renderer_oled_draw();
    display_flush_service_frame_ready();
}

void ui_renderer_oled_service_deadline(void)
{
    const uint32_t now = HAL_GetTick();
    if ((ui_boot_loading_is_active() == 0U)
        && (control_domain_project_ui_busy() != 0U))
        return;
    ui_core_feedback_service(now);
    ui_roll_popup_service(now);
    ui_page_settings_service_deadline(now);
    ui_param_service_value_flash(now);
    ui_hall_patch_feedback_service_deadline(now);

    if (ui_boot_loading_is_active() != 0U)
    {
        ui_boot_loading_advance_animation();
        ui_service_dirty_set();
    }
    else if ((control_domain_project_ui_busy() == 0U)
             && (ui_page_get_id() == UI_PAGE_TEMPLATE_SEQ)
             && (seq_runtime_is_running() != 0U))
    {
        ui_service_dirty_set();
    }
}

uint32_t ui_renderer_oled_next_render_wait_ticks(void)
{
    const uint32_t now = HAL_GetTick();
    uint32_t period = 0U;
    uint32_t next_deadline = 0U;
    uint32_t candidate = 0U;

    if (ui_boot_loading_is_active() != 0U)
        period = UI_BOOT_RENDER_PERIOD_MS;
    else if ((control_domain_project_ui_busy() == 0U)
             && (ui_page_get_id() == UI_PAGE_TEMPLATE_SEQ)
             && (seq_runtime_is_running() != 0U))
        period = UI_ACTIVE_RENDER_PERIOD_MS;
    else
        period = 0U;

    if (period != 0U)
        candidate = now + period;

    if ((control_domain_project_ui_busy() == 0U)
        && ui_core_feedback_next_deadline(now, &next_deadline) != 0U
        && ((candidate == 0U) || ((int32_t)(next_deadline - candidate) < 0)))
        candidate = next_deadline;
    if ((control_domain_project_ui_busy() == 0U)
        && ui_roll_popup_next_deadline(now, &next_deadline) != 0U
        && ((candidate == 0U) || ((int32_t)(next_deadline - candidate) < 0)))
        candidate = next_deadline;
    if ((control_domain_project_ui_busy() == 0U)
        && ui_page_calibration_next_deadline(now, &next_deadline) != 0U
        && ((candidate == 0U) || ((int32_t)(next_deadline - candidate) < 0)))
        candidate = next_deadline;
    if ((control_domain_project_ui_busy() == 0U)
        && ui_page_settings_next_deadline(now, &next_deadline) != 0U
        && ((candidate == 0U) || ((int32_t)(next_deadline - candidate) < 0)))
        candidate = next_deadline;
    if ((control_domain_project_ui_busy() == 0U)
        && ui_param_next_value_flash_deadline(now, &next_deadline) != 0U
        && ((candidate == 0U) || ((int32_t)(next_deadline - candidate) < 0)))
        candidate = next_deadline;
    if ((control_domain_project_ui_busy() == 0U)
        && ui_hall_patch_feedback_next_deadline(now, &next_deadline) != 0U
        && ((candidate == 0U) || ((int32_t)(next_deadline - candidate) < 0)))
        candidate = next_deadline;
    if (led_presentation_next_deadline(now, &next_deadline) != 0U
        && ((candidate == 0U) || ((int32_t)(next_deadline - candidate) < 0)))
        candidate = next_deadline;

    if (candidate == 0U)
        return osWaitForever;

    if ((int32_t)(g_ui_next_deadline_ms - now) <= 0U
        || ((int32_t)(candidate - g_ui_next_deadline_ms) < 0))
    {
        g_ui_next_deadline_ms = candidate;
        return 0U;
    }

    return g_ui_next_deadline_ms - now;
}

uint8_t ui_renderer_oled_is_rendering(void)
{
    return g_ui_rendering;
}
