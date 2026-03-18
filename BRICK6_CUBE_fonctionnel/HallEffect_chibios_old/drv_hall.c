#include "drv_hall.h"

#include "ch.h"
#include "hal.h"

#include <limits.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * drv_hall.c — STM32H743 + ChibiOS
 *
 * Acquisition:
 *   GPT (TIM4_TRGO) -> ADC (ext trigger) -> DMA circulaire -> adc_cb() IRQ
 *   adc_cb(): lit A/B, map index capteur, avance MUX, appelle hall_process_channel()
 *
 * IMPORTANT (MUX + ADC S/H):
 *   - Le premier échantillon après chaque changement de MUX est contaminé par le canal précédent.
 *   - On le jette systématiquement (hall_discard_next).
 *
 * Logique bouton + vélocité (multi-modes + courbes):
 *   - Min/Max auto-observés par capteur
 *   - Seuils Schmitt dynamiques en % de [min..max]
 *   - NOTE ON quand raw dépasse trig_hi
 *   - NOTE OFF quand raw redescend sous trig_lo
 *   - Vélocité: plusieurs algos (DV-peak, TIME, ENERGY) + courbes (linear/soft/hard/log/exp)
 *
 * Thread-safety:
 *   - note_on/off + velocity écrits en IRQ
 *   - note_on/off consommés via getters read+clear atomiques
 *   - hall_update() no-op
 */

/* -------------------- Paramètres -------------------- */

#define ADC_NUM_CHANNELS     2
#define ADC_DMA_DEPTH        1
#define HALL_SENSOR_COUNT    16U

/* Fréquence globale de déclenchement ADC */
#define HALL_ADC_TRIGGER_HZ  5000U

/* -------------------- Seuils dynamiques (RAW 16-bit) --------------------
 * threshold/hysteresis exprimés en millièmes de la plage [min..max].
 */
#define HALL_THRESHOLD_PPM    200U   /* 0..1000 */
#define HALL_HYST_PPM         40U    /* 0..1000 */
#define HALL_MIN_RANGE        500U   /* plage minimale avant détection */

/* -------------------- Vélocité: modes + courbes -------------------- */



/* Sélection globale (peut devenir configurable utilisateur plus tard)
 * -> Pour tester, change ces 2 lignes.
 */
static volatile hall_vel_mode_t  g_vel_mode  = HALL_VEL_MODE_TIME;
static volatile hall_vel_curve_t g_vel_curve = HALL_VEL_CURVE_SOFT;

/* --- Mode DV_PEAK (Grid-like) ---
 * dv_slow = range >> SLOW_SHIFT
 * dv_fast = range >> FAST_SHIFT
 */
#define HALL_VEL_SLOW_SHIFT   12U
#define HALL_VEL_FAST_SHIFT   2U

/* --- Mode TIME (piano-like) ---
 * Mesure du temps (en nombre de samples valides) entre:
 *   vel_start = min + range * START_PPM / 1000
 *   vel_end   = trig_hi (ou min + range * END_PPM / 1000)
 *
 * Mapping dt_count -> velocity:
 *   dt <= FAST_DT => 127
 *   dt >= SLOW_DT => 1
 */
#define HALL_VEL_TIME_START_PPM   150U  /* départ mesure: 15% de course */
#define HALL_VEL_TIME_END_PPM     0U    /* 0 => utilise trig_hi comme fin */
#define HALL_VEL_TIME_FAST_DT     2U    /* en nb d'échantillons valides */
#define HALL_VEL_TIME_SLOW_DT     14U   /* en nb d'échantillons valides */

/* --- Mode ENERGY ---
 * sum_dv = Σ max(0, raw[n]-raw[n-1]) pendant l'attaque.
 * Mapping similaire à DV_PEAK mais plus "stable".
 */
#define HALL_VEL_ENERGY_SLOW_SHIFT  6U
#define HALL_VEL_ENERGY_FAST_SHIFT  2U

/* -------------------- MUX / ADC -------------------- */

/* MUX pins */
#define MUX_S0_PORT GPIOA
#define MUX_S0_PIN  5
#define MUX_S1_PORT GPIOA
#define MUX_S1_PIN  4
#define MUX_S2_PORT GPIOA
#define MUX_S2_PIN  6

