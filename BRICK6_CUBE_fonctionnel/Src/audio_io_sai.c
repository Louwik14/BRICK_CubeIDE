/**
 * @file audio_io_sai.c
 * @brief Wrapper SAI pour accéder aux blocs RX/TX existants.
 *
 * Fournit une abstraction minimale autour des accès SAI RX/TX sans modifier
 * les buffers DMA ni les callbacks IRQ existants.
 *
 * Rôle dans le système:
 * - Accès uniforme aux blocs SAI pour le moteur audio.
 *
 * Contraintes temps réel:
 * - Critique audio: non (wrapper passif).
 * - IRQ: non.
 * - Tasklet: oui (appelé hors IRQ).
 * - Borné: oui.
 *
 * Architecture:
 * - Appelé par: audio_core.
 * - Appelle: audio_in, audio_out.
 * - Consommé par: moteur audio.
 *
 * Règles:
 * - Pas de malloc.
 * - Pas de blocage en IRQ.
 *
 * @note L’API publique est déclarée dans audio_io_sai.h.
 */

#include "audio_io_sai.h"

#include "audio_in.h"
#include "audio_out.h"

const int32_t *audio_io_sai_get_rx_block(void)
{
  return AudioIn_GetLatestBlock();
}

int32_t *audio_io_sai_get_tx_block(void)
{
  return AudioOut_GetBuffer();
}

uint32_t audio_io_sai_get_frames(void)
{
  return AUDIO_OUT_FRAMES_PER_HALF;
}
