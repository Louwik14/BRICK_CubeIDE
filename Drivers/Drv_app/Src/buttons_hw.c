/**
 * @file buttons_hw.c
 * @brief Module applicatif buttons_hw.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à buttons_hw.
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

#include "buttons_hw.h"
#include "Board/board_controls.h"

#define BUTTONS_HW_REG_COUNT_MAX 4U
#define BUTTONS_HW_BITS_PER_REG 8U

static uint8_t buttons_hw_state[BTN_COUNT];

/* --------- GPIO helpers --------- */

/**
 * @brief Point d'entrée sr_pl_low.
 *
 * Rôle:
 * - Exécuter le traitement associé à sr_pl_low.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline void sr_pl_low(void)
{
    board_controls_buttons_latch_low();
}

/**
 * @brief Point d'entrée sr_pl_high.
 *
 * Rôle:
 * - Exécuter le traitement associé à sr_pl_high.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline void sr_pl_high(void)
{
    board_controls_buttons_latch_high();
}

/**
 * @brief Point d'entrée sr_sck_low.
 *
 * Rôle:
 * - Exécuter le traitement associé à sr_sck_low.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline void sr_sck_low(void)
{
    board_controls_buttons_clock_low();
}

/**
 * @brief Point d'entrée sr_sck_high.
 *
 * Rôle:
 * - Exécuter le traitement associé à sr_sck_high.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline void sr_sck_high(void)
{
    board_controls_buttons_clock_high();
}

/**
 * @brief Point d'entrée sr_data_read.
 *
 * Rôle:
 * - Exécuter le traitement associé à sr_data_read.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline uint32_t sr_data_read(void)
{
    return board_controls_buttons_data_read();
}

/* --------- Init --------- */

/**
 * @brief Point d'entrée buttons_hw_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à buttons_hw_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void buttons_hw_init(void)
{
    for(uint32_t i = 0; i < BTN_COUNT; i++)
        buttons_hw_state[i] = 0;

    sr_pl_high();
    sr_sck_low();
}

/* --------- Read shift registers --------- */

/**
 * @brief Point d'entrée buttons_hw_read.
 *
 * Rôle:
 * - Exécuter le traitement associé à buttons_hw_read.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void buttons_hw_read(void)
{
    uint32_t raw = 0;

    /* Latch parallel inputs */
    sr_pl_low();
    board_controls_io_barrier();
    sr_pl_high();

    const uint32_t physical_count = board_controls_button_physical_count();
    const uint32_t total_bits = (physical_count <= (BUTTONS_HW_REG_COUNT_MAX * BUTTONS_HW_BITS_PER_REG))
        ? physical_count
        : (BUTTONS_HW_REG_COUNT_MAX * BUTTONS_HW_BITS_PER_REG);

    for(uint32_t i = 0; i < total_bits; i++)
    {
        sr_sck_low();
        board_controls_io_barrier();

        raw <<= 1;
        raw |= sr_data_read();

        sr_sck_high();
        board_controls_io_barrier();
    }

    /* active LOW buttons */
    uint32_t mask = (total_bits >= 32U) ? 0xFFFFFFFFUL : ((1UL << total_bits) - 1UL);
    uint32_t pressed_mask = (~raw) & mask;

    for(uint32_t i = 0; i < BTN_COUNT; i++)
    {
        buttons_hw_state[i] = 0U;
    }

    for(uint32_t physical_idx = 0; physical_idx < total_bits; physical_idx++)
    {
        const button_id_t logical_idx = board_controls_button_logical_for_physical((uint8_t)physical_idx);
        if (logical_idx < BTN_COUNT)
        {
            buttons_hw_state[logical_idx] = (uint8_t)((pressed_mask >> physical_idx) & 1U);
        }
    }
}

/* --------- API --------- */

/**
 * @brief Point d'entrée buttons_hw_get.
 *
 * Rôle:
 * - Exécuter le traitement associé à buttons_hw_get.
 *
 * @param btn Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
uint8_t buttons_hw_get(button_id_t btn)
{
    if(btn >= BTN_COUNT)
        return 0;

    return buttons_hw_state[btn];
}
