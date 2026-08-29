/**
 * @file cpu_load.c
 * @brief Module applicatif cpu_load.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à cpu_load.
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

#include "Platform/cpu_load.h"
#include "stm32h7xx.h"

#define CPU_LOAD_MAX_PERMILLE        1000U
#define CPU_LOAD_AVG_SHIFT           4U
#define CPU_LOAD_AVG_ROUNDING        (1U << 7)
#define CPU_LOAD_AVG_SCALE_SHIFT     8U
#define CPU_LOAD_RECENT_WINDOW       16U
#define CPU_LOAD_THRESH_80_PERMILLE  800U
#define CPU_LOAD_THRESH_90_PERMILLE  900U
#define CPU_LOAD_THRESH_100_PERMILLE 1000U

static uint32_t irq_start_cycles = 0U;
static uint32_t last_irq_entry_cycles = 0U;
static uint32_t current_period_cycles = 0U;
static uint32_t period_is_ready = 0U;

static volatile uint32_t cpu_last_permille = 0U;
static volatile uint32_t cpu_avg_permille_q8 = 0U;
static volatile uint32_t cpu_peak_permille = 0U;
static volatile uint32_t cpu_peak_recent_permille = 0U;
static volatile uint32_t cpu_over_80_count = 0U;
static volatile uint32_t cpu_over_90_count = 0U;
static volatile uint32_t cpu_over_100_count = 0U;
static volatile uint32_t cpu_block_count = 0U;
static volatile uint32_t cpu_counter_valid = 0U;

static uint16_t recent_permille_ring[CPU_LOAD_RECENT_WINDOW];
static uint32_t recent_permille_index = 0U;
static uint32_t recent_permille_count = 0U;

static uint32_t cpu_load_avg_permille_from_q8(uint32_t avg_q8)
{
    return (avg_q8 + CPU_LOAD_AVG_ROUNDING) >> CPU_LOAD_AVG_SCALE_SHIFT;
}

static uint32_t cpu_load_compute_recent_peak(void)
{
    uint32_t peak = 0U;
    uint32_t i;

    for(i = 0U; i < recent_permille_count; ++i)
    {
        const uint32_t sample = recent_permille_ring[i];

        if(sample > peak)
            peak = sample;
    }

    return peak;
}

/**
 * @brief Point d'entrée cpu_load_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à cpu_load_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void cpu_load_init(void)
{
    uint32_t t0;
    uint32_t t1;
    uint32_t i;

    irq_start_cycles = 0U;
    last_irq_entry_cycles = 0U;
    current_period_cycles = 0U;
    period_is_ready = 0U;
    cpu_last_permille = 0U;
    cpu_avg_permille_q8 = 0U;
    cpu_peak_permille = 0U;
    cpu_peak_recent_permille = 0U;
    cpu_over_80_count = 0U;
    cpu_over_90_count = 0U;
    cpu_over_100_count = 0U;
    cpu_block_count = 0U;
    cpu_counter_valid = 0U;
    recent_permille_index = 0U;
    recent_permille_count = 0U;

    for(i = 0U; i < CPU_LOAD_RECENT_WINDOW; ++i)
        recent_permille_ring[i] = 0U;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

#if defined(DWT_LAR)
    DWT->LAR = 0xC5ACCE55UL;
#endif

    __DSB();
    __ISB();

    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    __DSB();
    __ISB();

    t0 = DWT->CYCCNT;
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    t1 = DWT->CYCCNT;

    if(t1 != t0)
        cpu_counter_valid = 1U;
}

/**
 * @brief Point d'entrée cpu_load_irq_begin.
 *
 * Rôle:
 * - Exécuter le traitement associé à cpu_load_irq_begin.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void cpu_load_irq_begin(void)
{
    uint32_t now;

    if(cpu_counter_valid == 0U)
        return;

    now = DWT->CYCCNT;

    if(period_is_ready == 0U)
    {
        last_irq_entry_cycles = now;
        irq_start_cycles = now;
        current_period_cycles = 0U;
        period_is_ready = 1U;
        return;
    }

    current_period_cycles = now - last_irq_entry_cycles;
    last_irq_entry_cycles = now;
    irq_start_cycles = now;
}

/**
 * @brief Point d'entrée cpu_load_irq_end.
 *
 * Rôle:
 * - Exécuter le traitement associé à cpu_load_irq_end.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void cpu_load_irq_end(void)
{
    uint32_t end;
    uint32_t elapsed;
    uint32_t raw_pm;
    uint32_t pm;
    uint32_t avg_q8;

    if(cpu_counter_valid == 0U)
        return;

    end = DWT->CYCCNT;
    elapsed = end - irq_start_cycles;

    if(current_period_cycles == 0U)
        return;

    raw_pm = (uint32_t)(((uint64_t)elapsed * 1000ULL) /
                        (uint64_t)current_period_cycles);

    pm = raw_pm;

    if(pm > CPU_LOAD_MAX_PERMILLE)
        pm = CPU_LOAD_MAX_PERMILLE;

    cpu_last_permille = pm;

    if(cpu_block_count == 0U)
    {
        avg_q8 = pm << CPU_LOAD_AVG_SCALE_SHIFT;
    }
    else
    {
        const int32_t target_q8 = (int32_t)(pm << CPU_LOAD_AVG_SCALE_SHIFT);
        const int32_t current_q8 = (int32_t)cpu_avg_permille_q8;

        avg_q8 = (uint32_t)(current_q8 +
                            ((target_q8 - current_q8) >> CPU_LOAD_AVG_SHIFT));
    }

    cpu_avg_permille_q8 = avg_q8;

    if(pm > cpu_peak_permille)
        cpu_peak_permille = pm;

    recent_permille_ring[recent_permille_index] = (uint16_t)pm;
    recent_permille_index++;

    if(recent_permille_index >= CPU_LOAD_RECENT_WINDOW)
        recent_permille_index = 0U;

    if(recent_permille_count < CPU_LOAD_RECENT_WINDOW)
        recent_permille_count++;

    cpu_peak_recent_permille = cpu_load_compute_recent_peak();

    if(raw_pm > CPU_LOAD_THRESH_80_PERMILLE)
        cpu_over_80_count++;

    if(raw_pm > CPU_LOAD_THRESH_90_PERMILLE)
        cpu_over_90_count++;

    if(raw_pm > CPU_LOAD_THRESH_100_PERMILLE)
        cpu_over_100_count++;

    cpu_block_count++;
}

/**
 * @brief Point d'entrée cpu_load_get_permille.
 *
 * Rôle:
 * - Exécuter le traitement associé à cpu_load_get_permille.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
uint32_t cpu_load_get_permille(void)
{
    return cpu_last_permille;
}

/**
 * @brief Point d'entrée cpu_load_get_max.
 *
 * Rôle:
 * - Exécuter le traitement associé à cpu_load_get_max.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
uint32_t cpu_load_get_max(void)
{
    return cpu_peak_permille;
}

uint32_t cpu_load_get_avg_permille(void)
{
    return cpu_load_avg_permille_from_q8(cpu_avg_permille_q8);
}

uint32_t cpu_load_get_peak_recent_permille(void)
{
    return cpu_peak_recent_permille;
}

uint32_t cpu_load_get_over_80_count(void)
{
    return cpu_over_80_count;
}

uint32_t cpu_load_get_over_90_count(void)
{
    return cpu_over_90_count;
}

uint32_t cpu_load_get_over_100_count(void)
{
    return cpu_over_100_count;
}

uint32_t cpu_load_get_block_count(void)
{
    return cpu_block_count;
}

/**
 * @brief Point d'entrée cpu_load_is_valid.
 *
 * Rôle:
 * - Exécuter le traitement associé à cpu_load_is_valid.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
uint32_t cpu_load_is_valid(void)
{
    return cpu_counter_valid;
}

void cpu_load_get_metrics(cpu_load_metrics_t *metrics)
{
    uint32_t primask;

    if(metrics == 0)
        return;

    primask = __get_PRIMASK();
    __disable_irq();

    metrics->last_permille = cpu_last_permille;
    metrics->avg_permille = cpu_load_avg_permille_from_q8(cpu_avg_permille_q8);
    metrics->peak_permille = cpu_peak_permille;
    metrics->peak_recent_permille = cpu_peak_recent_permille;
    metrics->over_80_count = cpu_over_80_count;
    metrics->over_90_count = cpu_over_90_count;
    metrics->over_100_count = cpu_over_100_count;
    metrics->block_count = cpu_block_count;
    metrics->counter_valid = cpu_counter_valid;

    __set_PRIMASK(primask);
}

void cpu_load_reset_peak(void)
{
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    cpu_peak_permille = 0U;

    __set_PRIMASK(primask);
}

void cpu_load_reset_measurement(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();

    cpu_last_permille = 0U;
    cpu_avg_permille_q8 = 0U;
    cpu_peak_permille = 0U;
    cpu_peak_recent_permille = 0U;
    cpu_over_80_count = 0U;
    cpu_over_90_count = 0U;
    cpu_over_100_count = 0U;
    cpu_block_count = 0U;
    recent_permille_index = 0U;
    recent_permille_count = 0U;
    for (uint32_t i = 0U; i < CPU_LOAD_RECENT_WINDOW; ++i)
    {
        recent_permille_ring[i] = 0U;
    }

    __set_PRIMASK(primask);
}