/* ADC channels */
#define ADC_CH_MUXA   ADC_CHANNEL_IN4
#define ADC_CH_MUXB   ADC_CHANNEL_IN7
#define ADC_PCSEL     (ADC_SELMASK_IN4 | ADC_SELMASK_IN7)

/* -------------------- DMA buffer -------------------- */

__attribute__((section(".ramd2")))
static adcsample_t adc_buffer[ADC_DMA_DEPTH * ADC_NUM_CHANNELS];

/* -------------------- État capteurs -------------------- */

static volatile uint16_t hall_values[HALL_SENSOR_COUNT];
static volatile bool hall_note_on[HALL_SENSOR_COUNT];
static volatile bool hall_note_off[HALL_SENSOR_COUNT];

static volatile uint8_t hall_velocity[HALL_SENSOR_COUNT];
static volatile uint8_t hall_pressure[HALL_SENSOR_COUNT];
static volatile uint8_t hall_midi_value[HALL_SENSOR_COUNT];

static bool hall_initialized;
static uint8_t hall_mux_index;

/* Discard du premier sample après chaque commutation MUX */
static uint8_t hall_discard_next = 1U;

/* -------------------- Button state (par capteur) -------------------- */

typedef struct {
  uint16_t min;
  uint16_t max;
  uint16_t trig_lo;
  uint16_t trig_hi;

  uint8_t  prev_out; /* 0=OFF, 1=ON */
  uint8_t  curr_out; /* 0=OFF, 1=ON */

  /* Attaque / vélocité */
  uint16_t prev_raw;
  uint16_t dv_peak;
  uint16_t sum_dv;

  uint16_t vel_start_th;     /* seuil départ time */
  uint16_t vel_end_th;       /* seuil fin time */
  uint16_t time_count;       /* compteur de samples depuis start */
  uint8_t  time_active;      /* mesure time en cours */
  uint8_t  vel_latched;      /* 1..127 */
} hall_button_t;

static hall_button_t hall_btn[HALL_SENSOR_COUNT];

/* -------------------- Forward decl -------------------- */

static void mux_select(uint8_t ch);
static void hall_process_channel(uint8_t index, uint16_t raw);

static void hall_update_triggers(hall_button_t *b);
static bool hall_range_valid(const hall_button_t *b);

static uint8_t hall_velocity_compute(hall_button_t *b, uint16_t range);
static uint8_t hall_velocity_from_dv(uint16_t range, uint16_t dv_peak);
static uint8_t hall_velocity_from_time(uint16_t dt_count);
static uint8_t hall_velocity_from_energy(uint16_t range, uint16_t sum_dv);

static uint8_t hall_apply_curve(uint8_t v, hall_vel_curve_t curve);
static uint16_t isqrt_u32(uint32_t x);

/* -------------------- GPT (TIM4_TRGO) -------------------- */

static const GPTConfig hall_gptcfg = {
  .frequency = STM32_TIMCLK1,
  .callback  = NULL,
  .cr2       = TIM_CR2_MMS_1, /* MMS=010: TRGO on Update */
  .dier      = 0U
};

static const gptcnt_t hall_gpt_interval = (STM32_TIMCLK1 / HALL_ADC_TRIGGER_HZ);

/* -------------------- ADC callback (IRQ context) -------------------- */

static void adc_cb(ADCDriver *adcp) {
  (void)adcp;

  uint16_t vA = (uint16_t)adc_buffer[0U];
  uint16_t vB = (uint16_t)adc_buffer[1U];

  /* Après un mux_select(), le premier sample est contaminé -> on le jette. */
  if (hall_discard_next) {
    hall_discard_next = 0U;
    return;
  }

  uint8_t mux = hall_mux_index;

  hall_values[mux + 0U] = vA;
  hall_values[mux + 8U] = vB;

  hall_process_channel((uint8_t)(mux + 0U), vA);
  hall_process_channel((uint8_t)(mux + 8U), vB);

  /* Next mux channel (0..7) */
  hall_mux_index = (uint8_t)((hall_mux_index + 1U) & 7U);
  mux_select(hall_mux_index);

  hall_discard_next = 1U; /* le prochain sample sera jeté */
}

/* -------------------- ADC config -------------------- */

