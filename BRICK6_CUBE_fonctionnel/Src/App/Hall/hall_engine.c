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
#define HALL_DEADZONE_PERCENT        12U

/* seuil d'armement de la vélocité (avant le vrai trigger) */
#define HALL_VEL_ARM_PERCENT          4U

/* premier seuil d’activation */
#define HALL_TRG1_PERCENT            16U

/* seuil profond permettant le retrigger rapide */
#define HALL_TRG2_PERCENT            70U

/* hystérésis pour éviter les oscillations autour des seuils */
#define HALL_HYST_PERCENT             6U

/* garde-fous temporels sur la mesure de vélocité */
#define HALL_VEL_TIMEOUT_SAMPLES     24U
#define HALL_VEL_FASTEST_SAMPLES      1U
#define HALL_VEL_SLOWEST_SAMPLES     12U

/*
===============================================================================
Seuils avec hystérésis
===============================================================================

On sépare les seuils ON et OFF pour éviter le "chatter" lorsque le signal
oscille autour d'un seuil.
*/

#define HALL_VEL_ARM_ON   HALL_VEL_ARM_PERCENT
#define HALL_VEL_ARM_OFF  0U

#define HALL_TRG1_ON      HALL_TRG1_PERCENT
#define HALL_TRG1_OFF     (HALL_TRG1_PERCENT - HALL_HYST_PERCENT)

#define HALL_TRG2_ON      HALL_TRG2_PERCENT
#define HALL_TRG2_OFF     (HALL_TRG2_PERCENT - HALL_HYST_PERCENT)

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

/*
===============================================================================
Stockage état runtime
===============================================================================
*/

static uint16_t hall_min[HALL_KEY_COUNT];
static uint16_t hall_max[HALL_KEY_COUNT];

static uint16_t hall_value[HALL_KEY_COUNT];
static uint16_t hall_position[HALL_KEY_COUNT];
static uint16_t hall_raw_latched[HALL_KEY_COUNT];
static uint16_t hall_prev_position[HALL_KEY_COUNT];
static uint8_t  hall_pressed[HALL_KEY_COUNT];
static uint8_t  hall_velocity[HALL_KEY_COUNT];
static uint8_t  hall_velocity_valid[HALL_KEY_COUNT];
static uint8_t  hall_velocity_active[HALL_KEY_COUNT];
static uint32_t hall_velocity_start_sample[HALL_KEY_COUNT];
static uint32_t hall_velocity_elapsed_samples[HALL_KEY_COUNT];
static uint16_t hall_velocity_start_pos[HALL_KEY_COUNT];

static hall_state_t hall_state[HALL_KEY_COUNT];

static uint8_t hall_calibrated = 0U;

static uint8_t hall_velocity_samples_to_midi(uint32_t elapsed_samples)
{
    if (elapsed_samples <= HALL_VEL_FASTEST_SAMPLES)
    {
        return 127U;
    }

    if (elapsed_samples >= HALL_VEL_SLOWEST_SAMPLES)
    {
        return 1U;
    }

    {
        const uint32_t numerator =
            (elapsed_samples - HALL_VEL_FASTEST_SAMPLES) * 126U;
        const uint32_t denominator =
            (HALL_VEL_SLOWEST_SAMPLES - HALL_VEL_FASTEST_SAMPLES);
        const uint32_t attenuated = numerator / denominator;
        const uint32_t velocity = 127U - attenuated;

        return (velocity > 127U) ? 127U : (uint8_t)velocity;
    }
}

static void hall_velocity_reset_runtime(uint8_t key)
{
    hall_velocity_active[key] = 0U;
    hall_velocity_start_sample[key] = 0U;
    hall_velocity_elapsed_samples[key] = 0U;
    hall_velocity_start_pos[key] = 0U;
    hall_raw_latched[key] = 0U;
}

static void hall_velocity_cancel(uint8_t key)
{
    hall_velocity_reset_runtime(key);
}

static void hall_velocity_arm(uint8_t key,
                              uint16_t raw,
                              uint16_t pos,
                              uint32_t sample_count)
{
    hall_velocity_active[key] = 1U;
    hall_velocity_start_sample[key] = sample_count;
    hall_velocity_elapsed_samples[key] = 0U;
    hall_velocity_start_pos[key] = pos;
    hall_raw_latched[key] = raw;
}

static void hall_velocity_commit(uint8_t key,
                                 uint16_t raw,
                                 uint32_t sample_count)
{
    uint32_t elapsed_samples = 0U;

    if (sample_count > hall_velocity_start_sample[key])
    {
        elapsed_samples = sample_count - hall_velocity_start_sample[key];
    }

    if (elapsed_samples == 0U)
    {
        elapsed_samples = 1U;
    }

    hall_velocity_elapsed_samples[key] = elapsed_samples;
    hall_raw_latched[key] = raw;
    hall_velocity[key] = hall_velocity_samples_to_midi(elapsed_samples);
    hall_velocity_valid[key] = 1U;
    hall_velocity_reset_runtime(key);
}

/*
===============================================================================
Initialisation
===============================================================================
*/

