#include "drv_hall.h"

#include "brick_asc.h"
#include "brick_cal.h"

#include "ch.h"
#include "hal.h"

#include <limits.h>
#include <string.h>

#define HALL_ADC_RESOLUTION       12U
#define HALL_FILTER_FACTOR        8U
#define HALL_PRESS_NUM            4U
#define HALL_PRESS_DEN            10U
#define HALL_RELEASE_NUM          6U
#define HALL_RELEASE_DEN          10U
#define HALL_DETENT_DEN           20U
#define HALL_SETTLE_US            8U
#define HALL_NORMALIZE_MAX        127U
#define HALL_VELOCITY_MIN_US      500U
#define HALL_VELOCITY_MAX_US      30000U

#define HALL_LINE_ADC1            PAL_LINE(GPIOC, 4U) /* ADC1 INP4 */
#define HALL_LINE_ADC2            PAL_LINE(GPIOA, 7U) /* ADC1 INP7 */
#define HALL_LINE_MUX_S0          PAL_LINE(GPIOA, 5U)
#define HALL_LINE_MUX_S1          PAL_LINE(GPIOA, 4U)
#define HALL_LINE_MUX_S2          PAL_LINE(GPIOA, 6U)

/* ADC1 INP4 */
static const ADCConversionGroup adcgrpcfg1 = {
    .circular     = FALSE,
    .num_channels = 1,
    .end_cb       = NULL,
    .error_cb     = NULL,
    .cfgr         = 0,
    .cfgr2        = 0,
    .smpr         = {
        (ADC_SMPR_SMP_64P5 << ADC_SMPR1_SMP4_Pos),
        0
    },
    .sqr          = {
        ADC_SQR1_SQ1_N(4),
        0,
        0,
        0
    }
};

/* ADC1 INP7 */
static const ADCConversionGroup adcgrpcfg2 = {
    .circular     = FALSE,
    .num_channels = 1,
    .end_cb       = NULL,
    .error_cb     = NULL,
    .cfgr         = 0,
    .cfgr2        = 0,
    .smpr         = {
        (ADC_SMPR_SMP_64P5 << ADC_SMPR1_SMP7_Pos),
        0
    },
    .sqr          = {
        ADC_SQR1_SQ1_N(7),
        0,
        0,
        0
    }
};

static adcsample_t adc_sample1;
static adcsample_t adc_sample2;

static hall_state_t hall_state[BRICK_NUM_HALL_SENSORS];
static struct brick_asc asc_state[BRICK_NUM_HALL_SENSORS];
static struct brick_cal_pot cal_state;
static systime_t velocity_start_time[BRICK_NUM_HALL_SENSORS];
static bool velocity_armed[BRICK_NUM_HALL_SENSORS];
static bool hall_initialized;

static inline void mux_select(uint8_t index) {
    palWriteLine(HALL_LINE_MUX_S0, (index >> 0) & 1U);
    palWriteLine(HALL_LINE_MUX_S1, (index >> 1) & 1U);
    palWriteLine(HALL_LINE_MUX_S2, (index >> 2) & 1U);
}

static inline uint8_t clamp_uint8(int32_t v) {
    if (v < 0) return 0;
    if (v > HALL_NORMALIZE_MAX) return HALL_NORMALIZE_MAX;
    return (uint8_t)v;
}

static void hall_state_init(void) {
    memset(hall_state, 0, sizeof(hall_state));
    memset(asc_state, 0, sizeof(asc_state));
    memset(velocity_start_time, 0, sizeof(velocity_start_time));
    memset(velocity_armed, 0, sizeof(velocity_armed));

    for (uint8_t i = 0; i < BRICK_NUM_HALL_SENSORS; ++i) {
        hall_state[i].min = UINT16_MAX;
        hall_state[i].max = 0;
        hall_state[i].threshold = 0;
        hall_state[i].hysteresis = 0;
        hall_state[i].last_raw = UINT16_MAX;
    }

    brick_asc_array_set_factors(
        asc_state,
        BRICK_NUM_HALL_SENSORS,
        0,
        BRICK_NUM_HALL_SENSORS,
        HALL_FILTER_FACTOR
    );

    brick_cal_pot_init(&cal_state, HALL_ADC_RESOLUTION, BRICK_NUM_HALL_SENSORS);
    brick_cal_pot_enable_range(&cal_state, 0, BRICK_NUM_HALL_SENSORS);
}