static const ADCConversionGroup adcgrpcfg = {
  .circular     = true,
  .num_channels = ADC_NUM_CHANNELS,
  .end_cb       = adc_cb,
  .error_cb     = NULL,

  /* External trigger on rising edge: TIM4_TRGO */
  .cfgr  = ADC_CFGR_EXTEN_RISING | ADC_CFGR_EXTSEL_SRC(12),
  .cfgr2 = 0,

  .ltr1 = 0, .htr1 = 0,
  .ltr2 = 0, .htr2 = 0,
  .ltr3 = 0, .htr3 = 0,

  .awd2cr = 0,
  .awd3cr = 0,

  .pcsel = ADC_PCSEL,

  .smpr = {
    ADC_SMPR1_SMP_AN4(ADC_SMPR_SMP_64P5) |
    ADC_SMPR1_SMP_AN7(ADC_SMPR_SMP_64P5),
    0
  },

  .sqr = {
    ADC_SQR1_SQ1_N(ADC_CH_MUXA) |
    ADC_SQR1_SQ2_N(ADC_CH_MUXB),
    0, 0, 0
  }
};

/* -------------------- MUX -------------------- */

static void mux_select(uint8_t ch) {
  palWritePad(MUX_S0_PORT, MUX_S0_PIN, (ch >> 0) & 1U);
  palWritePad(MUX_S1_PORT, MUX_S1_PIN, (ch >> 1) & 1U);
  palWritePad(MUX_S2_PORT, MUX_S2_PIN, (ch >> 2) & 1U);
}

/* -------------------- Helpers -------------------- */

static bool hall_range_valid(const hall_button_t *b) {
  if (b->max <= b->min) {
    return false;
  }
  uint16_t range = (uint16_t)(b->max - b->min);
  return range >= (uint16_t)HALL_MIN_RANGE;
}

static void hall_update_triggers(hall_button_t *b) {
  if (!hall_range_valid(b)) {
    b->trig_lo = b->min;
    b->trig_hi = b->max;
    return;
  }

  uint32_t range = (uint32_t)(b->max - b->min);

  uint32_t half_hyst = (uint32_t)HALL_HYST_PPM / 2U;
  uint32_t lo_ppm = (uint32_t)HALL_THRESHOLD_PPM;
  uint32_t hi_ppm = (uint32_t)HALL_THRESHOLD_PPM;

  if (lo_ppm > half_hyst) {
    lo_ppm -= half_hyst;
  } else {
    lo_ppm = 0U;
  }

  hi_ppm += half_hyst;
  if (hi_ppm > 1000U) {
    hi_ppm = 1000U;
  }

  b->trig_lo = (uint16_t)(b->min + (range * lo_ppm) / 1000U);
  b->trig_hi = (uint16_t)(b->min + (range * hi_ppm) / 1000U);

  /* Seuils time-mode */
  b->vel_start_th = (uint16_t)(b->min + (range * (uint32_t)HALL_VEL_TIME_START_PPM) / 1000U);

  if (HALL_VEL_TIME_END_PPM == 0U) {
    b->vel_end_th = b->trig_hi;
  } else {
    b->vel_end_th = (uint16_t)(b->min + (range * (uint32_t)HALL_VEL_TIME_END_PPM) / 1000U);
  }
}

/* -------------------- Courbes -------------------- */

static uint16_t isqrt_u32(uint32_t x) {
  /* integer sqrt (floor), simple et suffisant pour 0..(127*127) */
  uint32_t op = x;
  uint32_t res = 0;
  uint32_t one = 1UL << 30;
  while (one > op) one >>= 2;
  while (one != 0) {
    if (op >= res + one) {
      op -= res + one;
      res = res + 2 * one;
    }
    res >>= 1;
    one >>= 2;
  }
  return (uint16_t)res;
}

