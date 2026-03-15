#include "hall_kbd.h"

#include "adc.h"
#include "main.h"
#include "stm32h7xx_hal.h"
#include "usart.h"

#include <stdio.h>
#include <string.h>

#define HALL_KBD_DEBUG               1

#define HALL_SCAN_RATE_HZ             10000U
#define HALL_TIMESTAMP_RATE_HZ        1000000U
#define HALL_SETTLE_US                7U
#define HALL_PRESS_THRESHOLD          32000U
#define HALL_RELEASE_THRESHOLD        28000U
#define HALL_VALUE_SHIFT              8U
#define HALL_EVENT_RING_SIZE          64U
#define HALL_ADC_POLL_MAX_ITER        128U

typedef enum
{
  HALL_EVENT_PRESS = 0,
  HALL_EVENT_RELEASE
} hall_event_type_t;

typedef struct
{
  uint8_t key;
  uint8_t type;
  uint32_t timestamp_us;
  uint32_t velocity_dt_us;
} hall_event_t;

typedef struct
{
  uint16_t raw[HALL_KBD_KEY_COUNT];
  uint8_t pressed[HALL_KBD_KEY_COUNT];
  uint32_t frame_timestamp_us;
} hall_snapshot_t;

typedef struct
{
  uint8_t is_down;
  uint8_t velocity_armed;
  uint32_t velocity_start_us;
} hall_key_scan_state_t;

static hall_snapshot_t s_snapshots[2];
static volatile uint8_t s_publish_idx;
static volatile uint32_t s_publish_seq;

static hall_key_scan_state_t s_key_scan[HALL_KBD_KEY_COUNT];

static hall_event_t s_event_ring[HALL_EVENT_RING_SIZE];
static volatile uint16_t s_event_wr;
static volatile uint16_t s_event_rd;

static uint8_t s_key_pressed[HALL_KBD_KEY_COUNT];
static uint8_t s_key_velocity[HALL_KBD_KEY_COUNT];
static uint8_t s_key_value[HALL_KBD_KEY_COUNT];

static volatile uint8_t s_scan_in_progress;
/* Counts TIM6 firings that arrived while a previous scan was still active. */
static volatile uint32_t s_scan_overrun_count;
/* Counts dropped key events when the SPSC ring is full. */
static volatile uint32_t s_event_overflow_count;
/* Tracks worst-case ISR execution time in core cycles (bring-up timing margin). */
static volatile uint32_t s_isr_max_cycles;

static uint8_t hall_velocity_from_dt(uint32_t dt_us);

#if HALL_KBD_DEBUG
/*
 * Lightweight UART debug instrumentation for runtime verification.
 *
 * Design constraints:
 *  - Never log from TIM6 ISR (all logging lives in hall_kbd_poll()).
 *  - Keep scan ISR deterministic (ISR only updates existing counters/state).
 *  - Use non-blocking UART writes to avoid stalling the main loop.
 */
static uint8_t hall_kbd_debug_uart_try_write(const char *msg)
{
  size_t len = strlen(msg);
  for (size_t i = 0U; i < len; i++)
  {
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TXE_TXFNF) == RESET)
    {
      return 0U;
    }

    huart1.Instance->TDR = (uint8_t)msg[i];
  }

  return 1U;
}

static void hall_kbd_debug_log_stats(void)
{
  char line[128];
  static uint32_t s_last_log_ts_us = 0U;
  static uint32_t s_last_seq = 0U;
  uint32_t now_us = TIM5->CNT;

  if ((uint32_t)(now_us - s_last_log_ts_us) < 1000000U)
  {
    return;
  }

  uint32_t seq = s_publish_seq;
  uint32_t scans_per_sec = seq - s_last_seq;
  int n = snprintf(line,
                   sizeof(line),
                   "[HALL] scan_hz=%lu isr_max_cycles=%lu overrun=%lu evt_ovf=%lu\r\n",
                   (unsigned long)scans_per_sec,
                   (unsigned long)s_isr_max_cycles,
                   (unsigned long)s_scan_overrun_count,
                   (unsigned long)s_event_overflow_count);

  if ((n > 0) && ((size_t)n < sizeof(line)) && (hall_kbd_debug_uart_try_write(line) != 0U))
  {
    s_last_log_ts_us = now_us;
    s_last_seq = seq;
  }
}

