/**
 * @file drv_display.c
 * @brief Module applicatif drv_display.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à drv_display.
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

#include "drv_display.h"

#include "spi.h"
#include "gpio.h"
#include "sdram.h"

#include <string.h>
#include <stdio.h>

/* SPI5 handle CubeMX */
extern SPI_HandleTypeDef hspi5;

/* ====================================================================== */
/*                             VARIABLES INTERNES                         */
/* ====================================================================== */

static uint8_t buffer[OLED_WIDTH * OLED_HEIGHT / 8] SDRAM_BSS;
static const font_t *current_font = NULL;

/* Dirty tracking */
static volatile bool display_dirty = false;
static uint8_t dirty_pages = 0;

/* ====================================================================== */
/*                              UTILITAIRES GPIO                          */
/* ====================================================================== */

/**
 * @brief Point d'entrée cs_low.
 *
 * Rôle:
 * - Exécuter le traitement associé à cs_low.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline void cs_low(void)
{
    HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_RESET);
}

/**
 * @brief Point d'entrée cs_high.
 *
 * Rôle:
 * - Exécuter le traitement associé à cs_high.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline void cs_high(void)
{
    HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_SET);
}

/**
 * @brief Point d'entrée dc_cmd.
 *
 * Rôle:
 * - Exécuter le traitement associé à dc_cmd.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline void dc_cmd(void)
{
    HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, GPIO_PIN_RESET);
}

/**
 * @brief Point d'entrée dc_data.
 *
 * Rôle:
 * - Exécuter le traitement associé à dc_data.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline void dc_data(void)
{
    HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, GPIO_PIN_SET);
}

/* ====================================================================== */
/*                              UTILITAIRES SPI                           */
/* ====================================================================== */

/**
 * @brief Point d'entrée send_cmd.
 *
 * Rôle:
 * - Exécuter le traitement associé à send_cmd.
 *
 * @param cmd Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void send_cmd(uint8_t cmd)
{
    dc_cmd();
    cs_low();
    HAL_SPI_Transmit(&hspi5, &cmd, 1, 10);
    cs_high();
}

/**
 * @brief Point d'entrée send_data.
 *
 * Rôle:
 * - Exécuter le traitement associé à send_data.
 *
 * @param data Paramètre d'entrée de l'API.
 * @param len Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void send_data(const uint8_t *data, size_t len)
{
    dc_data();
    cs_low();
    HAL_SPI_Transmit(&hspi5, (uint8_t*)data, len, 100);
    cs_high();
}

/* ====================================================================== */
/*                              FRAMEBUFFER                               */
/* ====================================================================== */

/**
 * @brief Point d'entrée drv_display_get_buffer.
 *
 * Rôle:
 * - Exécuter le traitement associé à drv_display_get_buffer.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
uint8_t* drv_display_get_buffer(void)
{
    return buffer;
}

/* ====================================================================== */
/*                              PIXELS                                    */
/* ====================================================================== */

/**
 * @brief Point d'entrée set_pixel.
 *
 * Rôle:
 * - Exécuter le traitement associé à set_pixel.
 *
 * @param x Paramètre d'entrée de l'API.
 * @param y Paramètre d'entrée de l'API.
 * @param on Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline void set_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_WIDTH ||
        y < 0 || y >= OLED_HEIGHT)
        return;

    const int index = x + (y >> 3) * OLED_WIDTH;
    const uint8_t mask = (uint8_t)(1U << (y & 7));

    uint8_t old = buffer[index];

    if (on)
        buffer[index] |= mask;
    else
        buffer[index] &= (uint8_t)~mask;

    if (buffer[index] != old)
    {
        display_dirty = true;
        dirty_pages |= (1U << (y >> 3));
    }
}

/**
 * @brief Point d'entrée drv_display_draw_pixel.
 *
 * Rôle:
 * - Exécuter le traitement associé à drv_display_draw_pixel.
 *
 * @param x Paramètre d'entrée de l'API.
 * @param y Paramètre d'entrée de l'API.
 * @param on Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void drv_display_draw_pixel(int x, int y, bool on)
{
    set_pixel(x, y, on);
}

/* ====================================================================== */
/*                              PRIMITIVES                                */
/* ====================================================================== */

/**
 * @brief Point d'entrée drv_display_draw_rect.
 *
 * Rôle:
 * - Exécuter le traitement associé à drv_display_draw_rect.
 *
 * @param x Paramètre d'entrée de l'API.
 * @param y Paramètre d'entrée de l'API.
 * @param w Paramètre d'entrée de l'API.
 * @param h Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void drv_display_draw_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;

    for (int ix = 0; ix < w; ix++)
    {
        set_pixel(x + ix, y, true);
        set_pixel(x + ix, y + h - 1, true);
    }

    for (int iy = 0; iy < h; iy++)
    {
        set_pixel(x, y + iy, true);
        set_pixel(x + w - 1, y + iy, true);
    }
}

/**
 * @brief Point d'entrée drv_display_fill_rect.
 *
 * Rôle:
 * - Exécuter le traitement associé à drv_display_fill_rect.
 *
 * @param x Paramètre d'entrée de l'API.
 * @param y Paramètre d'entrée de l'API.
 * @param w Paramètre d'entrée de l'API.
 * @param h Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void drv_display_fill_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;

    for (int iy = 0; iy < h; iy++)
    {
        for (int ix = 0; ix < w; ix++)
        {
            set_pixel(x + ix, y + iy, true);
        }
    }
}

/**
 * @brief Point d'entrée drv_display_clear_rect.
 *
 * Rôle:
 * - Exécuter le traitement associé à drv_display_clear_rect.
 *
 * @param x Paramètre d'entrée de l'API.
 * @param y Paramètre d'entrée de l'API.
 * @param w Paramètre d'entrée de l'API.
 * @param h Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void drv_display_clear_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;

    for (int iy = 0; iy < h; iy++)
    {
        for (int ix = 0; ix < w; ix++)
        {
            set_pixel(x + ix, y + iy, false);
        }
    }
}

/* ====================================================================== */
/*                              TEXTE                                     */
/* ====================================================================== */

