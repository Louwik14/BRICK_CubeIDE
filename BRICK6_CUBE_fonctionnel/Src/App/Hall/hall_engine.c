#include "App/Hall/hall_engine.h"
#include "stm32h7xx_hal.h"

/*
===============================================================================
Configuration comportement capteur Hall
===============================================================================

Les valeurs sont exprimées en pourcentage de la course du capteur.

Course = (raw - min) / (max - min)

0%  = position repos
100% = position enfoncée maximum
*/

/* zone morte autour du repos pour ignorer le bruit ADC */
#define HALL_DEADZONE_PERCENT         12U

/* premier seuil d’activation */
#define HALL_TRG1_PERCENT             16U

/* seuil profond permettant le retrigger rapide */
#define HALL_TRG2_PERCENT             70U

/* hystérésis pour éviter les oscillations autour des seuils */
#define HALL_HYST_PERCENT              6U

/* cadence observée: TIM6 = 50 us (20 kHz), 1 échantillon jeté, 8 positions mux */
#define HALL_KEY_SAMPLE_PERIOD_US   800U

#define HALL_US_TO_KEY_SAMPLES(us_) (((us_) + HALL_KEY_SAMPLE_PERIOD_US - 1U) / HALL_KEY_SAMPLE_PERIOD_US)

/* fenêtre VEL1 : sortie de deadzone -> TRG1 */
#define HALL_VEL1_ARM_PERCENT         HALL_DEADZONE_PERCENT
#define HALL_VEL1_TIMEOUT_SAMPLES     HALL_US_TO_KEY_SAMPLES(38400U)
#define HALL_VEL1_FASTEST_SAMPLES     HALL_US_TO_KEY_SAMPLES(3200U)
#define HALL_VEL1_SLOWEST_SAMPLES     HALL_US_TO_KEY_SAMPLES(19200U)

/* fenêtre VEL2 : seuil dédié avant TRG2 -> TRG2 */
#define HALL_VEL2_ARM_PERCENT         52U
#define HALL_VEL2_TIMEOUT_SAMPLES     HALL_US_TO_KEY_SAMPLES(32000U)
#define HALL_VEL2_FASTEST_SAMPLES     HALL_US_TO_KEY_SAMPLES(3200U)
#define HALL_VEL2_SLOWEST_SAMPLES     HALL_US_TO_KEY_SAMPLES(25600U)

/* fallback explicite si une fenêtre n'a pas pu être armée proprement */
#define HALL_VEL1_FALLBACK_MIDI      96U
#define HALL_VEL2_FALLBACK_MIDI      88U

/*
===============================================================================
Seuils avec hystérésis
===============================================================================
*/

#define HALL_DEADZONE_EXIT_ON  HALL_VEL1_ARM_PERCENT
#define HALL_DEADZONE_EXIT_OFF (HALL_DEADZONE_PERCENT - HALL_HYST_PERCENT)

#define HALL_TRG1_ON           HALL_TRG1_PERCENT
#define HALL_TRG1_OFF          (HALL_TRG1_PERCENT - HALL_HYST_PERCENT)

#define HALL_VEL2_ARM_ON       HALL_VEL2_ARM_PERCENT
#define HALL_VEL2_ARM_OFF      (HALL_VEL2_ARM_PERCENT - HALL_HYST_PERCENT)

#define HALL_TRG2_ON           HALL_TRG2_PERCENT
#define HALL_TRG2_OFF          (HALL_TRG2_PERCENT - HALL_HYST_PERCENT)

/*
===============================================================================
Machine d'état du capteur
===============================================================================

Toute la logique critique Hall s'exécute désormais dans l'IRQ ADC/DMA Hall,
exactement au rythme des samples ADC utiles, afin de ne plus perdre les
franchissements intermédiaires entre IRQ et superloop.
*/

typedef enum
{
    HALL_IDLE = 0,
    HALL_PRESSED_T1,
    HALL_PRESSED_T2,
    HALL_IDLE_T2
} hall_state_t;

typedef struct
{
    uint16_t raw_latched;
    uint16_t start_pos;
    uint32_t start_sample;
    uint32_t elapsed_samples;
    uint8_t velocity_latched;
    uint8_t armed;
    uint8_t ready;
    uint8_t fallback;
} hall_velocity_window_t;