static void hall_kbd_debug_log_press_event(const hall_event_t *ev)
{
  if (ev->type != (uint8_t)HALL_EVENT_PRESS)
  {
    return;
  }

  char line[48];
  uint8_t velocity = hall_velocity_from_dt(ev->velocity_dt_us);
  int n = snprintf(line,
                   sizeof(line),
                   "[HALL] key=%u vel=%u\r\n",
                   (unsigned int)ev->key,
                   (unsigned int)velocity);

  if ((n > 0) && ((size_t)n < sizeof(line)))
  {
    (void)hall_kbd_debug_uart_try_write(line);
  }
}
#endif

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
  /*
   * Fixed settle delay: the MUX + analog front-end must settle for a known,
   * deterministic window before conversion starts. This bounded busy-wait keeps
   * TIM6 ISR runtime predictable.
   */
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
    return 0U;
  }

  if (HAL_ADC_Start(&hadc2) != HAL_OK)
  {
    (void)HAL_ADC_Stop(&hadc1);
    return 0U;
  }

  uint8_t adc1_ready = 0U;
  uint8_t adc2_ready = 0U;

  /*
   * Bounded polling by design for ISR safety: this loop executes at most
   * HALL_ADC_POLL_MAX_ITER iterations, preventing lockup if an ADC conversion
   * never reaches EOC.
   */
  for (uint32_t i = 0U; i < HALL_ADC_POLL_MAX_ITER; i++)
  {
    if (!adc1_ready && (HAL_ADC_PollForConversion(&hadc1, 0U) == HAL_OK))
    {
      adc1_ready = 1U;
    }

    if (!adc2_ready && (HAL_ADC_PollForConversion(&hadc2, 0U) == HAL_OK))
    {
      adc2_ready = 1U;
    }

    if (adc1_ready && adc2_ready)
    {
      break;
    }
  }

  if (!(adc1_ready && adc2_ready))
  {
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

static void hall_event_push(uint8_t key, hall_event_type_t type, uint32_t ts_us, uint32_t velocity_dt_us)
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
  s_event_ring[wr].timestamp_us = ts_us;
  s_event_ring[wr].velocity_dt_us = velocity_dt_us;

  /*
   * SPSC ring ordering rule:
   *  - producer: TIM6 ISR writes payload
   *  - consumer: main loop reads payload
   * DMB ensures event fields are visible before write index publish.
   */
  __DMB();
  s_event_wr = next;
}

static uint8_t hall_velocity_from_dt(uint32_t dt_us)
{
  if (dt_us == 0U)
  {
    return 127U;
  }

  if (dt_us >= 20000U)
  {
    return 1U;
  }

  uint32_t v = 127U - ((dt_us * 126U) / 20000U);
  if (v < 1U)
  {
    v = 1U;
  }
  return (uint8_t)v;
}

static void hall_process_key_sample(uint8_t key, uint16_t raw, uint32_t now_us)
{
  hall_key_scan_state_t *st = &s_key_scan[key];

  if (st->is_down == 0U)
  {
    if ((raw >= HALL_RELEASE_THRESHOLD) && (raw < HALL_PRESS_THRESHOLD) && (st->velocity_armed == 0U))
    {
      st->velocity_armed = 1U;
      st->velocity_start_us = now_us;
    }

    if (raw < HALL_RELEASE_THRESHOLD)
    {
      st->velocity_armed = 0U;
    }

    if (raw >= HALL_PRESS_THRESHOLD)
    {
      uint32_t dt_us = 0U;
      if (st->velocity_armed != 0U)
      {
        dt_us = now_us - st->velocity_start_us;
      }

      st->is_down = 1U;
      st->velocity_armed = 0U;
      hall_event_push(key, HALL_EVENT_PRESS, now_us, dt_us);
    }
  }
  else if (raw <= HALL_RELEASE_THRESHOLD)
  {
    st->is_down = 0U;
    st->velocity_armed = 0U;
    hall_event_push(key, HALL_EVENT_RELEASE, now_us, 0U);
  }
}