static uint8_t hall_apply_curve(uint8_t v, hall_vel_curve_t curve) {
  if (v < 1U) v = 1U;
  if (v > 127U) v = 127U;

  switch (curve) {
    case HALL_VEL_CURVE_LINEAR:
      return v;

    case HALL_VEL_CURVE_SOFT: {
      /* v^2 / 127 */
      uint32_t vv = (uint32_t)v * (uint32_t)v;
      uint32_t out = vv / 127U;
      if (out < 1U) out = 1U;
      if (out > 127U) out = 127U;
      return (uint8_t)out;
    }

    case HALL_VEL_CURVE_HARD: {
      /* sqrt(v/127)*127 => sqrt(v*127) */
      uint32_t x = (uint32_t)v * 127U;
      uint16_t out = isqrt_u32(x);
      if (out < 1U) out = 1U;
      if (out > 127U) out = 127U;
      return (uint8_t)out;
    }

    case HALL_VEL_CURVE_LOG: {
      /* inverse de SOFT: 127 - (127-v)^2/127 */
      uint32_t d = (uint32_t)(127U - v);
      uint32_t dd = d * d;
      uint32_t out = 127U - (dd / 127U);
      if (out < 1U) out = 1U;
      if (out > 127U) out = 127U;
      return (uint8_t)out;
    }

    case HALL_VEL_CURVE_EXP: {
      /* v^3 / (127^2) */
      uint32_t vv = (uint32_t)v * (uint32_t)v * (uint32_t)v;
      uint32_t out = vv / (127U * 127U);
      if (out < 1U) out = 1U;
      if (out > 127U) out = 127U;
      return (uint8_t)out;
    }

    default:
      return v;
  }
}

/* -------------------- Vélocité: mapping par mode -------------------- */

static uint8_t hall_velocity_from_dv(uint16_t range, uint16_t dv_peak) {
  if (range == 0U) return 1U;

  uint16_t dv_slow = (uint16_t)(range >> HALL_VEL_SLOW_SHIFT);
  uint16_t dv_fast = (uint16_t)(range >> HALL_VEL_FAST_SHIFT);

  if (dv_slow < 1U) dv_slow = 1U;
  if (dv_fast <= (uint16_t)(dv_slow + 1U)) dv_fast = (uint16_t)(dv_slow + 2U);

  if (dv_peak <= dv_slow) return 1U;
  if (dv_peak >= dv_fast) return 127U;

  uint32_t num = (uint32_t)(dv_peak - dv_slow) * 126U;
  uint32_t den = (uint32_t)(dv_fast - dv_slow);
  uint32_t v = 1U + (num / den);

  if (v < 1U) v = 1U;
  if (v > 127U) v = 127U;
  return (uint8_t)v;
}

static uint8_t hall_velocity_from_time(uint16_t dt_count) {
  /* dt_count = nb de samples valides entre start et end */
  if (dt_count <= HALL_VEL_TIME_FAST_DT) return 127U;
  if (dt_count >= HALL_VEL_TIME_SLOW_DT) return 1U;

  uint32_t num = (uint32_t)(HALL_VEL_TIME_SLOW_DT - dt_count) * 126U;
  uint32_t den = (uint32_t)(HALL_VEL_TIME_SLOW_DT - HALL_VEL_TIME_FAST_DT);
  uint32_t v = 1U + (num / den);

  if (v < 1U) v = 1U;
  if (v > 127U) v = 127U;
  return (uint8_t)v;
}

static uint8_t hall_velocity_from_energy(uint16_t range, uint16_t sum_dv) {
  if (range == 0U) return 1U;

  uint16_t e_slow = (uint16_t)(range >> HALL_VEL_ENERGY_SLOW_SHIFT);
  uint16_t e_fast = (uint16_t)(range >> HALL_VEL_ENERGY_FAST_SHIFT);

  if (e_slow < 1U) e_slow = 1U;
  if (e_fast <= (uint16_t)(e_slow + 1U)) e_fast = (uint16_t)(e_slow + 2U);

  if (sum_dv <= e_slow) return 1U;
  if (sum_dv >= e_fast) return 127U;

  uint32_t num = (uint32_t)(sum_dv - e_slow) * 126U;
  uint32_t den = (uint32_t)(e_fast - e_slow);
  uint32_t v = 1U + (num / den);

  if (v < 1U) v = 1U;
  if (v > 127U) v = 127U;
  return (uint8_t)v;
}

static uint8_t hall_velocity_compute(hall_button_t *b, uint16_t range) {
  uint8_t vel_raw = 1U;

  hall_vel_mode_t mode = (hall_vel_mode_t)g_vel_mode;
  switch (mode) {
    case HALL_VEL_MODE_TIME:
      vel_raw = hall_velocity_from_time(b->time_count);
      break;

    case HALL_VEL_MODE_ENERGY:
      vel_raw = hall_velocity_from_energy(range, b->sum_dv);
      break;

    case HALL_VEL_MODE_DV_PEAK:
    default:
      vel_raw = hall_velocity_from_dv(range, b->dv_peak);
      break;
  }

  hall_vel_curve_t curve = (hall_vel_curve_t)g_vel_curve;
  return hall_apply_curve(vel_raw, curve);
}

