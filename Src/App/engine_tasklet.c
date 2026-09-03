/**
 * @file engine_tasklet.c
 * @brief Cadence moteur CONTROL autonome derivee de TIM5.
 *
 * TIM5 et SAI derivent du meme HSE. CONTROL convertit donc directement les
 * ticks TIM5 en frames nominales, sans reveil ni compteur publie par AUDIO.
 *
 * Rôle dans le système:
 * - Fournit une base de temps deterministe dans le domaine CONTROL.
 * - Cadence l'engine (séquenceur, UI, automation) sans perturber l'IRQ audio.
 *
 * Contraintes temps réel:
 * - Critique audio: non.
 * - IRQ: non.
 * - Tasklet: oui (engine_tasklet_poll appelé dans la main loop).
 * - Borné: oui (consommation tick par tick, section critique minimale).
 *
 * Architecture:
 * - Appele par: main loop (engine_tasklet_poll).
 * - Appelle: aucun module externe.
 *
 * Règles:
 * - Pas de malloc.
 * - Aucun traitement lourd en IRQ.
 * - Aucun etat AUDIO n'est lu ou publie pour cette cadence.
 *
 * @note L’API publique est déclarée dans engine_tasklet.h.
 */

#include "App/engine_tasklet.h"
#include "stm32h7xx_hal.h"
#include "IPC/live_clock_control.h"

#include "buttons.h"
#include "encoders.h"
#include "led_rgb.h"


volatile uint32_t engine_tick_count = 0U;

/* Tick fixed = 32 frames */
static uint32_t engine_frames_per_tick = 32U;
static uint32_t engine_last_poll_ms = 0U;
static uint32_t engine_sample_rate_hz = 48000U;
static uint32_t engine_tim5_hz;
static uint32_t engine_tim5_last_tick;
static uint64_t engine_tim5_frame_remainder;
static uint64_t engine_control_frames_pending;

/* ============================================================
   Internal tick
   ============================================================ */

/**
 * @brief Incrémente le compteur logique de ticks engine.
 *
 * Contexte d'appel:
 * - Main loop via engine_tasklet_poll.
 */
/**
 * @brief Point d'entrée engine_tick.
 *
 * Rôle:
 * - Exécuter le traitement associé à engine_tick.
 *
 * @param dt_ms Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void engine_tick(uint32_t dt_ms)
{
  engine_tick_count++;

  buttons_update(dt_ms);
  encoders_update(dt_ms);
  led_service(dt_ms);
}

/* ============================================================
   Init
   ============================================================ */

/**
 * @brief Initialise l'ordonnanceur de tasklet calé sur l'audio.
 *
 * @param sample_rate Fréquence audio (conservée pour API, non utilisée ici).
 */
/**
 * @brief Point d'entrée engine_tasklet_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à engine_tasklet_init.
 *
 * @param sample_rate Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void engine_tasklet_init(uint32_t sample_rate)
{
  engine_tick_count = 0U;
  engine_sample_rate_hz = (sample_rate != 0U) ? sample_rate : 48000U;
  engine_tim5_hz = live_clock_get_tim5_hz();
  engine_tim5_last_tick = live_clock_capture_tick();
  engine_tim5_frame_remainder = 0U;
  engine_control_frames_pending = 0U;

  /* Tick aligned with AUDIO_FRAMES_PER_HALF = 32
     => 48kHz / 32 = 1500 Hz stable */
  engine_frames_per_tick = 32U;
  engine_last_poll_ms = HAL_GetTick();

  buttons_init();
  encoders_init();
}

/* ============================================================
   Called from main loop (non-IRQ)
   ============================================================ */

/**
 * @brief Consomme les frames accumulées et déclenche les ticks engine.
 *
 * Contexte d'appel:
 * - Main loop (non IRQ).
 */
/**
 * @brief Point d'entrée engine_tasklet_poll.
 *
 * Rôle:
 * - Exécuter le traitement associé à engine_tasklet_poll.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void engine_tasklet_poll(void)
{
  uint32_t ticks_processed = 0U;

  if (engine_tim5_hz != 0U)
  {
    const uint32_t now_tick = live_clock_capture_tick();
    const uint32_t elapsed_ticks = now_tick - engine_tim5_last_tick;
    engine_tim5_last_tick = now_tick;
    const uint64_t scaled = engine_tim5_frame_remainder
      + (uint64_t)elapsed_ticks * engine_sample_rate_hz;
    engine_control_frames_pending += scaled / engine_tim5_hz;
    engine_tim5_frame_remainder = scaled % engine_tim5_hz;
  }

  while (ticks_processed < ENGINE_TASKLET_MAX_TICKS_PER_POLL)
  {
    if (engine_control_frames_pending < engine_frames_per_tick)
    {
      break;
    }
    engine_control_frames_pending -= engine_frames_per_tick;

    uint32_t now_ms = HAL_GetTick();
    uint32_t dt_ms = now_ms - engine_last_poll_ms;
    engine_last_poll_ms = now_ms;
    if (dt_ms == 0U)
    {
      dt_ms = 1U;
    }

    engine_tick(dt_ms);
    ticks_processed++;
  }

  /* Defensive fallback if TIM5 was not configured by the board image. */
  if ((ticks_processed == 0U) && (engine_tim5_hz == 0U))
  {
    const uint32_t now_ms = HAL_GetTick();
    const uint32_t dt_ms = now_ms - engine_last_poll_ms;
    if (dt_ms != 0U)
    {
      engine_last_poll_ms = now_ms;
      engine_tick(dt_ms);
    }
  }
}