/*
===============================================================================
Stockage état runtime partagé IRQ ADC Hall / superloop
===============================================================================
*/

static volatile uint16_t hall_min[HALL_KEY_COUNT];
static volatile uint16_t hall_max[HALL_KEY_COUNT];

static volatile uint16_t hall_value[HALL_KEY_COUNT];
static volatile uint16_t hall_position[HALL_KEY_COUNT];
static volatile uint16_t hall_raw_current[HALL_KEY_COUNT];
static volatile uint16_t hall_prev_position[HALL_KEY_COUNT];
static volatile uint32_t hall_sample_count_current[HALL_KEY_COUNT];
static volatile uint8_t  hall_pressed[HALL_KEY_COUNT];
static volatile uint8_t  hall_velocity[HALL_KEY_COUNT];
/* état latché durable : vitesse valide pour l'appui actuellement actif */
static volatile uint8_t  hall_velocity_valid[HALL_KEY_COUNT];
static volatile uint8_t  hall_note_on_pending[HALL_KEY_COUNT];
static volatile uint8_t  hall_note_off_pending[HALL_KEY_COUNT];

static volatile hall_velocity_window_t hall_velocity_1[HALL_KEY_COUNT];
static volatile hall_velocity_window_t hall_velocity_2[HALL_KEY_COUNT];

static volatile hall_state_t hall_state[HALL_KEY_COUNT];

static volatile uint8_t hall_calibrated = 0U;

static uint32_t hall_enter_critical(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void hall_exit_critical(uint32_t primask)
{
    __set_PRIMASK(primask);
}

static uint8_t hall_consume_flag(volatile uint8_t *flag)
{
    uint8_t pending;
    const uint32_t primask = hall_enter_critical();

    pending = *flag;
    *flag = 0U;

    hall_exit_critical(primask);
    return pending;
}


static uint8_t hall_velocity_samples_to_midi(uint32_t elapsed_samples,
                                             uint32_t fastest_samples,
                                             uint32_t slowest_samples)
{
    if (elapsed_samples <= fastest_samples)
    {
        return 127U;
    }

    if (elapsed_samples >= slowest_samples)
    {
        return 1U;
    }

    {
        const uint32_t numerator = (elapsed_samples - fastest_samples) * 126U;
        const uint32_t denominator = (slowest_samples - fastest_samples);
        const uint32_t attenuated = numerator / denominator;
        const uint32_t velocity_value = 127U - attenuated;

        return (velocity_value > 127U) ? 127U : (uint8_t)velocity_value;
    }
}

static void hall_velocity_window_reset_runtime(volatile hall_velocity_window_t *window)
{
    if (window == 0)
    {
        return;
    }

    window->armed = 0U;
    window->start_pos = 0U;
    window->start_sample = 0U;
}

static void hall_velocity_window_cancel(volatile hall_velocity_window_t *window)
{
    hall_velocity_window_reset_runtime(window);
}

static void hall_velocity_window_arm(volatile hall_velocity_window_t *window,
                                     uint16_t raw,
                                     uint16_t pos,
                                     uint32_t sample_count)
{
    if (window == 0)
    {
        return;
    }

    window->raw_latched = raw;
    window->start_pos = pos;
    window->start_sample = sample_count;
    window->elapsed_samples = 0U;
    window->velocity_latched = 0U;
    window->ready = 0U;
    window->fallback = 0U;
    window->armed = 1U;
}

static void hall_velocity_window_commit(volatile hall_velocity_window_t *window,
                                        uint16_t raw,
                                        uint32_t sample_count,
                                        uint32_t fastest_samples,
                                        uint32_t slowest_samples)
{
    uint32_t elapsed_samples = 0U;

    if (window == 0)
    {
        return;
    }

    if (sample_count > window->start_sample)
    {
        elapsed_samples = sample_count - window->start_sample;
    }

    if (elapsed_samples == 0U)
    {
        elapsed_samples = 1U;
    }

    window->raw_latched = raw;
    window->elapsed_samples = elapsed_samples;
    window->velocity_latched = hall_velocity_samples_to_midi(elapsed_samples,
                                                             fastest_samples,
                                                             slowest_samples);
    window->ready = 1U;
    window->fallback = 0U;
    hall_velocity_window_reset_runtime(window);
}

static void hall_velocity_window_commit_fallback(volatile hall_velocity_window_t *window,
                                                 uint16_t raw,
                                                 uint8_t fallback_velocity)
{
    if (window == 0)
    {
        return;
    }

    window->raw_latched = raw;
    window->elapsed_samples = 0U;
    window->velocity_latched = fallback_velocity;
    window->ready = 1U;
    window->fallback = 1U;
    hall_velocity_window_reset_runtime(window);
}

static void hall_velocity_windows_reset_key(uint8_t key)
{
    hall_velocity_window_cancel(&hall_velocity_1[key]);
    hall_velocity_window_cancel(&hall_velocity_2[key]);
}

static void hall_engine_reset_key_runtime(uint8_t key)
{
    hall_value[key] = 0U;
    hall_position[key] = 0U;
    hall_prev_position[key] = 0U;
    hall_pressed[key] = 0U;
    hall_velocity[key] = 1U;
    hall_velocity_valid[key] = 0U;
    hall_note_on_pending[key] = 0U;
    hall_note_off_pending[key] = 0U;
    hall_velocity_1[key].raw_latched = 0U;
    hall_velocity_1[key].start_pos = 0U;
    hall_velocity_1[key].start_sample = 0U;
    hall_velocity_1[key].elapsed_samples = 0U;
    hall_velocity_1[key].velocity_latched = 0U;
    hall_velocity_1[key].armed = 0U;
    hall_velocity_1[key].ready = 0U;
    hall_velocity_1[key].fallback = 0U;
    hall_velocity_2[key].raw_latched = 0U;
    hall_velocity_2[key].start_pos = 0U;
    hall_velocity_2[key].start_sample = 0U;
    hall_velocity_2[key].elapsed_samples = 0U;
    hall_velocity_2[key].velocity_latched = 0U;
    hall_velocity_2[key].armed = 0U;
    hall_velocity_2[key].ready = 0U;
    hall_velocity_2[key].fallback = 0U;
    hall_state[key] = HALL_IDLE;
}

/*
===============================================================================
Initialisation
===============================================================================
*/

void hall_engine_init(void)
{
    hall_calibrated = 0U;

    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        hall_min[i] = 0xFFFFU;
        hall_max[i] = 0U;
        hall_raw_current[i] = 0U;
        hall_sample_count_current[i] = 0U;
        hall_engine_reset_key_runtime(i);
    }
}

