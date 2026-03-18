#include "App/Hall/hall_engine.h"
#include "App/Hall/hall_adc.h"

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

/* cadence observée: TIM6 = 200 us, 1 échantillon jeté, 8 positions mux */
#define HALL_KEY_SAMPLE_PERIOD_US   3200U

/* fenêtre VEL1 : sortie de deadzone -> TRG1 */
#define HALL_VEL1_ARM_PERCENT        HALL_DEADZONE_PERCENT
#define HALL_VEL1_TIMEOUT_SAMPLES    12U
#define HALL_VEL1_FASTEST_SAMPLES     1U
#define HALL_VEL1_SLOWEST_SAMPLES     6U

/* fenêtre VEL2 : seuil dédié avant TRG2 -> TRG2 */
#define HALL_VEL2_ARM_PERCENT        52U
#define HALL_VEL2_TIMEOUT_SAMPLES    10U
#define HALL_VEL2_FASTEST_SAMPLES     1U
#define HALL_VEL2_SLOWEST_SAMPLES     8U

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

HALL_IDLE
    touche relâchée

HALL_PRESSED_T1
    appui déclenché via TRG1

HALL_PRESSED_T2
    appui profond (TRG2 atteint)

HALL_IDLE_T2
    touche relâchée après TRG2
    → permet retrigger rapide autour de TRG2
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
Stockage état runtime
===============================================================================
*/

static uint16_t hall_min[HALL_KEY_COUNT];
static uint16_t hall_max[HALL_KEY_COUNT];

static uint16_t hall_value[HALL_KEY_COUNT];
static uint16_t hall_position[HALL_KEY_COUNT];
static uint16_t hall_raw_current[HALL_KEY_COUNT];
static uint16_t hall_prev_position[HALL_KEY_COUNT];
static uint8_t  hall_pressed[HALL_KEY_COUNT];
static uint8_t  hall_velocity[HALL_KEY_COUNT];
static uint8_t  hall_velocity_valid[HALL_KEY_COUNT];

static hall_velocity_window_t hall_velocity_1[HALL_KEY_COUNT];
static hall_velocity_window_t hall_velocity_2[HALL_KEY_COUNT];

static hall_state_t hall_state[HALL_KEY_COUNT];

static uint8_t hall_calibrated = 0U;

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
        const uint32_t velocity = 127U - attenuated;

        return (velocity > 127U) ? 127U : (uint8_t)velocity;
    }
}

static void hall_velocity_window_reset_runtime(hall_velocity_window_t *window)
{
    if (window == 0)
    {
        return;
    }

    window->armed = 0U;
    window->start_pos = 0U;
    window->start_sample = 0U;
}

static void hall_velocity_window_cancel(hall_velocity_window_t *window)
{
    hall_velocity_window_reset_runtime(window);
}

