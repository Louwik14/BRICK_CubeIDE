#include "App/hall_kbd.h"

#include "adc.h"
#include "main.h"
#include "stm32h7xx_hal.h"

#include <limits.h>
#include <string.h>

#define HALL_SCAN_RATE_HZ             1000U
#define HALL_TIMESTAMP_RATE_HZ        1000000U
#define HALL_SETTLE_US                7U
#define HALL_ADC_POLL_MAX_ITER        128U
#define HALL_FILTER_FACTOR            8U
#define HALL_PRESS_NUM                4U
#define HALL_PRESS_DEN                10U
#define HALL_RELEASE_NUM              6U
#define HALL_RELEASE_DEN              10U
#define HALL_DETENT_DEN               20U
#define HALL_VALUE_MAX                127U
#define HALL_VELOCITY_MIN_US          500U
#define HALL_VELOCITY_MAX_US          30000U
#define HALL_EVENT_RING_SIZE          64U
#define HALL_MUX_CHANNEL_COUNT        8U
#define HALL_CPU_CLOCK_HZ             480000000U
#define HALL_BOOT_CALIBRATION_MS      300U
#define HALL_BOOT_CALIBRATION_SCANS   ((HALL_SCAN_RATE_HZ * HALL_BOOT_CALIBRATION_MS) / 1000U)

typedef enum
{
  HALL_EVENT_PRESS = 0,
  HALL_EVENT_RELEASE
} hall_event_type_t;

typedef struct
{
  uint8_t key;
  uint8_t type;
  uint8_t velocity;
} hall_event_t;

typedef struct
{
  uint16_t raw[HALL_KBD_KEY_COUNT];
  uint8_t pressed[HALL_KBD_KEY_COUNT];
} hall_snapshot_t;

typedef struct
{
  uint32_t sum;
  uint16_t buffer[HALL_FILTER_FACTOR];
  uint8_t head;
  uint8_t count;
  uint8_t factor;
} hall_asc_t;

typedef struct
{
  uint16_t raw;
  uint16_t filtered;
  uint16_t min;
  uint16_t max;
  uint16_t threshold;
  uint16_t hysteresis;
  uint16_t last_filtered;
  uint8_t value;
  uint8_t velocity;
  uint8_t pressed;
  uint8_t filter_init;
  uint8_t velocity_armed;
  uint32_t velocity_start_us;
} hall_key_scan_state_t;

static hall_snapshot_t s_snapshots[2];
static volatile uint8_t s_publish_idx;
static volatile uint32_t s_publish_seq;

static hall_key_scan_state_t s_key_scan[HALL_KBD_KEY_COUNT];
static hall_asc_t s_asc_state[HALL_KBD_KEY_COUNT];

static hall_event_t s_event_ring[HALL_EVENT_RING_SIZE];
static volatile uint16_t s_event_wr;
static volatile uint16_t s_event_rd;

static uint8_t s_key_pressed[HALL_KBD_KEY_COUNT];
static uint8_t s_key_velocity[HALL_KBD_KEY_COUNT];
static uint8_t s_key_value[HALL_KBD_KEY_COUNT];
static uint16_t s_key_raw[HALL_KBD_KEY_COUNT];
static uint16_t s_key_filtered[HALL_KBD_KEY_COUNT];
static uint16_t s_key_min[HALL_KBD_KEY_COUNT];
static uint16_t s_key_max[HALL_KBD_KEY_COUNT];
static uint16_t s_key_threshold[HALL_KBD_KEY_COUNT];
static uint16_t s_key_hysteresis[HALL_KBD_KEY_COUNT];

static volatile uint8_t s_scan_in_progress;
static volatile uint8_t s_hall_init_done;
static volatile uint8_t s_hall_scan_started;
static volatile uint32_t s_scan_overrun_count;
static volatile uint32_t s_event_overflow_count;
static volatile uint32_t s_isr_max_cycles;
static volatile uint32_t s_adc_error_count;
static volatile uint32_t s_isr_last_cycles;
static volatile uint16_t s_last_raw_adc1;
static volatile uint16_t s_last_raw_adc2;
static volatile uint16_t s_boot_calibration_remaining;

