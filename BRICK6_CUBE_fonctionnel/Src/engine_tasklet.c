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

volatile uint32_t engine_tick_count = 0U;

/* Shared between IRQ + main */
static volatile uint32_t engine_frames_accum = 0U;

/* Tick fixed = 32 frames */
static uint32_t engine_frames_per_tick = 32U;

/* ============================================================
   Internal tick
   ============================================================ */

static void engine_tick(void)
{
  engine_tick_count++;
}

/* ============================================================
   Init
   ============================================================ */

void engine_tasklet_init(uint32_t sample_rate)
{
  (void)sample_rate;

  engine_tick_count = 0U;
  engine_frames_accum = 0U;

  /* Tick aligned with AUDIO_FRAMES_PER_HALF = 32
     => 48kHz / 32 = 1500 Hz stable */
  engine_frames_per_tick = 32U;
}

/* ============================================================
   Called from audio IRQ (cheap)
   ============================================================ */

void engine_tasklet_notify_frames(uint32_t frames)
{
  engine_frames_accum += frames;
}

/* ============================================================
   Called from main loop (non-IRQ)
   ============================================================ */

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

    engine_tick();
  }
}
