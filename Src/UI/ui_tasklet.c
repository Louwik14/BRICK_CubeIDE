/**
 * @file ui_tasklet.c
 * @brief Module applicatif ui_tasklet.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à ui_tasklet.
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

#include "ui_tasklet.h"
#include "App/control_domain.h"
#include "IPC/live_clock_control.h"
#include "Track/track_runtime.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "drv_display.h"
#include "encoders.h"
#include "font.h"
#include "Storage/project_product.h"
#include "Storage/sd_access_gate.h"
#include "stm32h7xx_hal.h"
#include "ui_boot_loading.h"
#include "ui_core.h"
#include "ui_event.h"

/**
 * @brief Point d'entrée ui_tasklet_poll.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_tasklet_poll.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static uint8_t g_ui_tasklet_init = 0U;

typedef enum
{
    UI_BOOT_LOADING_INACTIVE = 0,
    UI_BOOT_LOADING_WAIT_FRAME,
    UI_BOOT_LOADING_RESTORE_PROJECT,
    UI_BOOT_LOADING_WAIT_SAMPLES,
    UI_BOOT_LOADING_FAILED
} ui_boot_loading_phase_t;

static ui_boot_loading_phase_t g_ui_boot_loading_phase = UI_BOOT_LOADING_INACTIVE;
static project_product_progress_t g_ui_boot_loading_progress;
static uint8_t g_ui_boot_loading_anim_phase;
static uint8_t g_ui_boot_loading_variant;
static uint8_t g_ui_boot_loading_phrase_a;
static uint8_t g_ui_boot_loading_phrase_b;

static const char *const k_ui_boot_loading_phrases[] = {
    "INSERT COIN",
    "CTRL ALT BEAT",
    "404 GROOVE FOUND",
    "BRICKING THE BEAT",
    "WAVING AT WAVS",
    "BEEP BOOP BRICK",
    "DO NOT PANIC",
    "DREAMING IN 48K",
    "TICKLING THE DAC",
    "BOOTING THE BOOP",
    "UNBOXING THE BOOP",
    "RETICULATING BASS",
    "GLUING THE GROOVE",
    "KERNEL OF FUNK",
    "LOADING THE WOBBLE",
    "MUTING THE SILENCE",
    "UNMUTING THE VOID",
    "HIHATS DOING MATH",
    "BASSLINE HAS SNACK",
    "PIXELS NEED COFFEE",
    "LFO SAYS MAYBE",
    "FILTERS SAY YES",
    "ASKING THE SYNTHS",
    "SYNTHS ARE THINKING",
    "SAMPLER YAWNS",
    "WAVS TAKE SEATS",
    "STEPS FIND THE ONE",
    "METRONOME BLINKS",
    "GHOST NOTES GATHER",
    "DRUMS CHECK PASSPORT",
    "BEEP FARM AWAKES",
    "BOOP FIELD READY",
    "OILING ENCODERS",
    "TINY DAC STRETCHES",
    "PHASE DUCKS ALIGN",
    "BEEP CACHE WARM",
    "BOOP BUFFER HUMS",
    "BEAT BUGS FRIENDLY",
    "ECHOES FIND ROOM",
    "DRUM BUS SMILES",
    "KICK BYTE READY",
    "HAT BYTE READY",
    "MONOCHROME GROOVE",
    "PLACING BEAT BRICKS",
    "MIDI MADE ME DO IT",
    "WAVS BEFORE RAVES",
    "NO SAMPLE LEFT BEHIND",
    "LOOP THERE IT IS",
    "QUANTIZE AND SHINE",
    "BEAT ON THE BRICKS",
    "KICK FLIPS A BIT",
    "SNARE BYTE SNACK",
    "DAC TO THE FUTURE",
    "SAMPLE AND HOLD UP",
    "LOLFO WOBBLE",
    "BUFFER THAN EVER",
    "CACHE IN THE ATTIC",
    "ENCODER ENCORE",
    "PIXEL PERFECTISH",
    "WAV HELLO THERE",
    "LOOP DREAMS LOUD",
    "SYNTH HAPPENS",
    "BASS ACKWARDS",
    "KICKSTART MY CACHE",
    "ALL YOUR BASS",
    "BPM MY GUEST",
    "TICK TOCK GROOVE",
    "THE ONE IS CALLING",
    "MUTE BUTTON BLUSHES",
    "UNMUTE TO CONTINUE",
    "SAMPLES IN PAJAMAS",
    "LOOPS EAT BREAKFAST",
    "KICK SAYS KNOCK",
    "SNAREWARE LOADING",
    "HAT TRICK BUFFER",
    "BASS GOT BUSY",
    "NEVER GONNA QUANTIZE",
    "FUNK STACK OVERFLOW",
    "BITCRUSH BREAKFAST",
    "LOWPASS HIGHFIVE",
    "SINE OF THE TIMES",
    "NOISE IS A FEATURE",
    "CLOCK SAYS TICK",
    "MIDI CABLE WINKS",
    "SAMPLE SOUP SIMMERS",
    "200 OK GROOVE",
    "BRICK BY BRICK",
    "ONE MORE BRICK",
    "TEMPO HAS OPINIONS",
    "QUANTIZE MY FRIES",
    "BUFFER HUGS THE BEAT",
    "DAC ATTACK SNACK",
    "LFO WOBBLE TAX",
    "NOTE TO SELF GROOVE"
};

#define UI_BOOT_LOADING_PHRASE_COUNT \
    ((uint8_t)(sizeof(k_ui_boot_loading_phrases) / sizeof(k_ui_boot_loading_phrases[0])))

static void ui_boot_loading_center_text(uint8_t y, const char *text)
{
    const uint8_t width = drv_display_text_width(text);
    const uint8_t x = (width < OLED_WIDTH) ? (uint8_t)((OLED_WIDTH - width) / 2U) : 0U;
    drv_display_draw_text(x, y, text);
}

static void ui_boot_loading_draw_frame(void)
{
    drv_display_draw_rect(1, 1, 126, 62);
    drv_display_draw_pixel(3, 3, true);
    drv_display_draw_pixel(4, 2, true);
    drv_display_draw_pixel(124, 3, true);
    drv_display_draw_pixel(123, 2, true);
    drv_display_draw_pixel(3, 60, true);
    drv_display_draw_pixel(4, 61, true);
    drv_display_draw_pixel(124, 60, true);
    drv_display_draw_pixel(123, 61, true);

    drv_display_draw_pixel(10, 6, true);
    drv_display_draw_pixel(13, 6, true);
    drv_display_draw_pixel(10, 9, true);
    drv_display_draw_pixel(13, 9, true);
    drv_display_draw_pixel(114, 6, true);
    drv_display_draw_pixel(117, 6, true);
    drv_display_draw_pixel(114, 9, true);
    drv_display_draw_pixel(117, 9, true);
}

/*
 * 107x19, 1bpp, MSB first.
 * ####.####.####........####.####.####........####.####.####.####...####.####.####.####...####...........####
 * ####.####.####........####.####.####........####.####.####.####...####.####.####.####...####...........####
 * ####.####.####........####.####.####........####.####.####.####...####.####.####.####...####...........####
 * ...........................................................................................................
 * ####...........####...####...........####........####.####........####..................####......####.....
 * ####...........####...####...........####........####.####........####..................####......####.....
 * ####...........####...####...........####........####.####........####..................####......####.....
 * ...........................................................................................................
 * ####.####.####........####.####.####.............####.####........####..................####.####..........
 * ####.####.####........####.####.####.............####.####........####..................####.####..........
 * ####.####.####........####.####.####.............####.####........####..................####.####..........
 * ...........................................................................................................
 * ####...........####...####......####.............####.####........####..................####......####.....
 * ####...........####...####......####.............####.####........####..................####......####.....
 * ####...........####...####......####.............####.####........####..................####......####.....
 * ...........................................................................................................
 * ####.####.####........####...........####...####.####.####.####...####.####.####.####...####...........####
 * ####.####.####........####...........####...####.####.####.####...####.####.####.####...####...........####
 * ####.####.####........####...........####...####.####.####.####...####.####.####.####...####...........####
 */
