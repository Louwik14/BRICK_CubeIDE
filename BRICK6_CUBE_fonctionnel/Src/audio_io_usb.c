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

#include "tusb.h"

#define USB_RX_CAPACITY ((4U * CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX) / sizeof(int32_t))
#define USB_TX_CAPACITY ((4U * CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX) / sizeof(int32_t))

static audio_buffer_t usb_rx_buf;
static int32_t usb_rx_mem[USB_RX_CAPACITY];
static audio_buffer_t usb_tx_buf;
static int32_t usb_tx_mem[USB_TX_CAPACITY];
static volatile uint32_t usb_rx_written_total = 0U;
static volatile uint32_t usb_rx_dropped_total = 0U;

void audio_io_usb_init(void)
{
  audio_buffer_init(&usb_rx_buf, usb_rx_mem, USB_RX_CAPACITY);
  audio_buffer_init(&usb_tx_buf, usb_tx_mem, USB_TX_CAPACITY);
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
}

uint32_t audio_io_usb_prepare_tx(int32_t *dst, uint32_t max_samples)
{
  return audio_buffer_read(&usb_tx_buf, dst, max_samples);
}

uint32_t audio_io_usb_get_rx_available(void)
{
  return audio_buffer_available(&usb_rx_buf);
}

uint32_t audio_io_usb_get_rx_free(void)
{
  return audio_buffer_free(&usb_rx_buf);
}

uint32_t audio_io_usb_get_rx_capacity(void)
{
  return USB_RX_CAPACITY;
}

uint32_t audio_io_usb_get_rx_written_total(void)
{
  return usb_rx_written_total;
}

uint32_t audio_io_usb_get_rx_dropped_total(void)
{
  return usb_rx_dropped_total;
}