void hall_engine_init(void)
{
    hall_calibrated = 0U;

    for(uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        hall_min[i] = 0xFFFFU;
        hall_max[i] = 0U;

        hall_value[i] = 0U;
        hall_position[i] = 0U;
        hall_raw_latched[i] = 0U;
        hall_prev_position[i] = 0U;
        hall_pressed[i] = 0U;
        hall_velocity[i] = 1U;
        hall_velocity_valid[i] = 0U;
        hall_velocity_active[i] = 0U;
        hall_velocity_start_sample[i] = 0U;
        hall_velocity_elapsed_samples[i] = 0U;
        hall_velocity_start_pos[i] = 0U;

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
    if((min_values == 0) || (max_values == 0))
        return;

    hall_calibrated = 1U;

    for(uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
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
    for(uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        uint16_t raw;

        if(!hall_adc_consume_raw(i, &raw))
            continue;

        if(!hall_calibrated)
        {
            if(raw < hall_min[i]) hall_min[i] = raw;
            if(raw > hall_max[i]) hall_max[i] = raw;

            hall_value[i] = 0U;
            hall_position[i] = 0U;
            hall_prev_position[i] = 0U;
            hall_pressed[i] = 0U;
            hall_velocity_cancel(i);
            continue;
        }

        uint16_t range =
            (hall_max[i] > hall_min[i]) ?
            (hall_max[i] - hall_min[i]) : 0U;

        if(range < 10U)
        {
            hall_value[i] = 0U;
            hall_position[i] = 0U;
            hall_prev_position[i] = 0U;
            hall_pressed[i] = 0U;
            hall_velocity_cancel(i);
            continue;
        }

        {
            const uint32_t sample_count = hall_adc_get_sample_count(i);
            const uint32_t delta =
                (raw > hall_min[i]) ?
                (raw - hall_min[i]) : 0U;
            const uint32_t limited_delta =
                (delta > range) ? range : delta;
            uint16_t pos = (uint16_t)((limited_delta * 100U) / range);
            uint16_t value;
            const uint16_t prev_pos = hall_prev_position[i];

            if(pos > 100U)
                pos = 100U;

            hall_position[i] = pos;
            value = pos;

            if(value < HALL_DEADZONE_PERCENT)
                value = 0U;

            hall_value[i] = value;

            if(hall_velocity_active[i] != 0U)
            {
                const uint32_t elapsed_samples =
                    (sample_count >= hall_velocity_start_sample[i]) ?
                    (sample_count - hall_velocity_start_sample[i]) : 0U;

                if((pos <= HALL_VEL_ARM_OFF) || (pos < hall_velocity_start_pos[i]))
                {
                    hall_velocity_cancel(i);
                }
                else if(elapsed_samples > HALL_VEL_TIMEOUT_SAMPLES)
                {
                    hall_velocity_cancel(i);
                }
            }

            switch(hall_state[i])
            {
                case HALL_IDLE:
                    if((hall_velocity_active[i] == 0U) &&
                       (prev_pos < HALL_VEL_ARM_ON) &&
                       (pos >= HALL_VEL_ARM_ON) &&
                       (pos < HALL_TRG1_ON))
                    {
                        hall_velocity_arm(i, raw, pos, sample_count);
                    }

                    if(pos > HALL_TRG1_ON)
                    {
                        if(hall_velocity_active[i] != 0U)
                        {
                            hall_velocity_commit(i, raw, sample_count);
                        }
                        else
                        {
                            hall_velocity[i] = 127U;
                            hall_velocity_valid[i] = 1U;
                            hall_raw_latched[i] = raw;
                            hall_velocity_elapsed_samples[i] = 1U;
                        }

                        hall_pressed[i] = 1U;
                        hall_state[i] = HALL_PRESSED_T1;
                    }
                break;

                case HALL_PRESSED_T1:
                    if(pos > HALL_TRG2_ON)
                    {
                        hall_state[i] = HALL_PRESSED_T2;
                    }
                    else if(pos < HALL_TRG1_OFF)
                    {
                        hall_pressed[i] = 0U;
                        hall_velocity_cancel(i);
                        hall_state[i] = HALL_IDLE;
                    }
                break;

                case HALL_PRESSED_T2:
                    if(pos < HALL_TRG2_OFF)
                    {
                        hall_pressed[i] = 0U;
                        hall_velocity_cancel(i);
                        hall_state[i] = HALL_IDLE_T2;
                    }
                break;

                case HALL_IDLE_T2:
                    if(pos > HALL_TRG2_ON)
                    {
                        hall_pressed[i] = 1U;
                        hall_state[i] = HALL_PRESSED_T2;
                    }
                    else if(pos < HALL_TRG1_OFF)
                    {
                        hall_velocity_cancel(i);
                        hall_state[i] = HALL_IDLE;
                    }
                break;

                default:
                    hall_velocity_cancel(i);
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
    if(key >= HALL_KEY_COUNT)
        return 0U;

    return hall_value[key];
}


uint8_t hall_engine_is_pressed(uint8_t key)
{
    if(key >= HALL_KEY_COUNT)
        return 0U;

    return hall_pressed[key];
}


uint16_t hall_engine_get_min(uint8_t key)
{
    if(key >= HALL_KEY_COUNT)
        return 0U;

    return hall_min[key];
}


uint16_t hall_engine_get_max(uint8_t key)
{
    if(key >= HALL_KEY_COUNT)
        return 0U;

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

    debug->velocity_arm_threshold = HALL_VEL_ARM_ON;
    debug->trigger_threshold = HALL_TRG1_ON;
    debug->raw_latched = hall_raw_latched[key];
    debug->elapsed_samples_latched = hall_velocity_elapsed_samples[key];
    debug->velocity_latched = hall_velocity[key];
    debug->velocity_ready = hall_velocity_valid[key];
    debug->velocity_armed = hall_velocity_active[key];
    debug->position_percent = hall_position[key];
    debug->state = (uint8_t)hall_state[key];
    debug->sample_count = hall_adc_get_sample_count(key);
}