#define UI_BOOT_LOGO_W 107U
#define UI_BOOT_LOGO_H 19U
static const uint8_t k_ui_boot_logo_brick[UI_BOOT_LOGO_H][14] = {
    {0xF7U, 0xBCU, 0x03U, 0xDEU, 0xF0U, 0x0FU, 0x7BU, 0xDEU, 0x3DU, 0xEFU, 0x78U, 0xF0U, 0x01U, 0xE0U},
    {0xF7U, 0xBCU, 0x03U, 0xDEU, 0xF0U, 0x0FU, 0x7BU, 0xDEU, 0x3DU, 0xEFU, 0x78U, 0xF0U, 0x01U, 0xE0U},
    {0xF7U, 0xBCU, 0x03U, 0xDEU, 0xF0U, 0x0FU, 0x7BU, 0xDEU, 0x3DU, 0xEFU, 0x78U, 0xF0U, 0x01U, 0xE0U},
    {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U},
    {0xF0U, 0x01U, 0xE3U, 0xC0U, 0x07U, 0x80U, 0x7BU, 0xC0U, 0x3CU, 0x00U, 0x00U, 0xF0U, 0x3CU, 0x00U},
    {0xF0U, 0x01U, 0xE3U, 0xC0U, 0x07U, 0x80U, 0x7BU, 0xC0U, 0x3CU, 0x00U, 0x00U, 0xF0U, 0x3CU, 0x00U},
    {0xF0U, 0x01U, 0xE3U, 0xC0U, 0x07U, 0x80U, 0x7BU, 0xC0U, 0x3CU, 0x00U, 0x00U, 0xF0U, 0x3CU, 0x00U},
    {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U},
    {0xF7U, 0xBCU, 0x03U, 0xDEU, 0xF0U, 0x00U, 0x7BU, 0xC0U, 0x3CU, 0x00U, 0x00U, 0xF7U, 0x80U, 0x00U},
    {0xF7U, 0xBCU, 0x03U, 0xDEU, 0xF0U, 0x00U, 0x7BU, 0xC0U, 0x3CU, 0x00U, 0x00U, 0xF7U, 0x80U, 0x00U},
    {0xF7U, 0xBCU, 0x03U, 0xDEU, 0xF0U, 0x00U, 0x7BU, 0xC0U, 0x3CU, 0x00U, 0x00U, 0xF7U, 0x80U, 0x00U},
    {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U},
    {0xF0U, 0x01U, 0xE3U, 0xC0U, 0xF0U, 0x00U, 0x7BU, 0xC0U, 0x3CU, 0x00U, 0x00U, 0xF0U, 0x3CU, 0x00U},
    {0xF0U, 0x01U, 0xE3U, 0xC0U, 0xF0U, 0x00U, 0x7BU, 0xC0U, 0x3CU, 0x00U, 0x00U, 0xF0U, 0x3CU, 0x00U},
    {0xF0U, 0x01U, 0xE3U, 0xC0U, 0xF0U, 0x00U, 0x7BU, 0xC0U, 0x3CU, 0x00U, 0x00U, 0xF0U, 0x3CU, 0x00U},
    {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U},
    {0xF7U, 0xBCU, 0x03U, 0xC0U, 0x07U, 0x8FU, 0x7BU, 0xDEU, 0x3DU, 0xEFU, 0x78U, 0xF0U, 0x01U, 0xE0U},
    {0xF7U, 0xBCU, 0x03U, 0xC0U, 0x07U, 0x8FU, 0x7BU, 0xDEU, 0x3DU, 0xEFU, 0x78U, 0xF0U, 0x01U, 0xE0U},
    {0xF7U, 0xBCU, 0x03U, 0xC0U, 0x07U, 0x8FU, 0x7BU, 0xDEU, 0x3DU, 0xEFU, 0x78U, 0xF0U, 0x01U, 0xE0U},
};

