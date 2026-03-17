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
#define HALL_DEADZONE_PERCENT 12U

/* premier seuil d’activation */
#define HALL_TRG1_PERCENT     16U

/* seuil profond permettant le retrigger rapide */
#define HALL_TRG2_PERCENT     70U

/* hystérésis pour éviter les oscillations autour des seuils */
#define HALL_HYST_PERCENT      6U

/*
===============================================================================
Seuils avec hystérésis
===============================================================================

On sépare les seuils ON et OFF pour éviter le "chatter" lorsque le signal
oscille autour d'un seuil.
*/

#define HALL_TRG1_ON   HALL_TRG1_PERCENT
#define HALL_TRG1_OFF  (HALL_TRG1_PERCENT - HALL_HYST_PERCENT)

#define HALL_TRG2_ON   HALL_TRG2_PERCENT
#define HALL_TRG2_OFF  (HALL_TRG2_PERCENT - HALL_HYST_PERCENT)

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
static uint8_t  hall_pressed[HALL_KEY_COUNT];

static hall_state_t hall_state[HALL_KEY_COUNT];

static uint8_t hall_calibrated = 0U;


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
        hall_pressed[i] = 0U;

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
        uint16_t raw = hall_adc_get_raw(i);

        /* phase auto calibration */
        if(!hall_calibrated)
        {
            if(raw < hall_min[i]) hall_min[i] = raw;
            if(raw > hall_max[i]) hall_max[i] = raw;

            hall_value[i] = 0U;
            hall_pressed[i] = 0U;
            continue;
        }

        /*
        ===========================================================================
        Conversion RAW → position %
        ===========================================================================
        */

        uint16_t range =
            (hall_max[i] > hall_min[i]) ?
            (hall_max[i] - hall_min[i]) : 0U;

        if(range < 10U)
        {
            hall_value[i] = 0U;
            hall_pressed[i] = 0U;
            continue;
        }

        uint32_t delta =
            (raw > hall_min[i]) ?
            (raw - hall_min[i]) : 0U;

        if(delta > range)
            delta = range;

        uint16_t pos = (delta * 100U) / range;

        if(pos > 100U)
            pos = 100U;

        if(pos < HALL_DEADZONE_PERCENT)
            pos = 0U;

        hall_value[i] = pos;

        /*
        ===========================================================================
        Machine d'état déclenchement
        ===========================================================================
        */

        switch(hall_state[i])
        {
            /*
            -----------------------------------------------------------------------
            état repos
            -----------------------------------------------------------------------
            */

            case HALL_IDLE:

                if(pos > HALL_TRG1_ON)
                {
                    hall_pressed[i] = 1U;
                    hall_state[i] = HALL_PRESSED_T1;
                }

            break;


            /*
            -----------------------------------------------------------------------
            appui normal (TRG1)
            -----------------------------------------------------------------------
            */

            case HALL_PRESSED_T1:

                /* si appui profond → mode retrigger rapide */
                if(pos > HALL_TRG2_ON)
                {
                    hall_state[i] = HALL_PRESSED_T2;
                }
                /* relâchement normal */
                else if(pos < HALL_TRG1_OFF)
                {
                    hall_pressed[i] = 0U;
                    hall_state[i] = HALL_IDLE;
                }

            break;


            /*
            -----------------------------------------------------------------------
            appui profond (TRG2)
            -----------------------------------------------------------------------
            */

            case HALL_PRESSED_T2:

                if(pos < HALL_TRG2_OFF)
                {
                    hall_pressed[i] = 0U;
                    hall_state[i] = HALL_IDLE_T2;
                }

            break;


            /*
            -----------------------------------------------------------------------
            mode retrigger rapide
            -----------------------------------------------------------------------
            */

            case HALL_IDLE_T2:

                /* retrigger rapide autour de TRG2 */
                if(pos > HALL_TRG2_ON)
                {
                    hall_pressed[i] = 1U;
                    hall_state[i] = HALL_PRESSED_T2;
                }
                /* retour au mode normal */
                else if(pos < HALL_TRG1_OFF)
                {
                    hall_state[i] = HALL_IDLE;
                }

            break;

            default:
                hall_state[i] = HALL_IDLE;
            break;
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
