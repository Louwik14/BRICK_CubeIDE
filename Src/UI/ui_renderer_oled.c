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

#include "IPC/audio_boot_diagnostic_reader.h"
#include "main.h"
#include "drv_display.h"
#include "font.h"
#include "ui_boot_loading.h"
#include "ui_page_manager.h"
#include "ui_roll_popup.h"
#include "ui_template_page.h"

#define UI_RENDER_PERIOD_MS 16U

static volatile uint8_t g_ui_rendering = 0U;
static const ui_page_t *g_ui_render_page;
static uint32_t g_ui_render_generation;
static uint32_t g_ui_render_job_generation;

static uint8_t ui_renderer_oled_page_pending(const ui_page_t *page)
{
    return (uint8_t)((ui_template_page_render_pending() != 0U)
        || ((page != NULL) && (page->render_pending != NULL)
            && (page->render_pending() != 0U)));
}

static void ui_renderer_oled_cancel_active(void)
{
    if ((g_ui_render_page != NULL) && (g_ui_render_page->render_cancel != NULL))
    {
        g_ui_render_page->render_cancel();
    }
    ui_template_page_render_cancel();
    g_ui_render_page = NULL;
    g_ui_rendering = 0U;
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

    if ((g_ui_rendering != 0U)
            && ((page != g_ui_render_page)
                || (g_ui_render_generation != g_ui_render_job_generation)))
    {
        ui_renderer_oled_cancel_active();
    }
    if (g_ui_rendering == 0U)
    {
        g_ui_render_page = page;
        g_ui_render_job_generation = g_ui_render_generation;
        g_ui_rendering = 1U;
        drv_display_clear();
    }

    audio_boot_diag_snapshot_t audio_diag;
    audio_boot_diag_read(&audio_diag);
    if (audio_diag.state == AUDIO_INIT_ERROR)
    {
        if (ui_renderer_oled_page_pending(g_ui_render_page) != 0U)
        {
            ui_renderer_oled_cancel_active();
            drv_display_clear();
        }
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
        if (ui_renderer_oled_page_pending(g_ui_render_page) != 0U)
        {
            ui_renderer_oled_cancel_active();
            drv_display_clear();
        }
        ui_boot_loading_render();
        ui_boot_loading_note_frame_rendered();
    }
    else if ((page != 0) && (page->render != 0))
    {
        page->render();
        if (ui_renderer_oled_page_pending(page) != 0U)
        {
            return;
        }
        ui_roll_popup_render(HAL_GetTick());
    }

    g_ui_render_page = NULL;
    g_ui_rendering = 0U;
}

/**
 * @brief Cadence le rendu UI à une fréquence adaptée à l'OLED.
 */
void ui_renderer_oled_service_poll(void)
{
    static uint32_t last_render = 0U;
    const uint32_t now = HAL_GetTick();

    if (g_ui_rendering != 0U)
    {
        ui_renderer_oled_draw();
        return;
    }

    if ((now - last_render) < UI_RENDER_PERIOD_MS)
    {
        return;
    }

    ui_renderer_oled_draw();
    last_render = now;
}

uint8_t ui_renderer_oled_is_rendering(void)
{
    return g_ui_rendering;
}

void ui_renderer_oled_invalidate(void)
{
    g_ui_render_generation++;
}