static void hall_scan_isr(void)
{
  if (s_scan_in_progress != 0U)
  {
    s_scan_overrun_count++;
    return;
  }

  s_scan_in_progress = 1U;

  uint32_t cyccnt_start = DWT->CYCCNT;
  uint8_t write_idx = (uint8_t)(s_publish_idx ^ 1U);
  hall_snapshot_t *snap = &s_snapshots[write_idx];
  uint32_t frame_ts_us = TIM5->CNT;

#ifdef HALL_KBD_DEBUG_SCOPE_PIN
  HAL_GPIO_WritePin(HALL_KBD_DEBUG_SCOPE_GPIO_Port, HALL_KBD_DEBUG_SCOPE_Pin, GPIO_PIN_SET);
#endif

  /*
   * Strictly bounded scan loop: exactly 8 mux steps per ISR tick. Combined with
   * fixed settle wait and bounded ADC polling, ISR time remains deterministic.
   */
  for (uint8_t mux = 0U; mux < 8U; mux++)
  {
    uint16_t adc1 = 0U;
    uint16_t adc2 = 0U;

    hall_mux_select(mux);
    hall_wait_settle_us(HALL_SETTLE_US);

    if (hall_adc_sample_pair(&adc1, &adc2) == 0U)
    {
      continue;
    }

    uint8_t key_a = (uint8_t)(mux * 2U);
    uint8_t key_b = (uint8_t)(key_a + 1U);
    uint32_t now_us = TIM5->CNT;

    snap->raw[key_a] = adc1;
    snap->raw[key_b] = adc2;

    hall_process_key_sample(key_a, adc1, now_us);
    hall_process_key_sample(key_b, adc2, now_us);

    snap->pressed[key_a] = s_key_scan[key_a].is_down;
    snap->pressed[key_b] = s_key_scan[key_b].is_down;
  }

  snap->frame_timestamp_us = frame_ts_us;

  __DMB();
  s_publish_idx = write_idx;
  s_publish_seq++;

#ifdef HALL_KBD_DEBUG_SCOPE_PIN
  HAL_GPIO_WritePin(HALL_KBD_DEBUG_SCOPE_GPIO_Port, HALL_KBD_DEBUG_SCOPE_Pin, GPIO_PIN_RESET);
#endif

  uint32_t cyccnt_delta = DWT->CYCCNT - cyccnt_start;
  if (cyccnt_delta > s_isr_max_cycles)
  {
    s_isr_max_cycles = cyccnt_delta;
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
  HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);

  TIM6->CR1 = TIM_CR1_CEN;
}

void hall_kbd_init(void)
{
  for (uint8_t i = 0U; i < HALL_KBD_KEY_COUNT; i++)
  {
    s_snapshots[0].raw[i] = 0U;
    s_snapshots[1].raw[i] = 0U;
    s_snapshots[0].pressed[i] = 0U;
    s_snapshots[1].pressed[i] = 0U;
    s_key_scan[i].is_down = 0U;
    s_key_scan[i].velocity_armed = 0U;
    s_key_scan[i].velocity_start_us = 0U;
    s_key_pressed[i] = 0U;
    s_key_velocity[i] = 0U;
    s_key_value[i] = 0U;
  }

  s_publish_idx = 0U;
  s_publish_seq = 0U;
  s_event_wr = 0U;
  s_event_rd = 0U;
  s_scan_in_progress = 0U;
  s_scan_overrun_count = 0U;
  s_event_overflow_count = 0U;
  s_isr_max_cycles = 0U;

  if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U)
  {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  }
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  hall_mux_select(0U);
  hall_timestamp_timer_init();
  hall_scan_timer_init();
}

void hall_kbd_poll(void)
{
#if HALL_KBD_DEBUG
  hall_kbd_debug_log_stats();
#endif

  static uint32_t last_seq = 0U;
  uint32_t seq = s_publish_seq;

  if (seq != last_seq)
  {
    uint8_t idx = s_publish_idx;
    hall_snapshot_t *snap = &s_snapshots[idx];

    for (uint8_t k = 0U; k < HALL_KBD_KEY_COUNT; k++)
    {
      s_key_value[k] = (uint8_t)(snap->raw[k] >> HALL_VALUE_SHIFT);
    }

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
      s_key_velocity[ev.key] = hall_velocity_from_dt(ev.velocity_dt_us);
      s_key_pressed[ev.key] = 1U;
#if HALL_KBD_DEBUG
      hall_kbd_debug_log_press_event(&ev);
#endif
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

void TIM6_DAC_IRQHandler(void)
{
  if ((TIM6->SR & TIM_SR_UIF) != 0U)
  {
    TIM6->SR &= ~TIM_SR_UIF;
    hall_scan_isr();
  }
}
