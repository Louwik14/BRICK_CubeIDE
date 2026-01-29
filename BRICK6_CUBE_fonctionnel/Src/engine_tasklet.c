/**
 * @file engine_tasklet.c
 * @brief Cadence moteur basée sur les frames audio (tick 1 kHz).
 *
 * Ce module accumule les frames audio et déclenche un tick logique à cadence
 * régulière pour l'engine (séquenceur/synth à venir), hors IRQ.
 *
 * Rôle dans le système:
 * - Découple la cadence de l'engine du DMA audio.
 * - Fournit un tick stable pour la logique temps réel non audio.
 *
 * Contraintes temps réel:
 * - Critique audio: indirect (cadencé par l'audio mais hors IRQ).
 * - Tasklet: oui (appelé dans la boucle principale).
 * - IRQ: non.
 * - Borné: oui (boucle bornée par l'accumulateur).
 *
 * Architecture:
 * - Appelé par: audio_out (notify frames), main loop (engine_tasklet_poll).
 * - Appelle: aucun module externe.
 *
 * Règles:
 * - Pas de malloc.
 * - Ne pas bloquer la boucle principale.
 *
 * @note L’API publique est déclarée dans engine_tasklet.h.
 */

#include "engine_tasklet.h"

#if BRICK6_REFACTOR_STEP_3

volatile uint32_t engine_tick_count = 0U;
static uint32_t engine_frames_accum = 0U;
static uint32_t engine_frames_per_tick = 0U;

static void engine_tick(void)
{
  engine_tick_count++;
}

void engine_tasklet_init(uint32_t sample_rate)
{
  engine_tick_count = 0U;
  engine_frames_accum = 0U;
  engine_frames_per_tick = sample_rate / 1000U;

  if (engine_frames_per_tick == 0U)
  {
    engine_frames_per_tick = 1U;
  }
}

void engine_tasklet_notify_frames(uint32_t frames)
{
  engine_frames_accum += frames;
}

void engine_tasklet_poll(void)
{
  while (engine_frames_accum >= engine_frames_per_tick)
  {
    engine_frames_accum -= engine_frames_per_tick;
    engine_tick();
  }
}

#endif