static void ui_boot_loading_draw_bitmap_1bpp(uint8_t x,
                                             uint8_t y,
                                             uint8_t width,
                                             uint8_t height,
                                             const uint8_t *bits)
{
    const uint8_t stride = (uint8_t)((width + 7U) / 8U);
    for (uint8_t row = 0U; row < height; ++row)
    {
        for (uint8_t col = 0U; col < width; ++col)
        {
            const uint8_t byte = bits[(uint16_t)row * stride + (col >> 3U)];
            if ((byte & (uint8_t)(0x80U >> (col & 7U))) != 0U)
            {
                drv_display_draw_pixel((int)x + col, (int)y + row, true);
            }
        }
    }
}

typedef struct
{
    uint8_t col;
    uint8_t row;
} ui_boot_loading_stack_cell_t;

#define UI_BOOT_STACK_BLOCKS 42U
static const ui_boot_loading_stack_cell_t k_ui_boot_stack[UI_BOOT_STACK_BLOCKS] = {
    {0U, 0U}, {1U, 0U}, {2U, 0U}, {3U, 0U}, {4U, 0U}, {5U, 0U}, {6U, 0U},
    {7U, 0U}, {8U, 0U}, {9U, 0U}, {10U, 0U}, {11U, 0U}, {12U, 0U},
    {13U, 0U}, {14U, 0U}, {15U, 0U}, {16U, 0U}, {17U, 0U}, {18U, 0U},
    {19U, 0U}, {20U, 0U},
    {0U, 1U}, {1U, 1U}, {2U, 1U}, {4U, 1U}, {5U, 1U}, {6U, 1U},
    {8U, 1U}, {9U, 1U}, {10U, 1U}, {12U, 1U}, {13U, 1U}, {14U, 1U},
    {16U, 1U}, {17U, 1U}, {18U, 1U}, {19U, 1U},
    {5U, 2U}, {10U, 2U}, {15U, 2U}, {18U, 2U}, {18U, 3U},
};