/**
 * @brief Point d'entrée drv_display_set_font.
 *
 * Rôle:
 * - Exécuter le traitement associé à drv_display_set_font.
 *
 * @param font Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void drv_display_set_font(const font_t *font)
{
    current_font = font;
}

/**
 * @brief Point d'entrée font_advance.
 *
 * Rôle:
 * - Exécuter le traitement associé à font_advance.
 *
 * @param f Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline uint8_t font_advance(const font_t *f)
{
    return (uint8_t)(f->width + f->spacing);
}

/**
 * @brief Point d'entrée drv_display_draw_char.
 *
 * Rôle:
 * - Exécuter le traitement associé à drv_display_draw_char.
 *
 * @param x Paramètre d'entrée de l'API.
 * @param y Paramètre d'entrée de l'API.
 * @param c Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void drv_display_draw_char(uint8_t x, uint8_t y, char c)
{
    if (!current_font)
        return;

    if ((uint8_t)c < current_font->first ||
        (uint8_t)c > current_font->last)
        c = '?';

    for (uint8_t col = 0; col < current_font->width; col++)
    {
        uint8_t bits = current_font->get_col(c, col);

        for (uint8_t row = 0; row < current_font->height; row++)
        {
            if (bits & (1U << row))
                set_pixel(x + col, y + row, true);
        }
    }
}

/**
 * @brief Point d'entrée drv_display_draw_text.
 *
 * Rôle:
 * - Exécuter le traitement associé à drv_display_draw_text.
 *
 * @param x Paramètre d'entrée de l'API.
 * @param y Paramètre d'entrée de l'API.
 * @param txt Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void drv_display_draw_text(uint8_t x, uint8_t y, const char *txt)
{
    if (!current_font || !txt)
        return;

    const uint8_t adv = font_advance(current_font);

    while (*txt && x < OLED_WIDTH)
    {
        drv_display_draw_char(x, y, *txt++);
        x = (uint8_t)(x + adv);
    }
}

/**
 * @brief Point d'entrée drv_display_draw_number.
 *
 * Rôle:
 * - Exécuter le traitement associé à drv_display_draw_number.
 *
 * @param x Paramètre d'entrée de l'API.
 * @param y Paramètre d'entrée de l'API.
 * @param num Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void drv_display_draw_number(uint8_t x, uint8_t y, int num)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", num);
    drv_display_draw_text(x, y, buf);
}

/* ====================================================================== */
/*                              CLEAR / UPDATE                            */
/* ====================================================================== */

/**
 * @brief Point d'entrée drv_display_clear.
 *
 * Rôle:
 * - Exécuter le traitement associé à drv_display_clear.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void drv_display_clear(void)
{
    memset(buffer, 0x00, sizeof(buffer));
    display_dirty = true;
    dirty_pages = 0xFF;
}

/**
 * @brief Point d'entrée drv_display_update.
 *
 * Rôle:
 * - Exécuter le traitement associé à drv_display_update.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void drv_display_update(void)
{
    if (!display_dirty)
        return;

    for (uint8_t page = 0; page < 8; page++)
    {
        if (!(dirty_pages & (1U << page)))
            continue;

        send_cmd(0xB0 + page);
        send_cmd(0x00);
        send_cmd(0x10);

        send_data(&buffer[page * OLED_WIDTH], OLED_WIDTH);
    }

    display_dirty = false;
    dirty_pages = 0;
}

/* ====================================================================== */
/*                              INITIALISATION                            */
/* ====================================================================== */

/**
 * @brief Point d'entrée drv_display_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à drv_display_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void drv_display_init(void)
{
    /* Reset OLED */
    HAL_GPIO_WritePin(OLED_RES_GPIO_Port, OLED_RES_Pin, GPIO_PIN_RESET);
    HAL_Delay(50);
    HAL_GPIO_WritePin(OLED_RES_GPIO_Port, OLED_RES_Pin, GPIO_PIN_SET);
    HAL_Delay(50);

    /* Init SSD1309 */
    send_cmd(0xAE);
    send_cmd(0xD5); send_cmd(0x80);
    send_cmd(0xA8); send_cmd(0x3F);
    send_cmd(0xD3); send_cmd(0x00);
    send_cmd(0x40);
    /*
     * Use PAGE addressing mode because drv_display_update() pushes the
     * framebuffer page by page with explicit 0xB0/0x00/0x10 addressing.
     * Horizontal mode can desynchronize addressing and create visual artifacts
     * on dense screens.
     */
    send_cmd(0x20); send_cmd(0x02);
    send_cmd(0xA1);
    send_cmd(0xC8);
    send_cmd(0xDA); send_cmd(0x12);
    send_cmd(0xAF);

    drv_display_clear();
    drv_display_update();

    /* Default font */
    extern const font_t FONT_5X7;
    current_font = &FONT_5X7;
}
