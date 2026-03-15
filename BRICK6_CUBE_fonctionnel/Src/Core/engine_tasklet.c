/**
 * @file engine_tasklet.c
 * @brief Cadence moteur basée sur les frames audio
 *        (tick aligné sur le block audio : 32 frames = 1500 Hz @48 kHz).
 *
 * Ce module accumule les frames audio notifiées depuis l'IRQ DMA,
 * puis déclenche un tick logique stable hors IRQ dans la boucle principale.
 *
 * Rôle dans le système:
 * - Fournit une base de temps déterministe dérivée de l'audio.
 * - Cadence l'engine (séquenceur, UI, automation) sans perturber l'IRQ audio.
 *
 * Contraintes temps réel:
 * - Critique audio: oui indirectement (appel notify_frames en IRQ, ultra cheap).
 * - IRQ: oui (engine_tasklet_notify_frames appelé dans callbacks audio).
 * - Tasklet: oui (engine_tasklet_poll appelé dans la main loop).
 * - Borné: oui (consommation tick par tick, section critique minimale).
 *
 * Architecture:
 * - Appelé par: callbacks audio DMA (notify_frames),
 *              main loop (engine_tasklet_poll).
 * - Appelle: aucun module externe.
 *
 * Règles:
 * - Pas de malloc.
 * - Aucun traitement lourd en IRQ.
 * - Accès partagé IRQ/main protégé par section critique courte.
 *
 * @note L’API publique est déclarée dans engine_tasklet.h.
 */

#include "engine_tasklet.h"
#include "stm32h7xx_hal.h"

#include "buttons.h"
#include "encoders.h"
#include "led_rgb.h"
#include "App/mux_pots.h"
#include "App/hall_kbd.h"

volatile uint32_t engine_tick_count = 0U;

/* Shared between IRQ + main */
static volatile uint32_t engine_frames_accum = 0U;

/* Tick fixed = 32 frames */
static uint32_t engine_frames_per_tick = 32U;
static uint32_t engine_last_poll_ms = 0U;

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
  mux_pots_scan();
  hall_kbd_poll();
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
  (void)sample_rate;

  engine_tick_count = 0U;
  engine_frames_accum = 0U;

  /* Tick aligned with AUDIO_FRAMES_PER_HALF = 32
     => 48kHz / 32 = 1500 Hz stable */
  engine_frames_per_tick = 32U;
  engine_last_poll_ms = HAL_GetTick();

  buttons_init();
  encoders_init();
  mux_pots_init();
  hall_kbd_init();
}

/* ============================================================
   Called from audio IRQ (cheap)
   ============================================================ */

/**
 * @brief Notifie le tasklet du nombre de frames audio traitées.
 *
 * @param frames Frames traitées par l'IRQ audio.
 *
 * Contexte d'appel:
 * - IRQ audio uniquement, chemin ultra court.
 */
/**
 * @brief Point d'entrée engine_tasklet_notify_frames.
 *
 * Rôle:
 * - Exécuter le traitement associé à engine_tasklet_notify_frames.
 *
 * @param frames Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void engine_tasklet_notify_frames(uint32_t frames)
{
  engine_frames_accum += frames;
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
  while (1)
  {
    uint32_t accum;

    /* ---- Critical section: take one tick worth safely ---- */
    __disable_irq();
    accum = engine_frames_accum;

    if (accum < engine_frames_per_tick)
    {
      __enable_irq();
      break;
    }

    engine_frames_accum = accum - engine_frames_per_tick;
    __enable_irq();
    /* ------------------------------------------------------ */

    uint32_t now_ms = HAL_GetTick();
    uint32_t dt_ms = now_ms - engine_last_poll_ms;
    engine_last_poll_ms = now_ms;
    if (dt_ms == 0U)
    {
      dt_ms = 1U;
    }

    engine_tick(dt_ms);
  }
}