static void ui_boot_loading_draw_tetris_cell(uint8_t x, uint8_t y, uint8_t active)
{
    if (active != 0U)
    {
        drv_display_fill_rect(x, y, 4, 4);
        drv_display_clear_rect((int)x + 1, (int)y + 1, 2, 2);
    }
    else
    {
        drv_display_draw_rect(x, y, 4, 4);
        drv_display_draw_pixel((int)x + 1, (int)y + 1, true);
        drv_display_draw_pixel((int)x + 2, (int)y + 2, true);
    }
}

static void ui_boot_loading_draw_tetris_shape(uint8_t col,
                                              uint8_t y,
                                              uint8_t shape,
                                              uint8_t active)
{
    static const int8_t k_shapes[3][4][2] = {
        { {0, 0}, {1, 0}, {0, 1}, {1, 1} },
        { {0, 0}, {1, 0}, {2, 0}, {3, 0} },
        { {0, 0}, {0, 1}, {1, 1}, {2, 1} },
    };
    const uint8_t base_x = (uint8_t)(12U + col * 5U);

    for (uint8_t i = 0U; i < 4U; ++i)
    {
        const int cell_x = (int)base_x + ((int)k_shapes[shape][i][0] * 5);
        const int cell_y = (int)y + ((int)k_shapes[shape][i][1] * 5);
        if ((cell_x >= 0) && (cell_x <= 124) && (cell_y >= 0) && (cell_y <= 60))
        {
            ui_boot_loading_draw_tetris_cell((uint8_t)cell_x, (uint8_t)cell_y, active);
        }
    }
}

static void ui_boot_loading_draw_side_meter(uint8_t x, uint8_t base_y)
{
    static const uint8_t k_cols[5] = { 2U, 4U, 7U, 4U, 2U };
    for (uint8_t col = 0U; col < 5U; ++col)
    {
        for (uint8_t row = 0U; row < k_cols[col]; ++row)
        {
            drv_display_draw_pixel((int)x + (int)col * 3,
                                   (int)base_y - (int)row * 3,
                                   true);
        }
    }
}

