/**
 * @file sd_audio_block_ring.c
 * @brief Ring buffer de blocs audio entre SD et sortie audio.
 *
 * Ce module fournit une file circulaire statique pour transporter des blocs
 * audio du streaming SD vers le rendu audio, sans allocation dynamique.
 *
 * Rôle dans le système:
 * - Tampon d'élasticité entre SD (producteur) et audio_out (consommateur).
 * - Permet d'absorber la latence SD sans impacter l'IRQ audio.
 *
 * Contraintes temps réel:
 * - Critique audio: oui (consommé par audio_out).
 * - IRQ: non (utilisé en tasklet).
 * - Tasklet: oui (sd_tasklet_poll / audio_tasklet_poll).
 * - Borné: oui (taille fixe AUDIO_BLOCK_COUNT).
 *
 * Architecture:
 * - Appelé par: sd_stream (produce), audio_out (consume).
 * - Appelle: aucun module externe.
 *
 * Règles:
 * - Pas de malloc.
 * - Pas de blocage.
 *
 * @note L’API publique est déclarée dans sd_audio_block_ring.h.
 */

#include "sd_audio_block_ring.h"
#include <stddef.h>

static uint32_t audio_block_ring_advance(uint32_t index)
{
  return (index + 1U) % AUDIO_BLOCK_COUNT;
}

audio_block_ring_t sd_audio_block_ring;

void audio_block_ring_init(audio_block_ring_t *ring)
{
  if (ring == NULL)
  {
    return;
  }

  ring->write_index = 0U;
  ring->read_index = 0U;
  ring->fill_level = 0U;
}

uint8_t *audio_block_ring_get_write_ptr(audio_block_ring_t *ring)
{
  if ((ring == NULL) || (ring->fill_level >= AUDIO_BLOCK_COUNT))
  {
    return NULL;
  }

  return ring->blocks[ring->write_index];
}

void audio_block_ring_produce(audio_block_ring_t *ring)
{
  if ((ring == NULL) || (ring->fill_level >= AUDIO_BLOCK_COUNT))
  {
    return;
  }

  ring->write_index = audio_block_ring_advance(ring->write_index);
  ring->fill_level++;
}

uint8_t *audio_block_ring_get_read_ptr(audio_block_ring_t *ring)
{
  if ((ring == NULL) || (ring->fill_level == 0U))
  {
    return NULL;
  }

  return ring->blocks[ring->read_index];
}

void audio_block_ring_consume(audio_block_ring_t *ring)
{
  if ((ring == NULL) || (ring->fill_level == 0U))
  {
    return;
  }

  ring->read_index = audio_block_ring_advance(ring->read_index);
  ring->fill_level--;
}

uint32_t audio_block_ring_fill_level(const audio_block_ring_t *ring)
{
  if (ring == NULL)
  {
    return 0U;
  }

  return ring->fill_level;
}