static void hall_velocity_window_arm(hall_velocity_window_t *window,
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

static void hall_velocity_window_commit(hall_velocity_window_t *window,
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

static void hall_velocity_window_commit_fallback(hall_velocity_window_t *window,
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

        hall_value[i] = 0U;
        hall_position[i] = 0U;
        hall_raw_current[i] = 0U;
        hall_prev_position[i] = 0U;
        hall_pressed[i] = 0U;
        hall_velocity[i] = 1U;
        hall_velocity_valid[i] = 0U;

        hall_velocity_1[i].raw_latched = 0U;
        hall_velocity_1[i].start_pos = 0U;
        hall_velocity_1[i].start_sample = 0U;
        hall_velocity_1[i].elapsed_samples = 0U;
        hall_velocity_1[i].velocity_latched = 0U;
        hall_velocity_1[i].armed = 0U;
        hall_velocity_1[i].ready = 0U;
        hall_velocity_1[i].fallback = 0U;

        hall_velocity_2[i].raw_latched = 0U;
        hall_velocity_2[i].start_pos = 0U;
        hall_velocity_2[i].start_sample = 0U;
        hall_velocity_2[i].elapsed_samples = 0U;
        hall_velocity_2[i].velocity_latched = 0U;
        hall_velocity_2[i].armed = 0U;
        hall_velocity_2[i].ready = 0U;
        hall_velocity_2[i].fallback = 0U;

        hall_state[i] = HALL_IDLE;
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
Traitement principal capteurs
===============================================================================
*/

void hall_engine_process(void)
{
    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        uint16_t raw;

        if (!hall_adc_consume_raw(i, &raw))
        {
            continue;
        }

        hall_raw_current[i] = raw;

        if (!hall_calibrated)
        {
            if (raw < hall_min[i]) { hall_min[i] = raw; }
            if (raw > hall_max[i]) { hall_max[i] = raw; }

            hall_value[i] = 0U;
            hall_position[i] = 0U;
            hall_prev_position[i] = 0U;
            hall_pressed[i] = 0U;
            hall_velocity_windows_reset_key(i);
            continue;
        }

        {
            const uint16_t range =
                (hall_max[i] > hall_min[i]) ?
                (hall_max[i] - hall_min[i]) : 0U;

            if (range < 10U)
            {
                hall_value[i] = 0U;
                hall_position[i] = 0U;
                hall_prev_position[i] = 0U;
                hall_pressed[i] = 0U;
                hall_velocity_windows_reset_key(i);
                continue;
            }
        }

        {
            const uint16_t range = (uint16_t)(hall_max[i] - hall_min[i]);
            const uint32_t sample_count = hall_adc_get_sample_count(i);
            const uint32_t delta = (raw > hall_min[i]) ? (raw - hall_min[i]) : 0U;
            const uint32_t limited_delta = (delta > range) ? range : delta;
            uint16_t pos = (uint16_t)((limited_delta * 100U) / range);
            uint16_t value;
            const uint16_t prev_pos = hall_prev_position[i];

            if (pos > 100U)
            {
                pos = 100U;
            }

            hall_position[i] = pos;
            value = (pos < HALL_DEADZONE_PERCENT) ? 0U : pos;
            hall_value[i] = value;

            if (hall_velocity_1[i].armed != 0U)
            {
                const uint32_t elapsed_samples =
                    (sample_count >= hall_velocity_1[i].start_sample) ?
                    (sample_count - hall_velocity_1[i].start_sample) : 0U;

                if ((pos <= HALL_DEADZONE_EXIT_OFF) ||
                    (pos < hall_velocity_1[i].start_pos) ||
                    (elapsed_samples > HALL_VEL1_TIMEOUT_SAMPLES))
                {
                    hall_velocity_window_cancel(&hall_velocity_1[i]);
                }
            }

            if (hall_velocity_2[i].armed != 0U)
            {
                const uint32_t elapsed_samples =
                    (sample_count >= hall_velocity_2[i].start_sample) ?
                    (sample_count - hall_velocity_2[i].start_sample) : 0U;

                if ((pos <= HALL_VEL2_ARM_OFF) ||
                    (pos < hall_velocity_2[i].start_pos) ||
                    (elapsed_samples > HALL_VEL2_TIMEOUT_SAMPLES))
                {
                    hall_velocity_window_cancel(&hall_velocity_2[i]);
                }
            }

            switch (hall_state[i])
            {
                case HALL_IDLE:
                    if ((hall_velocity_1[i].armed == 0U) &&
                        (prev_pos < HALL_DEADZONE_EXIT_ON) &&
                        (pos >= HALL_DEADZONE_EXIT_ON) &&
                        (pos < HALL_TRG1_ON))
                    {
                        hall_velocity_window_arm(&hall_velocity_1[i], raw, pos, sample_count);
                    }

                    if (pos > HALL_TRG1_ON)
                    {
                        if (hall_velocity_1[i].armed != 0U)
                        {
                            hall_velocity_window_commit(&hall_velocity_1[i],
                                                        raw,
                                                        sample_count,
                                                        HALL_VEL1_FASTEST_SAMPLES,
                                                        HALL_VEL1_SLOWEST_SAMPLES);
                        }
                        else
                        {
                            hall_velocity_window_commit_fallback(&hall_velocity_1[i],
                                                                 raw,
                                                                 HALL_VEL1_FALLBACK_MIDI);
                        }

                        hall_velocity[i] = hall_velocity_1[i].velocity_latched;
                        hall_velocity_valid[i] = hall_velocity_1[i].ready;
                        hall_pressed[i] = 1U;
                        hall_state[i] = HALL_PRESSED_T1;
                    }
                break;

                case HALL_PRESSED_T1:
                    if ((hall_velocity_2[i].armed == 0U) &&
                        (prev_pos < HALL_VEL2_ARM_ON) &&
                        (pos >= HALL_VEL2_ARM_ON) &&
                        (pos < HALL_TRG2_ON))
                    {
                        hall_velocity_window_arm(&hall_velocity_2[i], raw, pos, sample_count);
                    }

                    if (pos > HALL_TRG2_ON)
                    {
                        if (hall_velocity_2[i].armed != 0U)
                        {
                            hall_velocity_window_commit(&hall_velocity_2[i],
                                                        raw,
                                                        sample_count,
                                                        HALL_VEL2_FASTEST_SAMPLES,
                                                        HALL_VEL2_SLOWEST_SAMPLES);
                        }
                        else
                        {
                            hall_velocity_window_commit_fallback(&hall_velocity_2[i],
                                                                 raw,
                                                                 HALL_VEL2_FALLBACK_MIDI);
                        }

                        hall_state[i] = HALL_PRESSED_T2;
                    }
                    else if (pos < HALL_TRG1_OFF)
                    {
                        hall_pressed[i] = 0U;
                        hall_velocity_windows_reset_key(i);
                        hall_state[i] = HALL_IDLE;
                    }
                break;

                case HALL_PRESSED_T2:
                    if (pos < HALL_TRG2_OFF)
                    {
                        hall_pressed[i] = 0U;
                        hall_velocity_windows_reset_key(i);
                        hall_state[i] = HALL_IDLE_T2;
                    }
                break;

                case HALL_IDLE_T2:
                    if ((hall_velocity_2[i].armed == 0U) &&
                        (prev_pos < HALL_VEL2_ARM_ON) &&
                        (pos >= HALL_VEL2_ARM_ON) &&
                        (pos < HALL_TRG2_ON))
                    {
                        hall_velocity_window_arm(&hall_velocity_2[i], raw, pos, sample_count);
                    }

                    if (pos > HALL_TRG2_ON)
                    {
                        if (hall_velocity_2[i].armed != 0U)
                        {
                            hall_velocity_window_commit(&hall_velocity_2[i],
                                                        raw,
                                                        sample_count,
                                                        HALL_VEL2_FASTEST_SAMPLES,
                                                        HALL_VEL2_SLOWEST_SAMPLES);
                        }
                        else
                        {
                            hall_velocity_window_commit_fallback(&hall_velocity_2[i],
                                                                 raw,
                                                                 HALL_VEL2_FALLBACK_MIDI);
                        }

                        hall_pressed[i] = 1U;
                        hall_state[i] = HALL_PRESSED_T2;
                    }
                    else if (pos < HALL_TRG1_OFF)
                    {
                        hall_velocity_windows_reset_key(i);
                        hall_state[i] = HALL_IDLE;
                    }
                break;

                default:
                    hall_velocity_windows_reset_key(i);
                    hall_state[i] = HALL_IDLE;
                break;
            }

            hall_prev_position[i] = pos;
        }
    }
}

/*
===============================================================================
Accesseurs
===============================================================================
*/

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

void hall_engine_get_velocity_debug(uint8_t key, hall_velocity_debug_t *debug)
{
    if ((key >= HALL_KEY_COUNT) || (debug == 0))
    {
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
    debug->sample_count = hall_adc_get_sample_count(key);
    debug->sample_period_us = HALL_KEY_SAMPLE_PERIOD_US;
    debug->velocity_latched = hall_velocity_1[key].velocity_latched;
    debug->velocity2_latched = hall_velocity_2[key].velocity_latched;
    debug->velocity_ready = hall_velocity_1[key].ready;
    debug->velocity2_ready = hall_velocity_2[key].ready;
    debug->velocity1_armed = hall_velocity_1[key].armed;
    debug->velocity2_armed = hall_velocity_2[key].armed;
    debug->velocity1_fallback = hall_velocity_1[key].fallback;
    debug->velocity2_fallback = hall_velocity_2[key].fallback;
    debug->state = (uint8_t)hall_state[key];
}