static void ui_boot_loading_draw_tetris_scene(uint16_t done, uint16_t total)
{
    enum
    {
        STACK_X = 12,
        STACK_BASE_Y = 43,
        CELL_STEP = 5
    };
    uint8_t filled = 0U;

    if (total != 0U)
    {
        const uint32_t scaled = (uint32_t)done * (uint32_t)UI_BOOT_STACK_BLOCKS;
        filled = (uint8_t)(scaled / total);
        if ((done >= total) || (filled > UI_BOOT_STACK_BLOCKS))
        {
            filled = UI_BOOT_STACK_BLOCKS;
        }
    }

    for (uint8_t i = 0U; i < filled; ++i)
    {
        const uint8_t x = (uint8_t)(STACK_X + k_ui_boot_stack[i].col * CELL_STEP);
        const uint8_t y = (uint8_t)(STACK_BASE_Y - k_ui_boot_stack[i].row * CELL_STEP);
        ui_boot_loading_draw_tetris_cell(x, y, 0U);
    }

    if (filled < UI_BOOT_STACK_BLOCKS)
    {
        const ui_boot_loading_stack_cell_t target = k_ui_boot_stack[filled];
        const uint8_t shape = (uint8_t)((filled + done) % 3U);
        uint8_t col = target.col;
        if (shape == 1U)
        {
            col = (col > 17U) ? 17U : col;
        }
        else if (shape == 2U)
        {
            col = (col > 18U) ? 18U : col;
        }

        const uint8_t target_y =
            (uint8_t)(STACK_BASE_Y - target.row * CELL_STEP - 10U);
        const uint8_t travel = (target_y > 24U) ? (uint8_t)(target_y - 24U) : 0U;
        const uint8_t fall_phase = (uint8_t)(g_ui_boot_loading_anim_phase & 15U);
        const uint8_t active_y = (uint8_t)(24U + (((uint16_t)travel * fall_phase) / 15U));
        const uint8_t active = (((g_ui_boot_loading_anim_phase >> 2U) & 1U) != 0U) ? 1U : 0U;
        ui_boot_loading_draw_tetris_shape(col, active_y, shape, active);

        const uint8_t trail_x = (uint8_t)(12U + col * 5U + 4U);
        for (uint8_t dot = 0U; dot < 3U; ++dot)
        {
            const uint8_t dot_y = (uint8_t)(active_y + 10U + dot * 4U);
            if (dot_y < target_y)
            {
                drv_display_draw_pixel(trail_x, dot_y, true);
            }
        }
    }
    else
    {
        drv_display_draw_line(STACK_X, STACK_BASE_Y - 18, STACK_X + 104, STACK_BASE_Y - 18);
    }

    ui_boot_loading_draw_side_meter(7U, STACK_BASE_Y + 1U);
    ui_boot_loading_draw_side_meter(109U, STACK_BASE_Y + 1U);
    for (uint8_t i = 0U; i < 28U; ++i)
    {
        drv_display_draw_pixel((int)STACK_X + (int)i * 4, STACK_BASE_Y + 5, true);
    }
}

static void ui_boot_loading_draw_tetris_variant(uint16_t done, uint16_t total)
{
    enum
    {
        LOGO_X = (OLED_WIDTH - UI_BOOT_LOGO_W) / 2U,
        LOGO_Y = 4
    };

    ui_boot_loading_draw_frame();
    ui_boot_loading_draw_bitmap_1bpp(LOGO_X,
                                     LOGO_Y,
                                     UI_BOOT_LOGO_W,
                                     UI_BOOT_LOGO_H,
                                     &k_ui_boot_logo_brick[0][0]);
    drv_display_set_font(&FONT_4X6);
    ui_boot_loading_center_text(23U, "... GROOVEBOX ...");
    ui_boot_loading_draw_tetris_scene(done, total);
}

static void ui_boot_loading_draw_wall_brick(uint8_t x, uint8_t y, uint8_t filled)
{
    if (filled != 0U)
    {
        drv_display_fill_rect(x, y, 8, 4);
        drv_display_clear_rect((int)x + 1, (int)y + 2, 6, 1);
    }
    else
    {
        drv_display_draw_rect(x, y, 8, 4);
    }
}

