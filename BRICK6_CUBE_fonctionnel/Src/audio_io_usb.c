/**
 * @file audio_io_usb.c
 * @brief Wrapper USB audio RX/TX basé sur audio_buffer.
 *
 * Encapsule le ring USB RX/TX avec une API simple utilisée par le moteur
 * audio et par TinyUSB, sans allocation ni dépendance HAL directe.
 *
 * Rôle dans le système:
 * - Source USB RX et tampon TX pour la classe audio.
 *
 * Contraintes temps réel:
 * - Critique audio: non (buffering).
 * - IRQ: non.
 * - Tasklet: oui (appelé hors IRQ).
 * - Borné: oui (taille fixe).
 *
 * Architecture:
 * - Appelé par: tinyusb_app, audio_core.
 * - Appelle: audio_buffer.
 * - Consommé par: moteur audio / USB audio.
 *
 * Règles:
 * - Pas de malloc.
 * - Pas de blocage en IRQ.
 *
 * @note L’API publique est déclarée dans audio_io_usb.h.
 */

#include "audio_io_usb.h"

#include "audio_core.h"
#include "tusb.h"

#include <limits.h>

#define USB_RX_CAPACITY (AUDIO_CORE_FRAMES_PER_BLOCK * AUDIO_CORE_CHANNELS * 4U)
#define USB_TX_CAPACITY (AUDIO_CORE_FRAMES_PER_BLOCK * AUDIO_CORE_CHANNELS * 4U)

static audio_buffer_t usb_rx_buf;
static int32_t usb_rx_mem[USB_RX_CAPACITY];
static audio_buffer_t usb_tx_buf;
static int32_t usb_tx_mem[USB_TX_CAPACITY];
static volatile uint32_t usb_rx_written_total = 0U;
static volatile uint32_t usb_rx_read_total = 0U;
static volatile uint32_t usb_rx_dropped_total = 0U;
static volatile uint32_t usb_rx_avail_min = 0U;
static volatile uint32_t usb_rx_avail_max = 0U;
static volatile uint32_t usb_rx_free_min = 0U;
static volatile uint32_t usb_rx_free_max = 0U;

static void audio_io_usb_update_minmax(void)
{
  uint32_t available = 0U;
  uint32_t free_count = 0U;

  audio_buffer_get_levels(&usb_rx_buf, &available, &free_count);

  if (available < usb_rx_avail_min) {
    usb_rx_avail_min = available;
  }
  if (available > usb_rx_avail_max) {
    usb_rx_avail_max = available;
  }
  if (free_count < usb_rx_free_min) {
    usb_rx_free_min = free_count;
  }
  if (free_count > usb_rx_free_max) {
    usb_rx_free_max = free_count;
  }
}

void audio_io_usb_init(void)
{
  audio_buffer_init(&usb_rx_buf, usb_rx_mem, USB_RX_CAPACITY);
  audio_buffer_init(&usb_tx_buf, usb_tx_mem, USB_TX_CAPACITY);
  usb_rx_written_total = 0U;
  usb_rx_read_total = 0U;
  usb_rx_dropped_total = 0U;
  usb_rx_avail_min = UINT32_MAX;
  usb_rx_avail_max = 0U;
  usb_rx_free_min = UINT32_MAX;
  usb_rx_free_max = 0U;
  audio_io_usb_update_minmax();
}

audio_buffer_t *audio_io_usb_get_rx_buffer(void)
{
  return &usb_rx_buf;
}

void audio_io_usb_on_rx_samples(const int32_t *samples, uint32_t count)
{
  uint32_t written = audio_buffer_write(&usb_rx_buf, samples, count);
  usb_rx_written_total += written;
  usb_rx_dropped_total += (count - written);
  audio_io_usb_update_minmax();
}

uint32_t audio_io_usb_read_rx_samples(int32_t *dst, uint32_t count)
{
  uint32_t read = audio_buffer_read(&usb_rx_buf, dst, count);
  usb_rx_read_total += read;
  audio_io_usb_update_minmax();
  return read;
}

uint32_t audio_io_usb_prepare_tx(int32_t *dst, uint32_t max_samples)
{
  return audio_buffer_read(&usb_tx_buf, dst, max_samples);
}

uint32_t audio_io_usb_get_rx_available(void)
{
  uint32_t available = audio_buffer_available(&usb_rx_buf);
  if (available < usb_rx_avail_min) {
    usb_rx_avail_min = available;
  }
  if (available > usb_rx_avail_max) {
    usb_rx_avail_max = available;
  }
  return available;
}

uint32_t audio_io_usb_get_rx_free(void)
{
  uint32_t free_count = audio_buffer_free(&usb_rx_buf);
  if (free_count < usb_rx_free_min) {
    usb_rx_free_min = free_count;
  }
  if (free_count > usb_rx_free_max) {
    usb_rx_free_max = free_count;
  }
  return free_count;
}

uint32_t audio_io_usb_get_rx_capacity(void)
{
  return USB_RX_CAPACITY;
}

uint32_t audio_io_usb_get_rx_written_total(void)
{
  return usb_rx_written_total;
}

uint32_t audio_io_usb_get_rx_read_total(void)
{
  return usb_rx_read_total;
}

uint32_t audio_io_usb_get_rx_dropped_total(void)
{
  return usb_rx_dropped_total;
}

uint32_t audio_io_usb_get_rx_avail_min(void)
{
  return usb_rx_avail_min;
}

uint32_t audio_io_usb_get_rx_avail_max(void)
{
  return usb_rx_avail_max;
}

uint32_t audio_io_usb_get_rx_free_min(void)
{
  return usb_rx_free_min;
}

uint32_t audio_io_usb_get_rx_free_max(void)
{
  return usb_rx_free_max;
}