static uint8_t normalize_with_deadzone(uint16_t value, uint16_t min, uint16_t max,
                                       uint16_t detent_low, uint16_t detent_high) {
    if (detent_low < min) detent_low = min;
    if (detent_high > max) detent_high = max;
    if (detent_low > detent_high) detent_low = detent_high;

    int32_t range = (int32_t)(max - min);
    if (range <= 0) range = 1;

    int32_t base = ((int32_t)max - (int32_t)value) * (int32_t)HALL_NORMALIZE_MAX / range;
    if (value <= detent_low || detent_low == detent_high) return clamp_uint8(base);
    if (value >= detent_high) return clamp_uint8(base);

    int32_t n_low  = ((int32_t)max - (int32_t)detent_low)  * (int32_t)HALL_NORMALIZE_MAX / range;
    int32_t n_high = ((int32_t)max - (int32_t)detent_high) * (int32_t)HALL_NORMALIZE_MAX / range;
    int32_t span = (int32_t)(detent_high - detent_low);
    int32_t pos  = (int32_t)(value - detent_low);
    int32_t interp = n_low + (n_high - n_low) * pos / (span == 0 ? 1 : span);
    return clamp_uint8(interp);
}

static uint16_t compute_detent_width(uint16_t range) {
    uint16_t width = range / HALL_DETENT_DEN;
    if (width == 0) width = 1;
    return width;
}

static uint8_t compute_velocity(uint8_t index, uint16_t calibrated,
                                uint16_t start_th, uint16_t end_th, systime_t now) {
    if (index >= BRICK_NUM_HALL_SENSORS) return 0;

    if (!velocity_armed[index]) {
        if (hall_state[index].last_raw > start_th && calibrated <= start_th) {
            velocity_start_time[index] = now;
            velocity_armed[index] = true;
        }
        return hall_state[index].velocity;
    }

    if (calibrated > start_th) {
        velocity_armed[index] = false;
        return hall_state[index].velocity;
    }

    if (calibrated <= end_th) {
        uint32_t dt_us = TIME_I2US(chVTTimeElapsedSinceX(velocity_start_time[index]));
        if (dt_us < HALL_VELOCITY_MIN_US) dt_us = HALL_VELOCITY_MIN_US;
        if (dt_us > HALL_VELOCITY_MAX_US) dt_us = HALL_VELOCITY_MAX_US;

        uint32_t span = HALL_VELOCITY_MAX_US - HALL_VELOCITY_MIN_US;
        uint32_t scaled = (HALL_VELOCITY_MAX_US - dt_us) * HALL_NORMALIZE_MAX / span;
        velocity_armed[index] = false;
        return clamp_uint8((int32_t)scaled);
    }

    return hall_state[index].velocity;
}