static void ui_boot_loading_draw_wall_variant(uint16_t done, uint16_t total)
{
    enum
    {
        LOGO_X = (OLED_WIDTH - UI_BOOT_LOGO_W) / 2U,
        LOGO_Y = 3,
        WALL_X = 10,
        WALL_Y = 27,
        WALL_COLS = 12,
        WALL_ROWS = 4,
        WALL_BRICKS = WALL_COLS * WALL_ROWS
    };
    uint8_t filled = 0U;

    ui_boot_loading_draw_frame();
    ui_boot_loading_draw_bitmap_1bpp(LOGO_X,
                                     LOGO_Y,
                                     UI_BOOT_LOGO_W,
                                     UI_BOOT_LOGO_H,
                                     &k_ui_boot_logo_brick[0][0]);

    if (total != 0U)
    {
        const uint32_t scaled = (uint32_t)done * (uint32_t)WALL_BRICKS;
        filled = (uint8_t)(scaled / total);
        if ((done >= total) || (filled > WALL_BRICKS))
        {
            filled = WALL_BRICKS;
        }
    }

    for (uint8_t row = 0U; row < WALL_ROWS; ++row)
    {
        const uint8_t stagger = ((row & 1U) != 0U) ? 4U : 0U;
        for (uint8_t col = 0U; col < WALL_COLS; ++col)
        {
            const uint8_t index = (uint8_t)((WALL_ROWS - 1U - row) * WALL_COLS + col);
            const uint8_t x = (uint8_t)(WALL_X + stagger + col * 9U);
            const uint8_t y = (uint8_t)(WALL_Y + row * 5U);
            ui_boot_loading_draw_wall_brick(x, y, (index < filled) ? 1U : 0U);
        }
    }

    if (filled < WALL_BRICKS)
    {
        const uint8_t active_col = (uint8_t)(filled % WALL_COLS);
        const uint8_t x = (uint8_t)(WALL_X + active_col * 9U);
        const uint8_t fall = (uint8_t)(g_ui_boot_loading_anim_phase & 15U);
        const uint8_t y = (uint8_t)(23U + (((uint16_t)fall * 17U) / 15U));
        if (((g_ui_boot_loading_anim_phase >> 2U) & 1U) != 0U)
        {
            ui_boot_loading_draw_wall_brick(x, y, 1U);
        }
    }
}

static void ui_boot_loading_draw_loader(uint16_t done, uint16_t total)
{
    if (g_ui_boot_loading_variant == 0U)
    {
        ui_boot_loading_draw_tetris_variant(done, total);
    }
    else
    {
        ui_boot_loading_draw_wall_variant(done, total);
    }
}

static uint8_t ui_boot_loading_select_variant(void)
{
    const uint32_t tick = HAL_GetTick();
    const uint32_t systick = SysTick->VAL;
    return (uint8_t)((tick ^ systick ^ (systick >> 8U)) & 1U);
}

static void ui_boot_loading_select_phrases(void)
{
    const uint32_t seed = HAL_GetTick() ^ SysTick->VAL ^ (SysTick->VAL >> 7U);
    g_ui_boot_loading_phrase_a = (uint8_t)(seed % UI_BOOT_LOADING_PHRASE_COUNT);
    g_ui_boot_loading_phrase_b =
        (uint8_t)(((seed >> 8U) ^ (seed * 17U)) % UI_BOOT_LOADING_PHRASE_COUNT);
    if (g_ui_boot_loading_phrase_b == g_ui_boot_loading_phrase_a)
    {
        g_ui_boot_loading_phrase_b =
            (uint8_t)((g_ui_boot_loading_phrase_b + 37U) % UI_BOOT_LOADING_PHRASE_COUNT);
    }
}

static const char *ui_boot_loading_current_phrase(void)
{
    const uint8_t use_second = ((g_ui_boot_loading_anim_phase >> 7U) & 1U) != 0U ? 1U : 0U;
    return k_ui_boot_loading_phrases[(use_second != 0U)
                                         ? g_ui_boot_loading_phrase_b
                                         : g_ui_boot_loading_phrase_a];
}