/* -------------------- Logique ON/OFF + velocity (IRQ context) -------------------- */

static void hall_process_channel(uint8_t index, uint16_t raw) {

  hall_button_t *b = &hall_btn[index];

  /* auto-cal min/max */
  if (raw < b->min) b->min = raw;
  if (raw > b->max) b->max = raw;

  /* triggers dynamiques + thresholds time-mode */
  hall_update_triggers(b);

  /* historique sortie */
  b->prev_out = b->curr_out;

  /* Tant que plage invalide, force OFF + reset attaque */
  if (!hall_range_valid(b)) {
    b->curr_out = 0U;
    b->prev_raw = raw;
    b->dv_peak = 0U;
    b->sum_dv = 0U;
    b->time_count = 0U;
    b->time_active = 0U;
    b->vel_latched = 0U;
    return;
  }

  /* ---------- Attaque (features) ---------- */

  /* dv positif */
  uint16_t dv = 0U;
  if (raw > b->prev_raw) dv = (uint16_t)(raw - b->prev_raw);
  b->prev_raw = raw;

  /* reset au repos bas */
  if ((b->curr_out == 0U) && (raw <= b->trig_lo)) {
    b->dv_peak = 0U;
    b->sum_dv = 0U;
    b->time_count = 0U;
    b->time_active = 0U;
  }

  /* accumulate pendant OFF */
  if (b->curr_out == 0U) {
    if (dv > b->dv_peak) b->dv_peak = dv;

    /* sum_dv saturée */
    uint32_t s = (uint32_t)b->sum_dv + (uint32_t)dv;
    if (s > 65535U) s = 65535U;
    b->sum_dv = (uint16_t)s;

    /* time-mode: démarrage quand on dépasse start */
    if (!b->time_active && raw >= b->vel_start_th) {
      b->time_active = 1U;
      b->time_count = 0U;
    }
    if (b->time_active) {
      if (b->time_count < 65535U) b->time_count++;
      /* fin quand on dépasse end */
      if (raw >= b->vel_end_th) {
        /* on garde time_count tel quel, time_active peut rester 1 */
        b->time_active = 0U;
      }
    }
  }

  /* ---------- Schmitt ---------- */

  if ((b->curr_out == 0U) && (raw >= b->trig_hi)) {
    b->curr_out = 1U;

    /* Latch velocity au NOTE ON */
    uint16_t range = (uint16_t)(b->max - b->min);
    b->vel_latched = hall_velocity_compute(b, range);
    hall_velocity[index] = b->vel_latched;

  } else if ((b->curr_out != 0U) && (raw <= b->trig_lo)) {
    b->curr_out = 0U;
  }

  /* edge detect -> flags */
  if ((b->prev_out == 0U) && (b->curr_out == 1U)) {
    hall_note_on[index] = true;
  } else if ((b->prev_out == 1U) && (b->curr_out == 0U)) {
    hall_note_off[index] = true;
  }
}

/* -------------------- API -------------------- */