static void update_state(uint8_t index, uint16_t raw_sample, uint16_t filtered_sample) {
    if (index >= BRICK_NUM_HALL_SENSORS) return;

    hall_state_t* st = &hall_state[index];
    uint16_t calibrated = 0;

    if (brick_cal_pot_next(&cal_state, index, filtered_sample, &calibrated) != 0) return;

    uint16_t new_min = st->min;
    uint16_t new_max = st->max;

    if (cal_state.min[index] < new_min) new_min = cal_state.min[index];
    if (cal_state.max[index] > new_max) new_max = cal_state.max[index];
    if (new_max < new_min) new_max = new_min;

    uint16_t range = (uint16_t)(new_max - new_min);
    if (range == 0) range = 1;

    uint16_t press_th   = (uint16_t)(new_min + (uint32_t)range * HALL_PRESS_NUM   / HALL_PRESS_DEN);
    uint16_t release_th = (uint16_t)(new_min + (uint32_t)range * HALL_RELEASE_NUM / HALL_RELEASE_DEN);
    if (release_th <= press_th) release_th = (uint16_t)(press_th + 1U);
    if (release_th > new_max) release_th = new_max;
    if (release_th < press_th) release_th = press_th;

    uint16_t detent_width = compute_detent_width(range);
    uint16_t center = (uint16_t)(new_min + range / 2U);
    uint16_t detent_low  = (center > detent_width / 2U) ? (uint16_t)(center - detent_width / 2U) : new_min;
    uint16_t detent_high = center + detent_width / 2U;
    if (detent_high > new_max) detent_high = new_max;

    uint8_t new_value = normalize_with_deadzone(calibrated, new_min, new_max, detent_low, detent_high);

    systime_t now = chVTGetSystemTimeX();
    uint8_t new_velocity = compute_velocity(index, calibrated, release_th, press_th, now);

    bool prev_pressed = st->pressed;
    bool new_pressed  = prev_pressed;
    if (calibrated <= press_th) new_pressed = true;
    else if (calibrated >= release_th) new_pressed = false;

    uint32_t new_last_time = st->last_time;
    if (new_pressed != prev_pressed) new_last_time = TIME_I2US(now);

    st->raw       = raw_sample;
    st->filtered  = calibrated;
    st->min       = new_min;
    st->max       = new_max;
    st->threshold = press_th;
    st->hysteresis= release_th;
    st->value     = new_value;
    st->velocity  = new_velocity;
    st->pressed   = new_pressed;
    st->last_time = new_last_time;
    st->last_raw  = calibrated;
}

void drv_hall_init(void) {
    if (hall_initialized) return;

    palSetLineMode(HALL_LINE_ADC1, PAL_MODE_INPUT_ANALOG);
    palSetLineMode(HALL_LINE_ADC2, PAL_MODE_INPUT_ANALOG);

    palSetLineMode(HALL_LINE_MUX_S0, PAL_MODE_OUTPUT_PUSHPULL);
    palSetLineMode(HALL_LINE_MUX_S1, PAL_MODE_OUTPUT_PUSHPULL);
    palSetLineMode(HALL_LINE_MUX_S2, PAL_MODE_OUTPUT_PUSHPULL);

    palClearLine(HALL_LINE_MUX_S0);
    palClearLine(HALL_LINE_MUX_S1);
    palClearLine(HALL_LINE_MUX_S2);

    adcStart(&ADCD1, NULL);

    hall_state_init();
    hall_initialized = true;
}

void drv_hall_task(void) {
    for (uint8_t mux = 0; mux < BRICK_HALL_MUX_CHANNELS; ++mux) {
        mux_select(mux);
        chThdSleepMicroseconds(HALL_SETTLE_US);

        adcConvert(&ADCD1, &adcgrpcfg1, &adc_sample1, 1);
        adcConvert(&ADCD1, &adcgrpcfg2, &adc_sample2, 1);

        uint16_t filtered1 = 0;
        uint16_t filtered2 = 0;

        (void)brick_asc_process(&asc_state[mux], adc_sample1, &filtered1);
        update_state(mux, adc_sample1, filtered1);

        (void)brick_asc_process(&asc_state[mux + BRICK_HALL_MUX_CHANNELS], adc_sample2, &filtered2);
        update_state((uint8_t)(mux + BRICK_HALL_MUX_CHANNELS), adc_sample2, filtered2);
    }
}

uint16_t drv_hall_get_raw(uint8_t i) {
    if (i >= BRICK_NUM_HALL_SENSORS) return 0;
    return hall_state[i].raw;
}

uint8_t drv_hall_get_value(uint8_t i) {
    if (i >= BRICK_NUM_HALL_SENSORS) return 0;
    return hall_state[i].value;
}

uint8_t drv_hall_get_velocity(uint8_t i) {
    if (i >= BRICK_NUM_HALL_SENSORS) return 0;
    return hall_state[i].velocity;
}

bool drv_hall_is_pressed(uint8_t i) {
    if (i >= BRICK_NUM_HALL_SENSORS) return false;
    return hall_state[i].pressed;
}

bool drv_hall_is_initialized(void) {
    return hall_initialized;
}