/*
===============================================================================
Injection calibration min/max
===============================================================================
*/

void hall_engine_set_calibration(const uint16_t *min_values,
                                 const uint16_t *max_values)
{
    if ((min_values == 0) || (max_values == 0))
    {
        return;
    }

    hall_calibrated = 1U;

    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        hall_min[i] = min_values[i];
        hall_max[i] = max_values[i];
    }
}

/*
===============================================================================
Traitement critique sample par sample - IRQ ADC Hall
===============================================================================
*/

void hall_engine_process_sample(uint8_t key, uint16_t raw, uint32_t sample_count)
{
    uint16_t pos;
    uint16_t value;
    uint16_t prev_pos;
    uint16_t range;
    uint32_t delta;
    uint32_t limited_delta;

    if (key >= HALL_KEY_COUNT)
    {
        return;
    }

    hall_raw_current[key] = raw;
    hall_sample_count_current[key] = sample_count;

    if (hall_calibrated == 0U)
    {
        if (raw < hall_min[key]) { hall_min[key] = raw; }
        if (raw > hall_max[key]) { hall_max[key] = raw; }

        hall_engine_reset_key_runtime(key);
        return;
    }

    range = (hall_max[key] > hall_min[key]) ? (uint16_t)(hall_max[key] - hall_min[key]) : 0U;

    if (range < 10U)
    {
        hall_engine_reset_key_runtime(key);
        return;
    }

    delta = (raw > hall_min[key]) ? (uint32_t)(raw - hall_min[key]) : 0U;
    limited_delta = (delta > range) ? range : delta;
    pos = (uint16_t)((limited_delta * 100U) / range);

    if (pos > 100U)
    {
        pos = 100U;
    }

    prev_pos = hall_prev_position[key];
    value = (pos < HALL_DEADZONE_PERCENT) ? 0U : pos;

    hall_position[key] = pos;
    hall_value[key] = value;

    if (hall_velocity_1[key].armed != 0U)
    {
        const uint32_t elapsed_samples =
            (sample_count >= hall_velocity_1[key].start_sample) ?
            (sample_count - hall_velocity_1[key].start_sample) : 0U;

        if ((pos <= HALL_DEADZONE_EXIT_OFF) ||
            (pos < hall_velocity_1[key].start_pos) ||
            (elapsed_samples > HALL_VEL1_TIMEOUT_SAMPLES))
        {
            hall_velocity_window_cancel(&hall_velocity_1[key]);
        }
    }

    if (hall_velocity_2[key].armed != 0U)
    {
        const uint32_t elapsed_samples =
            (sample_count >= hall_velocity_2[key].start_sample) ?
            (sample_count - hall_velocity_2[key].start_sample) : 0U;

        if ((pos <= HALL_VEL2_ARM_OFF) ||
            (pos < hall_velocity_2[key].start_pos) ||
            (elapsed_samples > HALL_VEL2_TIMEOUT_SAMPLES))
        {
            hall_velocity_window_cancel(&hall_velocity_2[key]);
        }
    }

    switch (hall_state[key])
    {
        case HALL_IDLE:
            if ((hall_velocity_1[key].armed == 0U) &&
                (prev_pos < HALL_DEADZONE_EXIT_ON) &&
                (pos >= HALL_DEADZONE_EXIT_ON) &&
                (pos < HALL_TRG1_ON))
            {
                hall_velocity_window_arm(&hall_velocity_1[key], raw, pos, sample_count);
            }

            if (pos > HALL_TRG1_ON)
            {
                if (hall_velocity_1[key].armed != 0U)
                {
                    hall_velocity_window_commit(&hall_velocity_1[key],
                                                raw,
                                                sample_count,
                                                HALL_VEL1_FASTEST_SAMPLES,
                                                HALL_VEL1_SLOWEST_SAMPLES);
                }
                else
                {
                    hall_velocity_window_commit_fallback(&hall_velocity_1[key],
                                                         raw,
                                                         HALL_VEL1_FALLBACK_MIDI);
                }

                hall_velocity[key] = hall_velocity_1[key].velocity_latched;
                hall_velocity_valid[key] = hall_velocity_1[key].ready;
                hall_pressed[key] = 1U;
                hall_note_on_pending[key] = 1U;
                hall_state[key] = HALL_PRESSED_T1;
            }
        break;

        case HALL_PRESSED_T1:
            if ((hall_velocity_2[key].armed == 0U) &&
                (prev_pos < HALL_VEL2_ARM_ON) &&
                (pos >= HALL_VEL2_ARM_ON) &&
                (pos < HALL_TRG2_ON))
            {
                hall_velocity_window_arm(&hall_velocity_2[key], raw, pos, sample_count);
            }

            if (pos > HALL_TRG2_ON)
            {
                if (hall_velocity_2[key].armed != 0U)
                {
                    hall_velocity_window_commit(&hall_velocity_2[key],
                                                raw,
                                                sample_count,
                                                HALL_VEL2_FASTEST_SAMPLES,
                                                HALL_VEL2_SLOWEST_SAMPLES);
                }
                else
                {
                    hall_velocity_window_commit_fallback(&hall_velocity_2[key],
                                                         raw,
                                                         HALL_VEL2_FALLBACK_MIDI);
                }

                hall_state[key] = HALL_PRESSED_T2;
            }
            else if (pos < HALL_TRG1_OFF)
            {
                hall_pressed[key] = 0U;
                hall_velocity_valid[key] = 0U;
                hall_note_off_pending[key] = 1U;
                hall_velocity_windows_reset_key(key);
                hall_state[key] = HALL_IDLE;
            }
        break;

        case HALL_PRESSED_T2:
            if (pos < HALL_TRG2_OFF)
            {
                hall_pressed[key] = 0U;
                hall_velocity_valid[key] = 0U;
                hall_note_off_pending[key] = 1U;
                hall_velocity_windows_reset_key(key);
                hall_state[key] = HALL_IDLE_T2;
            }
        break;

        case HALL_IDLE_T2:
            if ((hall_velocity_2[key].armed == 0U) &&
                (prev_pos < HALL_VEL2_ARM_ON) &&
                (pos >= HALL_VEL2_ARM_ON) &&
                (pos < HALL_TRG2_ON))
            {
                hall_velocity_window_arm(&hall_velocity_2[key], raw, pos, sample_count);
            }

            if (pos > HALL_TRG2_ON)
            {
                if (hall_velocity_2[key].armed != 0U)
                {
                    hall_velocity_window_commit(&hall_velocity_2[key],
                                                raw,
                                                sample_count,
                                                HALL_VEL2_FASTEST_SAMPLES,
                                                HALL_VEL2_SLOWEST_SAMPLES);
                }
                else
                {
                    hall_velocity_window_commit_fallback(&hall_velocity_2[key],
                                                         raw,
                                                         HALL_VEL2_FALLBACK_MIDI);
                }

                hall_pressed[key] = 1U;
                hall_note_on_pending[key] = 1U;
                hall_state[key] = HALL_PRESSED_T2;
            }
            else if (pos < HALL_TRG1_OFF)
            {
                hall_velocity_windows_reset_key(key);
                hall_state[key] = HALL_IDLE;
            }
        break;

        default:
            hall_velocity_windows_reset_key(key);
            hall_state[key] = HALL_IDLE;
        break;
    }

    hall_prev_position[key] = pos;
}

