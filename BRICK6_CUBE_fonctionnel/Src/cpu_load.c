/**
 * @file cpu_load.c
 * @brief Mesure temps CPU du bloc audio via DWT CYCCNT (Cortex-M7).
 *
 * Principe:
 * - On lit DWT->CYCCNT au début et à la fin du traitement DSP d'un bloc.
 * - Delta cycles = cycles réellement consommés par le traitement audio.
 * - Charge permille = delta_cycles / budget_cycles * 1000.
 *
 * Pourquoi DWT->CYCCNT:
 * - Lecture très rapide (quelques instructions), adaptée au contexte IRQ.
 * - Résolution au cycle CPU, plus précise que des tick timers lents.
 *
 * Contexte IRQ vs main:
 * - Les écritures (last/max/overruns) sont faites en IRQ audio.
 * - Les lectures (UI) sont faites en main loop via getters.
 */

#include "cpu_load.h"
#include "stm32h7xx.h"

/* Budget cycles pour un bloc audio:
 * budget = SystemCoreClock * frames_per_block / sample_rate_hz
 */
static uint32_t budget_cycles = 1U;

/* Timestamp début bloc (IRQ). */
static uint32_t block_start_cycles = 0U;

/* Métriques partagées IRQ -> main loop. */
static volatile uint32_t last_permille = 0U;
static volatile uint32_t max_permille = 0U;
static volatile uint32_t overruns_count = 0U;

void cpu_load_init(uint32_t sample_rate_hz, uint32_t frames_per_block)
{
    uint64_t budget64;

    if(sample_rate_hz == 0U)
        sample_rate_hz = 1U;
    if(frames_per_block == 0U)
        frames_per_block = 1U;

    budget64 = ((uint64_t)SystemCoreClock * (uint64_t)frames_per_block) /
               (uint64_t)sample_rate_hz;

    if(budget64 == 0U)
        budget64 = 1U;

    budget_cycles = (uint32_t)budget64;

    last_permille = 0U;
    max_permille = 0U;
    overruns_count = 0U;
    block_start_cycles = 0U;

    /* Active les blocs trace DWT puis le compteur cycle. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void cpu_load_block_start_irq(void)
{
    block_start_cycles = DWT->CYCCNT;
}

void cpu_load_block_end_irq(void)
{
    const uint32_t end_cycles = DWT->CYCCNT;
    const uint32_t elapsed_cycles = end_cycles - block_start_cycles;
    uint32_t permille;

    permille = (uint32_t)(((uint64_t)elapsed_cycles * 1000ULL) /
                          (uint64_t)budget_cycles);

    last_permille = permille;

    if(permille > max_permille)
        max_permille = permille;

    if(permille > 1000U)
        overruns_count++;
}

uint32_t cpu_load_get_permille(void)
{
    return last_permille;
}

uint32_t cpu_load_get_max_permille(void)
{
    return max_permille;
}

uint32_t cpu_load_get_overruns(void)
{
    return overruns_count;
}

void cpu_load_reset_max(void)
{
    max_permille = last_permille;
}