void ui_boot_loading_begin(void)
{
    memset(&g_ui_boot_loading_progress, 0, sizeof(g_ui_boot_loading_progress));
    g_ui_boot_loading_anim_phase = 0U;
    g_ui_boot_loading_variant = ui_boot_loading_select_variant();
    ui_boot_loading_select_phrases();
    g_ui_boot_loading_phase = UI_BOOT_LOADING_WAIT_FRAME;
}

void ui_boot_loading_service(void)
{
    const sd_storage_status_t storage_status = sd_access_storage_status();
    if ((storage_status == SD_STORAGE_STATUS_NO_MEDIA)
        || (storage_status == SD_STORAGE_STATUS_FAULT))
    {
        g_ui_boot_loading_phase = UI_BOOT_LOADING_INACTIVE;
        return;
    }
    if (storage_status != SD_STORAGE_STATUS_READY)
    {
        return;
    }

    if (g_ui_boot_loading_phase == UI_BOOT_LOADING_RESTORE_PROJECT)
    {
        if (drv_display_flush_in_progress() != 0U)
        {
            return;
        }

        if (control_domain_request_project(&(control_project_intent_t){CONTROL_PROJECT_RESTORE_BOOT, 0U}) != 0U)
            g_ui_boot_loading_phase = UI_BOOT_LOADING_WAIT_SAMPLES;
        else
            g_ui_boot_loading_phase = UI_BOOT_LOADING_FAILED;
    }

    if (g_ui_boot_loading_phase == UI_BOOT_LOADING_WAIT_SAMPLES)
    {
        if ((project_product_get_progress(&g_ui_boot_loading_progress) != 0U)
            && (g_ui_boot_loading_progress.complete != 0U))
        {
            g_ui_boot_loading_phase =
                (g_ui_boot_loading_progress.result == PROJECT_PRODUCT_RESULT_FAILED)
                    ? UI_BOOT_LOADING_FAILED : UI_BOOT_LOADING_INACTIVE;
        }
    }
}

uint8_t ui_boot_loading_is_active(void)
{
    return (g_ui_boot_loading_phase != UI_BOOT_LOADING_INACTIVE) ? 1U : 0U;
}

void ui_boot_loading_note_frame_rendered(void)
{
    if (g_ui_boot_loading_phase == UI_BOOT_LOADING_WAIT_FRAME)
    {
        g_ui_boot_loading_phase = UI_BOOT_LOADING_RESTORE_PROJECT;
    }
}

void ui_boot_loading_render(void)
{
    char counter[16];
    g_ui_boot_loading_anim_phase++;

    drv_display_set_draw_color(1U);
    ui_boot_loading_draw_loader(g_ui_boot_loading_progress.done,
                                g_ui_boot_loading_progress.total);
    drv_display_set_font(&FONT_4X6);
    ui_boot_loading_center_text(51U, ui_boot_loading_current_phrase());

    if (g_ui_boot_loading_progress.total != 0U)
    {
        (void)snprintf(counter,
                       sizeof(counter),
                       "%u/%u",
                       (unsigned)g_ui_boot_loading_progress.done,
                       (unsigned)g_ui_boot_loading_progress.total);
        ui_boot_loading_center_text(58U, counter);
    }
}

void ui_boot_loading_discard_inputs(void)
{
    ui_event_t ev;

    for (uint8_t i = 0U; i < (uint8_t)ENC_COUNT; ++i)
    {
        encoder_reset_delta(i);
    }

    while (ui_event_pop(&ev))
    {
    }
}

void ui_tasklet_poll(void)
{
    if (g_ui_tasklet_init == 0U)
    {
        g_ui_tasklet_init = 1U;
        drv_display_init();
    }

    if (ui_boot_loading_is_active() != 0U)
    {
        ui_boot_loading_discard_inputs();
        return;
    }

    ui_core_tick();
}

uint8_t ui_tasklet_is_initialized(void)
{
    return g_ui_tasklet_init;
}