void hall_init(void) {
  if (hall_initialized) return;

  /* GPIO MUX */
  palSetPadMode(GPIOA, 4, PAL_MODE_OUTPUT_PUSHPULL);
  palSetPadMode(GPIOA, 5, PAL_MODE_OUTPUT_PUSHPULL);
  palSetPadMode(GPIOA, 6, PAL_MODE_OUTPUT_PUSHPULL);

  /* ADC pins */
  palSetPadMode(GPIOC, 4, PAL_MODE_INPUT_ANALOG); /* IN4 */
  palSetPadMode(GPIOA, 7, PAL_MODE_INPUT_ANALOG); /* IN7 */

  for (uint8_t i = 0U; i < HALL_SENSOR_COUNT; i++) {
    hall_values[i] = 0U;
    hall_note_on[i] = false;
    hall_note_off[i] = false;

    hall_velocity[i] = 0U;
    hall_pressure[i] = 0U;
    hall_midi_value[i] = 0U;

    hall_btn[i].min = UINT16_MAX;
    hall_btn[i].max = 0U;
    hall_btn[i].trig_lo = UINT16_MAX;
    hall_btn[i].trig_hi = 0U;

    hall_btn[i].prev_out = 0U;
    hall_btn[i].curr_out = 0U;

    hall_btn[i].prev_raw = 0U;
    hall_btn[i].dv_peak = 0U;
    hall_btn[i].sum_dv = 0U;

    hall_btn[i].vel_start_th = 0U;
    hall_btn[i].vel_end_th = 0U;
    hall_btn[i].time_count = 0U;
    hall_btn[i].time_active = 0U;

    hall_btn[i].vel_latched = 0U;
  }

  hall_mux_index = 0U;
  mux_select(0U);
  hall_discard_next = 1U;

  gptStart(&GPTD4, &hall_gptcfg);

  adcStart(&ADCD1, NULL);
  adcStartConversion(&ADCD1, &adcgrpcfg, adc_buffer, ADC_DMA_DEPTH);

  gptStartContinuous(&GPTD4, hall_gpt_interval);

  hall_initialized = true;
}

void hall_update(void) {
  /* no-op */
}

uint16_t hall_get(uint8_t index) {
  if (index >= HALL_SENSOR_COUNT) return 0U;
  return hall_values[index];
}

bool hall_get_note_on(uint8_t index) {
  if (index >= HALL_SENSOR_COUNT) return false;
  bool v;
  chSysLock();
  v = hall_note_on[index];
  hall_note_on[index] = false;
  chSysUnlock();
  return v;
}

bool hall_get_note_off(uint8_t index) {
  if (index >= HALL_SENSOR_COUNT) return false;
  bool v;
  chSysLock();
  v = hall_note_off[index];
  hall_note_off[index] = false;
  chSysUnlock();
  return v;
}

uint8_t hall_get_velocity(uint8_t index) {
  if (index >= HALL_SENSOR_COUNT) return 0U;
  return hall_velocity[index];
}

uint8_t hall_get_pressure(uint8_t index) {
  if (index >= HALL_SENSOR_COUNT) return 0U;
  return hall_pressure[index];
}

uint8_t hall_get_midi_value(uint8_t index) {
  if (index >= HALL_SENSOR_COUNT) return 0U;
  return hall_midi_value[index];
}

/* Optionnel: setters si tu veux changer à runtime (ajoute les prototypes dans drv_hall.h si besoin) */
void hall_set_velocity_mode(uint8_t mode) {
  if (mode < (uint8_t)HALL_VEL_MODE_COUNT) {
    g_vel_mode = (hall_vel_mode_t)mode;
  }
}


void hall_set_velocity_curve(uint8_t curve) {
  if (curve < (uint8_t)HALL_VEL_CURVE_COUNT) {
    g_vel_curve = (hall_vel_curve_t)curve;
  }
}


uint8_t hall_get_velocity_mode(void) {
  return (uint8_t)g_vel_mode;
}

uint8_t hall_get_velocity_curve(void) {
  return (uint8_t)g_vel_curve;
}

/* -------------------- DEBUG API -------------------- */

uint16_t hall_dbg_get_min(uint8_t i) {
  if (i >= HALL_SENSOR_COUNT) return 0U;
  return hall_btn[i].min;
}

uint16_t hall_dbg_get_max(uint8_t i) {
  if (i >= HALL_SENSOR_COUNT) return 0U;
  return hall_btn[i].max;
}

uint16_t hall_dbg_get_trig_lo(uint8_t i) {
  if (i >= HALL_SENSOR_COUNT) return 0U;
  return hall_btn[i].trig_lo;
}

uint16_t hall_dbg_get_trig_hi(uint8_t i) {
  if (i >= HALL_SENSOR_COUNT) return 0U;
  return hall_btn[i].trig_hi;
}

uint8_t hall_dbg_get_state(uint8_t i) {
  if (i >= HALL_SENSOR_COUNT) return 0U;
  return hall_btn[i].curr_out;
}

uint8_t hall_dbg_get_velocity(uint8_t i) {
  if (i >= HALL_SENSOR_COUNT) return 0U;
  return hall_btn[i].vel_latched;
}