static uint32_t hall_get_apb1_timer_clk_hz(void)
{
  uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
  uint32_t ppre1 = ((RCC->D2CFGR >> RCC_D2CFGR_D2PPRE1_Pos) & 0x7U);

  if (ppre1 < 4U)
  {
    return pclk1;
  }

  return pclk1 * 2U;
}

static void hall_mux_select(uint8_t mux)
{
  HAL_GPIO_WritePin(MUX_HALL_S0_GPIO_Port, MUX_HALL_S0_Pin,
                    (mux & 0x01U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(MUX_HALL_S1_GPIO_Port, MUX_HALL_S1_Pin,
                    (mux & 0x02U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(MUX_HALL_S2_GPIO_Port, MUX_HALL_S2_Pin,
                    (mux & 0x04U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void hall_wait_settle_us(uint32_t settle_us)
{
  uint32_t start = TIM5->CNT;
  while ((uint32_t)(TIM5->CNT - start) < settle_us)
  {
    __NOP();
  }
}

static uint8_t hall_adc_sample_pair(uint16_t *adc1_out, uint16_t *adc2_out)
{
  if (HAL_ADC_Start(&hadc1) != HAL_OK)
  {
    s_adc_error_count++;
    return 0U;
  }

  if (HAL_ADC_Start(&hadc2) != HAL_OK)
  {
    s_adc_error_count++;
    (void)HAL_ADC_Stop(&hadc1);
    return 0U;
  }

  uint8_t adc1_ready = 0U;
  uint8_t adc2_ready = 0U;

  for (uint32_t i = 0U; i < HALL_ADC_POLL_MAX_ITER; i++)
  {
    if ((adc1_ready == 0U) && (HAL_ADC_PollForConversion(&hadc1, 0U) == HAL_OK))
    {
      adc1_ready = 1U;
    }

    if ((adc2_ready == 0U) && (HAL_ADC_PollForConversion(&hadc2, 0U) == HAL_OK))
    {
      adc2_ready = 1U;
    }

    if ((adc1_ready != 0U) && (adc2_ready != 0U))
    {
      break;
    }
  }

  if ((adc1_ready == 0U) || (adc2_ready == 0U))
  {
    s_adc_error_count++;
    (void)HAL_ADC_Stop(&hadc1);
    (void)HAL_ADC_Stop(&hadc2);
    return 0U;
  }

  if (((HAL_ADC_GetState(&hadc1) & HAL_ADC_STATE_REG_EOC) == 0U) ||
      ((HAL_ADC_GetState(&hadc2) & HAL_ADC_STATE_REG_EOC) == 0U))
  {
    s_adc_error_count++;
    (void)HAL_ADC_Stop(&hadc1);
    (void)HAL_ADC_Stop(&hadc2);
    return 0U;
  }

  *adc1_out = (uint16_t)HAL_ADC_GetValue(&hadc1);
  *adc2_out = (uint16_t)HAL_ADC_GetValue(&hadc2);

  (void)HAL_ADC_Stop(&hadc1);
  (void)HAL_ADC_Stop(&hadc2);

  return 1U;
}

static uint8_t hall_clamp_u8(int32_t v)
{
  if (v < 0)
  {
    return 0U;
  }
  if (v > (int32_t)HALL_VALUE_MAX)
  {
    return HALL_VALUE_MAX;
  }
  return (uint8_t)v;
}

static uint16_t hall_compute_detent_width(uint16_t range)
{
  uint16_t width = (uint16_t)(range / HALL_DETENT_DEN);
  if (width == 0U)
  {
    width = 1U;
  }
  return width;
}

static uint8_t hall_normalize_with_deadzone(uint16_t value,
                                            uint16_t min,
                                            uint16_t max,
                                            uint16_t detent_low,
                                            uint16_t detent_high)
{
  if (detent_low < min)
  {
    detent_low = min;
  }
  if (detent_high > max)
  {
    detent_high = max;
  }
  if (detent_low > detent_high)
  {
    detent_low = detent_high;
  }

  int32_t range = (int32_t)(max - min);
  if (range <= 0)
  {
    range = 1;
  }

  int32_t base = ((int32_t)max - (int32_t)value) * (int32_t)HALL_VALUE_MAX / range;
  if ((value <= detent_low) || (detent_low == detent_high) || (value >= detent_high))
  {
    return hall_clamp_u8(base);
  }

  int32_t n_low = ((int32_t)max - (int32_t)detent_low) * (int32_t)HALL_VALUE_MAX / range;
  int32_t n_high = ((int32_t)max - (int32_t)detent_high) * (int32_t)HALL_VALUE_MAX / range;
  int32_t span = (int32_t)(detent_high - detent_low);
  int32_t pos = (int32_t)(value - detent_low);
  int32_t interp = n_low + (n_high - n_low) * pos / ((span == 0) ? 1 : span);
  return hall_clamp_u8(interp);
}

static uint8_t hall_velocity_from_dt(uint32_t dt_us)
{
  if (dt_us < HALL_VELOCITY_MIN_US)
  {
    dt_us = HALL_VELOCITY_MIN_US;
  }

  if (dt_us > HALL_VELOCITY_MAX_US)
  {
    dt_us = HALL_VELOCITY_MAX_US;
  }

  uint32_t span = HALL_VELOCITY_MAX_US - HALL_VELOCITY_MIN_US;
  uint32_t scaled = (HALL_VELOCITY_MAX_US - dt_us) * HALL_VALUE_MAX / span;

  return hall_clamp_u8((int32_t)scaled);
}

static void hall_event_push(uint8_t key, hall_event_type_t type, uint8_t velocity)
{
  uint16_t wr = s_event_wr;
  uint16_t next = (uint16_t)((wr + 1U) % HALL_EVENT_RING_SIZE);

  if (next == s_event_rd)
  {
    s_event_overflow_count++;
    return;
  }

  s_event_ring[wr].key = key;
  s_event_ring[wr].type = (uint8_t)type;
  s_event_ring[wr].velocity = velocity;

  __DMB();
  s_event_wr = next;
}

static void hall_asc_reset(hall_asc_t *asc, uint8_t factor)
{
  if (factor == 0U)
  {
    factor = 1U;
  }
  if (factor > HALL_FILTER_FACTOR)
  {
    factor = HALL_FILTER_FACTOR;
  }

  asc->sum = 0U;
  asc->head = 0U;
  asc->count = 0U;
  asc->factor = factor;
  memset(asc->buffer, 0, sizeof(asc->buffer));
}

static uint16_t hall_asc_process(hall_asc_t *asc, uint16_t raw)
{
  if ((asc->factor == 0U) || (asc->factor > HALL_FILTER_FACTOR))
  {
    hall_asc_reset(asc, HALL_FILTER_FACTOR);
  }

  asc->sum -= asc->buffer[asc->head];
  asc->buffer[asc->head] = raw;
  asc->sum += raw;

  if (asc->count < asc->factor)
  {
    asc->count++;
  }

  asc->head = (uint8_t)((asc->head + 1U) % asc->factor);
  return (uint16_t)(asc->sum / asc->count);
}

static void hall_process_key_sample(uint8_t key, uint16_t raw, uint32_t now_us)
{
  hall_key_scan_state_t *st = &s_key_scan[key];
  hall_asc_t *asc = &s_asc_state[key];

  st->raw = raw;

  if (st->filter_init == 0U)
  {
    hall_asc_reset(asc, HALL_FILTER_FACTOR);
    st->filter_init = 1U;
  }

  st->filtered = hall_asc_process(asc, raw);

  uint16_t sample = st->filtered;

  if (sample < st->min)
  {
    st->min = sample;
  }
  if (sample > st->max)
  {
    st->max = sample;
  }
  if (st->max < st->min)
  {
    st->max = st->min;
  }

  uint16_t range = (uint16_t)(st->max - st->min);
  if (range == 0U)
  {
    range = 1U;
  }

  uint16_t press_th = (uint16_t)(st->min + ((uint32_t)range * HALL_PRESS_NUM / HALL_PRESS_DEN));
  uint16_t release_th = (uint16_t)(st->min + ((uint32_t)range * HALL_RELEASE_NUM / HALL_RELEASE_DEN));

  if (release_th <= press_th)
  {
    release_th = (uint16_t)(press_th + 1U);
  }
  if (release_th > st->max)
  {
    release_th = st->max;
  }

  uint16_t detent_width = hall_compute_detent_width(range);
  uint16_t center = (uint16_t)(st->min + range / 2U);
  uint16_t detent_low = (center > (detent_width / 2U))
                            ? (uint16_t)(center - detent_width / 2U)
                            : st->min;
  uint16_t detent_high = (uint16_t)(center + detent_width / 2U);
  if (detent_high > st->max)
  {
    detent_high = st->max;
  }

  st->threshold = press_th;
  st->hysteresis = release_th;
  st->value = hall_normalize_with_deadzone(sample, st->min, st->max, detent_low, detent_high);

  if (s_boot_calibration_remaining != 0U)
  {
    st->pressed = 0U;
    st->velocity = 0U;
    st->velocity_armed = 0U;
    st->last_filtered = sample;
    return;
  }

  if (st->velocity_armed == 0U)
  {
    if ((st->last_filtered > release_th) && (sample <= release_th))
    {
      st->velocity_start_us = now_us;
      st->velocity_armed = 1U;
    }
  }
  else
  {
    if (sample > release_th)
    {
      st->velocity_armed = 0U;
    }
    else if (sample <= press_th)
    {
      st->velocity = hall_velocity_from_dt(now_us - st->velocity_start_us);
      st->velocity_armed = 0U;
    }
  }

  uint8_t prev_pressed = st->pressed;
  uint8_t new_pressed = prev_pressed;

  if (sample <= press_th)
  {
    new_pressed = 1U;
  }
  else if (sample >= release_th)
  {
    new_pressed = 0U;
  }

  st->pressed = new_pressed;
  st->last_filtered = sample;

  if ((prev_pressed == 0U) && (new_pressed != 0U))
  {
    hall_event_push(key, HALL_EVENT_PRESS, st->velocity);
  }
  else if ((prev_pressed != 0U) && (new_pressed == 0U))
  {
    hall_event_push(key, HALL_EVENT_RELEASE, 0U);
  }
}

static void hall_scan_isr(void)
{
  if ((s_hall_init_done == 0U) || (s_hall_scan_started == 0U))
  {
    return;
  }



  if (s_scan_in_progress != 0U)
  {
    s_scan_overrun_count++;
    return;
  }

  s_scan_in_progress = 1U;

  uint32_t cyccnt_start = DWT->CYCCNT;
  uint8_t write_idx = (uint8_t)(s_publish_idx ^ 1U);
  hall_snapshot_t *snap = &s_snapshots[write_idx];

  for (uint8_t mux = 0U; mux < HALL_MUX_CHANNEL_COUNT; mux++)
  {
    uint16_t adc1 = 0U;
    uint16_t adc2 = 0U;

    hall_mux_select(mux);
    hall_wait_settle_us(HALL_SETTLE_US);

    if (hall_adc_sample_pair(&adc1, &adc2) == 0U)
    {
      continue;
    }

    s_last_raw_adc1 = adc1;
    s_last_raw_adc2 = adc2;

    uint8_t key_a = mux;
    uint8_t key_b = (uint8_t)(mux + HALL_MUX_CHANNEL_COUNT);
    uint32_t now_us = TIM5->CNT;

    hall_process_key_sample(key_a, adc1, now_us);
    hall_process_key_sample(key_b, adc2, now_us);

    snap->raw[key_a] = adc1;
    snap->raw[key_b] = adc2;
    snap->pressed[key_a] = s_key_scan[key_a].pressed;
    snap->pressed[key_b] = s_key_scan[key_b].pressed;
  }

  __DMB();
  s_publish_idx = write_idx;
  s_publish_seq++;

  uint32_t cyccnt_delta = DWT->CYCCNT - cyccnt_start;
  s_isr_last_cycles = cyccnt_delta;
  if (cyccnt_delta > s_isr_max_cycles)
  {
    s_isr_max_cycles = cyccnt_delta;
  }

  if (cyccnt_delta > (HALL_CPU_CLOCK_HZ / HALL_SCAN_RATE_HZ))
  {
    s_scan_overrun_count++;
  }

  if (s_boot_calibration_remaining != 0U)
  {
    s_boot_calibration_remaining--;
  }

  s_scan_in_progress = 0U;
}

static void hall_timestamp_timer_init(void)
{
  __HAL_RCC_TIM5_CLK_ENABLE();

  TIM5->CR1 = 0U;
  uint32_t tim_clk_hz = hall_get_apb1_timer_clk_hz();
  TIM5->PSC = (uint32_t)((tim_clk_hz / HALL_TIMESTAMP_RATE_HZ) - 1U);
  TIM5->ARR = 0xFFFFFFFFU;
  TIM5->EGR = TIM_EGR_UG;
  TIM5->CNT = 0U;
  TIM5->CR1 = TIM_CR1_CEN;
}

static void hall_scan_timer_init(void)
{
  __HAL_RCC_TIM6_CLK_ENABLE();

  TIM6->CR1 = 0U;
  uint32_t tim_clk_hz = hall_get_apb1_timer_clk_hz();
  uint32_t scan_base_hz = 1000000U;

  TIM6->PSC = (uint32_t)((tim_clk_hz / scan_base_hz) - 1U);
  TIM6->ARR = (scan_base_hz / HALL_SCAN_RATE_HZ) - 1U;
  TIM6->EGR = TIM_EGR_UG;
  TIM6->SR = 0U;
  TIM6->DIER = TIM_DIER_UIE;

  HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 6U, 0U);
  HAL_NVIC_DisableIRQ(TIM6_DAC_IRQn);
}

void hall_kbd_init(void)
{
  for (uint8_t i = 0U; i < HALL_KBD_KEY_COUNT; i++)
  {
    s_snapshots[0].raw[i] = 0U;
    s_snapshots[1].raw[i] = 0U;
    s_snapshots[0].pressed[i] = 0U;
    s_snapshots[1].pressed[i] = 0U;

    s_key_scan[i].raw = 0U;
    s_key_scan[i].filtered = 0U;
    s_key_scan[i].min = UINT16_MAX;
    s_key_scan[i].max = 0U;
    s_key_scan[i].threshold = 0U;
    s_key_scan[i].hysteresis = 0U;
    s_key_scan[i].last_filtered = UINT16_MAX;
    s_key_scan[i].value = 0U;
    s_key_scan[i].velocity = 0U;
    s_key_scan[i].pressed = 0U;
    s_key_scan[i].filter_init = 0U;
    s_key_scan[i].velocity_armed = 0U;
    s_key_scan[i].velocity_start_us = 0U;

    hall_asc_reset(&s_asc_state[i], HALL_FILTER_FACTOR);

    s_key_pressed[i] = 0U;
    s_key_velocity[i] = 0U;
    s_key_value[i] = 0U;
    s_key_raw[i] = 0U;
    s_key_filtered[i] = 0U;
    s_key_min[i] = 0U;
    s_key_max[i] = 0U;
    s_key_threshold[i] = 0U;
    s_key_hysteresis[i] = 0U;
  }

  s_publish_idx = 0U;
  s_publish_seq = 0U;
  s_event_wr = 0U;
  s_event_rd = 0U;
  s_scan_in_progress = 0U;
  s_hall_init_done = 0U;
  s_hall_scan_started = 0U;
  s_scan_overrun_count = 0U;
  s_event_overflow_count = 0U;
  s_isr_max_cycles = 0U;
  s_adc_error_count = 0U;
  s_isr_last_cycles = 0U;
  s_last_raw_adc1 = 0U;
  s_last_raw_adc2 = 0U;
  s_boot_calibration_remaining = 0U;

  if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U)
  {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  }
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  hall_mux_select(0U);
  hall_timestamp_timer_init();
  hall_scan_timer_init();
  s_hall_init_done = 1U;
}

void hall_kbd_start(void)
{
  if ((s_hall_init_done == 0U) || (s_hall_scan_started != 0U))
  {
    return;
  }

  s_scan_in_progress = 0U;
  TIM6->SR = 0U;
  HAL_NVIC_ClearPendingIRQ(TIM6_DAC_IRQn);
  HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
  TIM6->CR1 = TIM_CR1_CEN;
  s_boot_calibration_remaining = (uint16_t)HALL_BOOT_CALIBRATION_SCANS;
  s_hall_scan_started = 1U;
}

void hall_kbd_poll(void)
{
  static uint32_t last_seq = 0U;
  uint32_t seq = s_publish_seq;

  if (seq != last_seq)
  {
    __DMB();
    uint8_t idx = s_publish_idx;

    __disable_irq();
    for (uint8_t k = 0U; k < HALL_KBD_KEY_COUNT; k++)
    {
      s_key_raw[k] = s_snapshots[idx].raw[k];
      s_key_filtered[k] = s_key_scan[k].filtered;
      s_key_min[k] = s_key_scan[k].min;
      s_key_max[k] = s_key_scan[k].max;
      s_key_threshold[k] = s_key_scan[k].threshold;
      s_key_hysteresis[k] = s_key_scan[k].hysteresis;
      s_key_value[k] = s_key_scan[k].value;
    }
    __enable_irq();

    last_seq = seq;
  }

  while (s_event_rd != s_event_wr)
  {
    uint16_t rd = s_event_rd;
    hall_event_t ev = s_event_ring[rd];
    s_event_rd = (uint16_t)((rd + 1U) % HALL_EVENT_RING_SIZE);

    if (ev.key >= HALL_KBD_KEY_COUNT)
    {
      continue;
    }

    if (ev.type == (uint8_t)HALL_EVENT_PRESS)
    {
      s_key_velocity[ev.key] = ev.velocity;
      s_key_pressed[ev.key] = 1U;
    }
    else
    {
      s_key_pressed[ev.key] = 0U;
      s_key_velocity[ev.key] = 0U;
    }
  }
}

uint8_t hall_kbd_is_pressed(uint8_t key)
{
  if (key >= HALL_KBD_KEY_COUNT)
  {
    return 0U;
  }

  return s_key_pressed[key];
}

uint8_t hall_kbd_get_velocity(uint8_t key)
{
  if (key >= HALL_KBD_KEY_COUNT)
  {
    return 0U;
  }

  return s_key_velocity[key];
}

uint8_t hall_kbd_get_value(uint8_t key)
{
  if (key >= HALL_KBD_KEY_COUNT)
  {
    return 0U;
  }

  return s_key_value[key];
}

uint16_t hall_kbd_get_raw(uint8_t key)
{
  if (key >= HALL_KBD_KEY_COUNT)
  {
    return 0U;
  }

  return s_key_raw[key];
}

uint16_t hall_kbd_get_filtered(uint8_t key)
{
  if (key >= HALL_KBD_KEY_COUNT)
  {
    return 0U;
  }

  return s_key_filtered[key];
}

uint16_t hall_kbd_get_min(uint8_t key)
{
  if (key >= HALL_KBD_KEY_COUNT)
  {
    return 0U;
  }

  return s_key_min[key];
}

uint16_t hall_kbd_get_max(uint8_t key)
{
  if (key >= HALL_KBD_KEY_COUNT)
  {
    return 0U;
  }

  return s_key_max[key];
}

uint16_t hall_kbd_get_threshold(uint8_t key)
{
  if (key >= HALL_KBD_KEY_COUNT)
  {
    return 0U;
  }

  return s_key_threshold[key];
}

uint16_t hall_kbd_get_hysteresis(uint8_t key)
{
  if (key >= HALL_KBD_KEY_COUNT)
  {
    return 0U;
  }

  return s_key_hysteresis[key];
}

uint32_t hall_kbd_get_scan_overrun_count(void)
{
  return s_scan_overrun_count;
}

uint32_t hall_kbd_get_event_overflow_count(void)
{
  return s_event_overflow_count;
}

uint32_t hall_kbd_get_isr_max_cycles(void)
{
  return s_isr_max_cycles;
}

uint32_t hall_kbd_get_isr_max_time_us(void)
{
  return s_isr_max_cycles / (HALL_CPU_CLOCK_HZ / 1000000U);
}

uint32_t hall_kbd_get_adc_error_count(void)
{
  return s_adc_error_count;
}

uint16_t hall_kbd_get_last_raw_adc1(void)
{
  return s_last_raw_adc1;
}

uint16_t hall_kbd_get_last_raw_adc2(void)
{
  return s_last_raw_adc2;
}

void TIM6_DAC_IRQHandler(void)
{
  if ((s_hall_init_done == 0U) || (s_hall_scan_started == 0U))
  {
    TIM6->SR = 0U;
    return;
  }

  if ((TIM6->SR & TIM_SR_UIF) != 0U)
  {
    TIM6->SR &= ~TIM_SR_UIF;
    hall_scan_isr();
  }
}