/*
===============================================================================
Compatibilité superloop : plus aucun recalcul Hall fin ici
===============================================================================
*/

void hall_engine_process(void)
{
}

/*
===============================================================================
Accesseurs
===============================================================================
*/

uint16_t hall_engine_get_raw(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_raw_current[key];
}

uint16_t hall_engine_get_value(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_value[key];
}

uint8_t hall_engine_is_pressed(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_pressed[key];
}

uint16_t hall_engine_get_min(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_min[key];
}

uint16_t hall_engine_get_max(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_max[key];
}

uint8_t hall_engine_get_velocity(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_velocity[key];
}

uint8_t hall_engine_get_velocity_valid(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_velocity_valid[key];
}

uint16_t hall_engine_get_velocity_position(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_position[key];
}

uint32_t hall_engine_get_sample_count(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_sample_count_current[key];
}

uint8_t hall_engine_consume_note_on(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_consume_flag(&hall_note_on_pending[key]);
}

uint8_t hall_engine_consume_note_off(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_consume_flag(&hall_note_off_pending[key]);
}

void hall_engine_get_velocity_debug(uint8_t key, hall_velocity_debug_t *debug)
{
    const uint32_t primask = hall_enter_critical();

    if ((key >= HALL_KEY_COUNT) || (debug == 0))
    {
        hall_exit_critical(primask);
        return;
    }

    debug->raw_current = hall_raw_current[key];
    debug->min_current = hall_min[key];
    debug->max_current = hall_max[key];
    debug->position_percent = hall_position[key];
    debug->velocity1_arm_threshold = HALL_DEADZONE_EXIT_ON;
    debug->trigger1_threshold = HALL_TRG1_ON;
    debug->velocity2_arm_threshold = HALL_VEL2_ARM_ON;
    debug->trigger2_threshold = HALL_TRG2_ON;
    debug->velocity1_raw_latched = hall_velocity_1[key].raw_latched;
    debug->velocity2_raw_latched = hall_velocity_2[key].raw_latched;
    debug->velocity1_elapsed_samples = hall_velocity_1[key].elapsed_samples;
    debug->velocity2_elapsed_samples = hall_velocity_2[key].elapsed_samples;
    debug->sample_count = hall_sample_count_current[key];
    debug->sample_period_us = HALL_KEY_SAMPLE_PERIOD_US;
    debug->velocity_latched = hall_velocity_1[key].velocity_latched;
    debug->velocity2_latched = hall_velocity_2[key].velocity_latched;
    debug->velocity_ready = hall_velocity_1[key].ready;
    debug->velocity2_ready = hall_velocity_2[key].ready;
    debug->velocity1_armed = hall_velocity_1[key].armed;
    debug->velocity2_armed = hall_velocity_2[key].armed;
    debug->velocity1_fallback = hall_velocity_1[key].fallback;
    debug->velocity2_fallback = hall_velocity_2[key].fallback;
    debug->note_on_pending = hall_note_on_pending[key];
    debug->note_off_pending = hall_note_off_pending[key];
    debug->state = (uint8_t)hall_state[key];

    hall_exit_critical(primask);
}
