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

#include "u8g2_port.h"

u8g2_t g_u8g2;

void ui_renderer_oled_init(void)
{
    u8g2_Setup_ssd1309_128x64_noname0_f(
        &g_u8g2,
        U8G2_R0,
        u8x8_byte_stm32_spi_hw,
        u8x8_gpio_and_delay_stm32
    );

    u8g2_InitDisplay(&g_u8g2);
    u8g2_SetPowerSave(&g_u8g2, 0);
    u8g2_ClearBuffer(&g_u8g2);
    u8g2_SendBuffer(&g_u8g2);
}

u8g2_t g_u8g2;

void ui_renderer_oled_init(void)
{
    u8g2_Setup_ssd1309_128x64_noname0_f(
        &g_u8g2,
        U8G2_R0,
        u8x8_byte_stm32_spi_hw,
        u8x8_gpio_and_delay_stm32
    );

    u8g2_InitDisplay(&g_u8g2);
    u8g2_SetPowerSave(&g_u8g2, 0);
    u8g2_ClearBuffer(&g_u8g2);
    u8g2_SendBuffer(&g_u8g2);
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
    /* Rendering is inlined in each page->render() for debug instrumentation. */
}
