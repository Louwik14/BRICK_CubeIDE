/**
 * @file audio_core.c
 * @brief Orchestrateur audio minimal (sélection de source + mix simple).
 *
 * Ce module sélectionne la source audio selon le routing compile-time,
 * applique un mix 2→1 si demandé, et assure un fallback SAI pass-through.
 *
 * Rôle dans le système:
 * - Point central de traitement par blocs (256 frames, 8 canaux).
 *
 * Contraintes temps réel:
 * - Critique audio: oui.
 * - IRQ: non (traitement hors IRQ).
 * - Tasklet: oui (appelé depuis audio_out).
 * - Borné: oui (taille de bloc fixe).
 *
 * Architecture:
 * - Appelé par: audio_out (tasklet).
 * - Appelle: audio_io_usb, audio_io_sd, mixer.
 * - Consommé par: sortie SAI (via audio_out).
 *
 * Règles:
 * - Pas de malloc.
 * - Pas de blocage en IRQ.
 *
 * @note L’API publique est déclarée dans audio_core.h.
 */

#include "audio_core.h"

#include "audio_io_sd.h"
#include "audio_io_usb.h"
#include "mixer.h"
#include "routing.h"

#include <string.h>

#define USB_CHANNELS 2

static int32_t core_input_copy[AUDIO_CORE_FRAMES_PER_BLOCK * AUDIO_CORE_CHANNELS];
static int32_t core_usb_block[AUDIO_CORE_FRAMES_PER_BLOCK * AUDIO_CORE_CHANNELS];
static int32_t core_sd_block[AUDIO_CORE_FRAMES_PER_BLOCK * AUDIO_CORE_CHANNELS];
static volatile uint32_t audio_core_process_count = 0U;
static volatile uint32_t audio_core_usb_block_used = 0U;
static volatile uint32_t audio_core_usb_block_missed = 0U;
static volatile uint32_t audio_core_fallback_count = 0U;
static volatile uint32_t audio_core_frames_requested_total = 0U;
static volatile uint32_t audio_core_frames_provided_total = 0U;
static volatile uint32_t audio_core_last_usb_available = 0U;
static volatile uint32_t audio_core_last_usb_samples = 0U;
static volatile uint32_t audio_core_last_frames = 0U;
static volatile uint32_t audio_core_last_usb_available_frames = 0U;
static volatile uint32_t audio_core_last_usb_need_frames = 0U;
static volatile uint8_t audio_core_last_source = 0U;

void audio_core_init(void) {
}

void audio_core_process_block(int32_t *out, uint32_t frames) {
  if (out == NULL) {
    return;
  }

  if (frames > AUDIO_CORE_FRAMES_PER_BLOCK) {
    frames = AUDIO_CORE_FRAMES_PER_BLOCK;
  }

  size_t out_sample_count = (size_t)frames * AUDIO_CORE_CHANNELS;
  size_t usb_sample_count = (size_t)frames * USB_CHANNELS;

  uint32_t usb_samples = 0U;
  uint32_t sd_samples = 0U;

  route_source_t source = ROUTE_SRC_USB;
  audio_core_process_count++;
  audio_core_frames_requested_total += frames;
  audio_core_last_frames = frames;
  audio_core_last_source = (uint8_t)source;
  audio_core_last_usb_available = 0U;
  audio_core_last_usb_samples = 0U;
  audio_core_last_usb_available_frames = 0U;
  audio_core_last_usb_need_frames = frames;

  if ((source == ROUTE_SRC_USB) || (source == ROUTE_SRC_MIX))
  {
    audio_buffer_t *usb_buf = audio_io_usb_get_rx_buffer();
    if (usb_buf != NULL)
    {
    	uint32_t available = audio_io_usb_get_rx_available();
    	audio_core_last_usb_available = available;
    	audio_core_last_usb_available_frames = available / USB_CHANNELS;

    	if (available >= usb_sample_count)
    	{
    	  usb_samples = audio_io_usb_read_rx_samples(core_usb_block, (uint32_t)usb_sample_count);
    	  audio_core_last_usb_samples = usb_samples;
    	}

    }
  }

  if ((source == ROUTE_SRC_SD) || (source == ROUTE_SRC_MIX))
  {
    if (audio_io_sd_has_block())
    {
    	sd_samples = audio_io_sd_read_block(core_sd_block, (uint32_t)out_sample_count);
    }
  }

  switch (source)
  {
  case ROUTE_SRC_USB:
    if (usb_samples == usb_sample_count)
    {
      // Clear output
      memset(out, 0, out_sample_count * sizeof(int32_t));

      // Map USB 2ch -> slots 0/1
      for (uint32_t f = 0; f < frames; ++f)
      {
        out[f * AUDIO_CORE_CHANNELS + 0] = core_usb_block[f * USB_CHANNELS + 0];
        out[f * AUDIO_CORE_CHANNELS + 1] = core_usb_block[f * USB_CHANNELS + 1];
      }

      audio_core_usb_block_used++;
      audio_core_frames_provided_total += frames;
      return;
    }

    audio_core_usb_block_missed++;
    break;


    case ROUTE_SRC_SD:
    	if (sd_samples == out_sample_count)
    	{
    	  memcpy(out, core_sd_block, out_sample_count * sizeof(int32_t));
    	  audio_core_frames_provided_total += frames;
    	  return;
    	}

      break;

    case ROUTE_SRC_MIX:
    	if ((usb_samples == usb_sample_count) && (sd_samples == out_sample_count))
      {
    		mixer_mix_2_to_1(core_usb_block, core_sd_block, out, (uint32_t)out_sample_count);
        audio_core_usb_block_used++;
        audio_core_frames_provided_total += frames;
        return;
      }
    	if (usb_samples == usb_sample_count)
      {
    		memcpy(out, core_usb_block, out_sample_count * sizeof(int32_t));
        audio_core_usb_block_used++;
        audio_core_frames_provided_total += frames;
        return;
      }
    	if (sd_samples == out_sample_count)
      {
    		memcpy(out, core_sd_block, out_sample_count * sizeof(int32_t));
        audio_core_frames_provided_total += frames;
        return;
      }
      audio_core_usb_block_missed++;
      break;

    case ROUTE_SRC_SAI:
    default:
      break;
  }

  audio_core_fallback_count++;
  memcpy(out, core_input_copy, out_sample_count * sizeof(int32_t));
  audio_core_frames_provided_total += frames;
}

void audio_core_on_input_block(const int32_t *data, uint32_t frames) {
  if (data == NULL) {
    return;
  }

  if (frames > AUDIO_CORE_FRAMES_PER_BLOCK) {
    frames = AUDIO_CORE_FRAMES_PER_BLOCK;
  }

  size_t sample_count = (size_t)frames * AUDIO_CORE_CHANNELS;
  memcpy(core_input_copy, data, sample_count * sizeof(int32_t));
}

uint32_t audio_core_get_process_count(void)
{
  return audio_core_process_count;
}

uint32_t audio_core_get_usb_block_used_count(void)
{
  return audio_core_usb_block_used;
}

uint32_t audio_core_get_usb_block_missed_count(void)
{
  return audio_core_usb_block_missed;
}

uint32_t audio_core_get_fallback_count(void)
{
  return audio_core_fallback_count;
}

uint32_t audio_core_get_frames_requested_total(void)
{
  return audio_core_frames_requested_total;
}

uint32_t audio_core_get_frames_provided_total(void)
{
  return audio_core_frames_provided_total;
}

uint32_t audio_core_get_last_usb_available(void)
{
  return audio_core_last_usb_available;
}

uint32_t audio_core_get_last_usb_samples(void)
{
  return audio_core_last_usb_samples;
}

uint32_t audio_core_get_last_frames(void)
{
  return audio_core_last_frames;
}

uint32_t audio_core_get_last_usb_available_frames(void)
{
  return audio_core_last_usb_available_frames;
}

uint32_t audio_core_get_last_usb_need_frames(void)
{
  return audio_core_last_usb_need_frames;
}

uint8_t audio_core_get_last_source(void)
{
  return audio_core_last_source;
}
