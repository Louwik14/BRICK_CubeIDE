/**
 * @file midi.c
 * @brief Implémentation du module MIDI (USB Device + backends futurs) pour STM32 HAL.
 *
 * Ce module fournit une API MIDI haut niveau, indépendante du transport,
 * et un backend USB Device basé sur la classe usbd_midi.
 *
 * Rôle dans le système:
 * - Centralise la gestion MIDI (RX/TX) pour les tasklets.
 * - Découple le moteur et l'UI des transports (USB Device/Host).
 *
 * Contraintes temps réel:
 * - Critique audio: non.
 * - IRQ: RX USB en ISR -> file RX -> traitement en midi_poll().
 * - Tasklet: oui (midi_poll dans la boucle principale).
 * - Borné: oui (traitement par paquets/itérations).
 *
 * Architecture:
 * - Appelé par: main loop (midi_poll), callbacks USB Device.
 * - Appelle: usbd_midi, midi_host (backend host), diagnostics/logs.
 *
 * Règles:
 * - Pas de malloc.
 * - Pas d'émission USB en IRQ.
 *
 * @note L’API publique est déclarée dans midi.h.
 */

#include "midi.h"
#include "main.h"
#include "tusb.h"
#include <string.h>


midi_tx_stats_t midi_tx_stats = {0};
midi_rx_stats_t midi_rx_stats = {0};

volatile uint32_t midi_usb_rx_drops = 0;

static bool midi_initialized = false;
static midi_dest_t midi_rx_dest = MIDI_DEST_BOTH;

/* ====================================================================== */
/*                              CLOCK MIDI                               */
/* ====================================================================== */

static midi_clock_mode_t midi_clock_mode = MIDI_CLOCK_MODE_SLAVE;
static volatile bool midi_clock_running = false;
static midi_dest_t midi_clock_dest = MIDI_DEST_BOTH;

/* ====================================================================== */
/*                              FILES USB                                */
/* ====================================================================== */

#define MIDI_USB_TX_QUEUE_LEN 128U
#define MIDI_USB_RX_QUEUE_LEN 128U
#define MIDI_USB_MAX_BURST    16U

typedef struct {
  uint8_t bytes[4];
} midi_usb_packet_t;

static midi_usb_packet_t midi_usb_tx_queue[MIDI_USB_TX_QUEUE_LEN];
static midi_usb_packet_t midi_usb_rx_queue[MIDI_USB_RX_QUEUE_LEN];

static volatile uint16_t midi_usb_tx_head = 0U;
static volatile uint16_t midi_usb_tx_tail = 0U;
static volatile uint16_t midi_usb_tx_count = 0U;
static volatile uint16_t midi_usb_tx_high_water = 0U;

static volatile uint16_t midi_usb_rx_head = 0U;
static volatile uint16_t midi_usb_rx_tail = 0U;
static volatile uint16_t midi_usb_rx_count = 0U;
static volatile uint16_t midi_usb_rx_high_water = 0U;

static volatile bool midi_usb_tx_kick = false;

static inline uint32_t midi_enter_critical(void) {
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static inline void midi_exit_critical(uint32_t primask) {
  __set_PRIMASK(primask);
}

static inline bool midi_in_isr(void) {
  return (__get_IPSR() != 0U);
}

/* ====================================================================== */
/*                              BACKENDS                                 */
/* ====================================================================== */

static void midi_send(midi_dest_t dest, const uint8_t *msg, size_t len);
static void backend_usb_device_send(const uint8_t *msg, size_t len);
static void backend_usb_host_send(const uint8_t *msg, size_t len) __attribute__((unused));
static void backend_din_send(const uint8_t *msg, size_t len);

static bool usb_device_ready(void) {
  return (tud_midi_mounted());
}

static bool usb_device_send_packets(const uint8_t *buffer, uint16_t bytes_len)
{
  if (!usb_device_ready()) {
    return false;
  }

  // USB-MIDI = paquets de 4 octets
  uint32_t len = (bytes_len / 4) * 4;
  if (len == 0) {
    return false;
  }

  tud_midi_stream_write(0, buffer, len);
  return true;
}

static bool usb_tx_queue_push(const uint8_t packet[4]) {
  uint32_t primask = midi_enter_critical();
  if (midi_usb_tx_count >= MIDI_USB_TX_QUEUE_LEN) {
#if MIDI_MB_DROP_OLDEST
    midi_usb_tx_tail = (uint16_t)((midi_usb_tx_tail + 1U) % MIDI_USB_TX_QUEUE_LEN);
    midi_usb_tx_count--;
#else
    midi_exit_critical(primask);
    return false;
#endif
  }

  midi_usb_tx_queue[midi_usb_tx_head].bytes[0] = packet[0];
  midi_usb_tx_queue[midi_usb_tx_head].bytes[1] = packet[1];
  midi_usb_tx_queue[midi_usb_tx_head].bytes[2] = packet[2];
  midi_usb_tx_queue[midi_usb_tx_head].bytes[3] = packet[3];

  midi_usb_tx_head = (uint16_t)((midi_usb_tx_head + 1U) % MIDI_USB_TX_QUEUE_LEN);
  midi_usb_tx_count++;
  if (midi_usb_tx_count > midi_usb_tx_high_water) {
    midi_usb_tx_high_water = midi_usb_tx_count;
  }
  midi_exit_critical(primask);
  return true;
}

static bool usb_tx_queue_pop(midi_usb_packet_t *out) {
  uint32_t primask = midi_enter_critical();
  if (midi_usb_tx_count == 0U) {
    midi_exit_critical(primask);
    return false;
  }

  *out = midi_usb_tx_queue[midi_usb_tx_tail];
  midi_usb_tx_tail = (uint16_t)((midi_usb_tx_tail + 1U) % MIDI_USB_TX_QUEUE_LEN);
  midi_usb_tx_count--;
  midi_exit_critical(primask);
  return true;
}

static bool usb_rx_queue_push(const uint8_t packet[4]) {
  uint32_t primask = midi_enter_critical();
  if (midi_usb_rx_count >= MIDI_USB_RX_QUEUE_LEN) {
    midi_exit_critical(primask);
    return false;
  }

  midi_usb_rx_queue[midi_usb_rx_head].bytes[0] = packet[0];
  midi_usb_rx_queue[midi_usb_rx_head].bytes[1] = packet[1];
  midi_usb_rx_queue[midi_usb_rx_head].bytes[2] = packet[2];
  midi_usb_rx_queue[midi_usb_rx_head].bytes[3] = packet[3];

  midi_usb_rx_head = (uint16_t)((midi_usb_rx_head + 1U) % MIDI_USB_RX_QUEUE_LEN);
  midi_usb_rx_count++;
  if (midi_usb_rx_count > midi_usb_rx_high_water) {
    midi_usb_rx_high_water = midi_usb_rx_count;
  }
  midi_exit_critical(primask);
  return true;
}

static bool usb_rx_queue_pop(midi_usb_packet_t *out) {
  uint32_t primask = midi_enter_critical();
  if (midi_usb_rx_count == 0U) {
    midi_exit_critical(primask);
    return false;
  }

  *out = midi_usb_rx_queue[midi_usb_rx_tail];
  midi_usb_rx_tail = (uint16_t)((midi_usb_rx_tail + 1U) % MIDI_USB_RX_QUEUE_LEN);
  midi_usb_rx_count--;
  midi_exit_critical(primask);
  return true;
}

static void midi_usb_try_flush(void) {
  uint8_t buffer[4U * MIDI_USB_MAX_BURST];
  uint16_t packets = 0U;

  if (midi_in_isr()) {
    return;
  }

  if (!usb_device_ready()) {
    return;
  }

  while (packets < MIDI_USB_MAX_BURST) {
    midi_usb_packet_t packet;
    if (!usb_tx_queue_pop(&packet)) {
      break;
    }
    memcpy(&buffer[packets * 4U], packet.bytes, 4U);
    packets++;
  }

  if (packets == 0U) {
    return;
  }

  if (usb_device_send_packets(buffer, (uint16_t)(packets * 4U))) {
    midi_tx_stats.tx_sent_batched++;
  } else {
    midi_tx_stats.usb_not_ready_drops += packets;
  }
}

/* ====================================================================== */
/*                        RÉCEPTION USB (DÉCODAGE)                        */
/* ====================================================================== */

static bool usb_midi_decode_packet(const uint8_t pkt[4], midi_msg_t *out) {
  if (out == NULL) {
    return false;
  }

  const uint8_t cin = (uint8_t)(pkt[0] & 0x0F);

  switch (cin) {
    case 0x08: /* Note Off */
    case 0x09: /* Note On */
    case 0x0A: /* Poly Aftertouch */
    case 0x0B: /* Control Change */
    case 0x0E: /* Pitch Bend */
      out->len = 3U;
      out->data[0] = pkt[1];
      out->data[1] = pkt[2];
      out->data[2] = pkt[3];
      return true;

    case 0x0C: /* Program Change */
    case 0x0D: /* Channel Pressure */
      out->len = 2U;
      out->data[0] = pkt[1];
      out->data[1] = pkt[2];
      return true;

    case 0x02: /* System Common 2 bytes (MTC Quarter Frame / Song Select) */
      out->len = 2U;
      out->data[0] = pkt[1];
      out->data[1] = pkt[2];
      return true;

    case 0x03: /* System Common 3 bytes (Song Position Pointer) */
      out->len = 3U;
      out->data[0] = pkt[1];
      out->data[1] = pkt[2];
      out->data[2] = pkt[3];
      return true;

    case 0x0F: /* Single-byte real-time */
      switch (pkt[1]) {
        case 0xF8: /* Clock */
        case 0xFA: /* Start */
        case 0xFB: /* Continue */
        case 0xFC: /* Stop */
        case 0xFE: /* Active Sensing */
        case 0xFF: /* System Reset */
          out->len = 1U;
          out->data[0] = pkt[1];
          return true;
        default:
          return false;
      }
      break;

    default:
      break;
  }

  return false;
}

static void midi_dispatch_rx_message(const midi_msg_t *msg) {
  midi_internal_receive(msg->data, msg->len);

  if ((midi_rx_dest == MIDI_DEST_UART) || (midi_rx_dest == MIDI_DEST_BOTH)) {
    backend_din_send(msg->data, msg->len);
  }
}

static void midi_process_usb_rx(void) {
  uint32_t processed = 0U;
  while (processed < MIDI_USB_MAX_BURST) {
    midi_usb_packet_t packet;
    if (!usb_rx_queue_pop(&packet)) {
      break;
    }

    midi_msg_t msg;
    if (usb_midi_decode_packet(packet.bytes, &msg)) {
      midi_dispatch_rx_message(&msg);
      midi_rx_stats.usb_rx_decoded++;
    } else {
      midi_rx_stats.usb_rx_ignored++;
    }
    processed++;
  }
}

/* ====================================================================== */
/*                       TRANSMISSION USB (PROTOCOLE)                     */
/* ====================================================================== */

static void usb_device_enqueue_packet(const uint8_t packet[4]) {
  if (!usb_tx_queue_push(packet)) {
    midi_tx_stats.tx_mb_drops++;
  }
}

static void backend_usb_device_send(const uint8_t *msg, size_t len) {
  uint8_t packet[4] = {0, 0, 0, 0};
  const uint8_t st = msg[0];
  const uint8_t cable = (uint8_t)(MIDI_USB_CABLE << 4);

  /* Channel Voice */
  if ((st & 0xF0U) == 0x80U && len >= 3U) {
    packet[0] = (uint8_t)(cable | 0x08U);
    packet[1] = msg[0];
    packet[2] = msg[1];
    packet[3] = msg[2];
  } else if ((st & 0xF0U) == 0x90U && len >= 3U) {
    packet[0] = (uint8_t)(cable | 0x09U);
    packet[1] = msg[0];
    packet[2] = msg[1];
    packet[3] = msg[2];
  } else if ((st & 0xF0U) == 0xA0U && len >= 3U) {
    packet[0] = (uint8_t)(cable | 0x0AU);
    packet[1] = msg[0];
    packet[2] = msg[1];
    packet[3] = msg[2];
  } else if ((st & 0xF0U) == 0xB0U && len >= 3U) {
    packet[0] = (uint8_t)(cable | 0x0BU);
    packet[1] = msg[0];
    packet[2] = msg[1];
    packet[3] = msg[2];
  } else if ((st & 0xF0U) == 0xE0U && len >= 3U) {
    packet[0] = (uint8_t)(cable | 0x0EU);
    packet[1] = msg[0];
    packet[2] = msg[1];
    packet[3] = msg[2];
  } else if ((st & 0xF0U) == 0xC0U && len >= 2U) {
    packet[0] = (uint8_t)(cable | 0x0CU);
    packet[1] = msg[0];
    packet[2] = msg[1];
    packet[3] = 0U;
  } else if ((st & 0xF0U) == 0xD0U && len >= 2U) {
    packet[0] = (uint8_t)(cable | 0x0DU);
    packet[1] = msg[0];
    packet[2] = msg[1];
    packet[3] = 0U;
  }

  /* System Common */
  else if (st == 0xF1U && len >= 2U) {
    packet[0] = (uint8_t)(cable | 0x02U);
    packet[1] = 0xF1U;
    packet[2] = msg[1];
  } else if (st == 0xF2U && len >= 3U) {
    packet[0] = (uint8_t)(cable | 0x03U);
    packet[1] = 0xF2U;
    packet[2] = msg[1];
    packet[3] = msg[2];
  } else if (st == 0xF3U && len >= 2U) {
    packet[0] = (uint8_t)(cable | 0x02U);
    packet[1] = 0xF3U;
    packet[2] = msg[1];
  } else if (st == 0xF6U) {
    packet[0] = (uint8_t)(cable | 0x0FU);
    packet[1] = 0xF6U;
  }

  /* Realtime */
  else if (st >= 0xF8U) {
    packet[0] = (uint8_t)(cable | 0x0FU);
    packet[1] = st;
  } else {
    packet[0] = (uint8_t)(cable | 0x0FU);
    packet[1] = len > 0U ? msg[0] : 0U;
    packet[2] = len > 1U ? msg[1] : 0U;
    packet[3] = len > 2U ? msg[2] : 0U;
  }

  if (!midi_in_isr() && usb_device_ready() && midi_usb_tx_count == 0U) {
    if (usb_device_send_packets(packet, 4U)) {
      midi_tx_stats.tx_sent_immediate++;
      return;
    }
  }

  usb_device_enqueue_packet(packet);
  midi_usb_try_flush();
}

static void backend_usb_host_send(const uint8_t *msg, size_t len) {
  (void)msg;
  (void)len;
  /* Stub: USB Host MIDI backend à implémenter plus tard. */
}

static void backend_din_send(const uint8_t *msg, size_t len) {
  (void)msg;
  (void)len;
  /* Stub: MIDI DIN UART backend à implémenter plus tard. */
}

/* ====================================================================== */
/*                            API PUBLIQUE                                */
/* ====================================================================== */

__attribute__((weak)) void midi_internal_receive(const uint8_t *msg, size_t len) {
  (void)msg;
  (void)len;
}

void midi_init(void) {
  if (midi_initialized) {
    return;
  }

  midi_initialized = true;

  midi_usb_tx_head = 0U;
  midi_usb_tx_tail = 0U;
  midi_usb_tx_count = 0U;
  midi_usb_tx_high_water = 0U;

  midi_usb_rx_head = 0U;
  midi_usb_rx_tail = 0U;
  midi_usb_rx_count = 0U;
  midi_usb_rx_high_water = 0U;

  midi_usb_rx_drops = 0U;
  midi_usb_tx_kick = false;

  midi_stats_reset();
}

bool midi_is_initialized(void) {
  return midi_initialized;
}

void midi_set_rx_destination(midi_dest_t dest) {
  switch (dest) {
    case MIDI_DEST_UART:
    case MIDI_DEST_USB:
    case MIDI_DEST_BOTH:
      midi_rx_dest = dest;
      break;
    default:
      midi_rx_dest = MIDI_DEST_BOTH;
      break;
  }
}

midi_dest_t midi_get_rx_destination(void) {
  return midi_rx_dest;
}

void midi_poll(void) {
  if (!midi_initialized) {
    return;
  }

  if (midi_usb_tx_kick) {
    midi_usb_tx_kick = false;
  }

  midi_process_usb_rx();
  midi_usb_try_flush();
}

void midi_send_raw(midi_dest_t dest, const uint8_t *msg, size_t len) {
  if ((msg == NULL) || (len == 0U)) {
    return;
  }

  midi_send(dest, msg, len);
}

void midi_clock_set_mode(midi_clock_mode_t mode) {
  midi_clock_mode = mode;
}

midi_clock_mode_t midi_clock_get_mode(void) {
  return midi_clock_mode;
}

void midi_clock_set_running(bool running) {
  midi_clock_running = running;
}

bool midi_clock_is_running(void) {
  return midi_clock_running;
}

void midi_clock_set_destination(midi_dest_t dest) {
  midi_clock_dest = dest;
}

midi_dest_t midi_clock_get_destination(void) {
  return midi_clock_dest;
}

void midi_clock_on_timer_tick(void) {
  /* TODO CubeMX: configure TIMx à 24 PPQN pour générer l'horloge MIDI. */
  if (midi_clock_mode == MIDI_CLOCK_MODE_MASTER && midi_clock_running) {
    midi_clock(midi_clock_dest);
  }
}

/* ====================================================================== */
/*                            ROUTAGE MIDI                                */
/* ====================================================================== */

static void midi_send(midi_dest_t dest, const uint8_t *msg, size_t len) {
  switch (dest) {
    case MIDI_DEST_UART:
      backend_din_send(msg, len);
      break;
    case MIDI_DEST_USB:
      backend_usb_device_send(msg, len);
      break;
    case MIDI_DEST_BOTH:
      backend_din_send(msg, len);
      backend_usb_device_send(msg, len);
      break;
    default:
      break;
  }
}

/* ====================================================================== */
/*                             API MIDI                                   */
/* ====================================================================== */

void midi_note_on(midi_dest_t dest, uint8_t ch, uint8_t note, uint8_t vel) {
  if ((vel & 0x7FU) == 0U) {
    midi_note_off(dest, ch, note, 0U);
    return;
  }
  uint8_t msg[3] = { (uint8_t)(0x90U | (ch & 0x0FU)), (uint8_t)(note & 0x7FU), (uint8_t)(vel & 0x7FU) };
  midi_send(dest, msg, 3U);
}

void midi_note_off(midi_dest_t dest, uint8_t ch, uint8_t note, uint8_t vel) {
  uint8_t msg[3] = { (uint8_t)(0x80U | (ch & 0x0FU)), (uint8_t)(note & 0x7FU), (uint8_t)(vel & 0x7FU) };
  midi_send(dest, msg, 3U);
}

void midi_poly_aftertouch(midi_dest_t dest, uint8_t ch, uint8_t note, uint8_t pressure) {
  uint8_t msg[3] = { (uint8_t)(0xA0U | (ch & 0x0FU)), (uint8_t)(note & 0x7FU), (uint8_t)(pressure & 0x7FU) };
  midi_send(dest, msg, 3U);
}

void midi_cc(midi_dest_t dest, uint8_t ch, uint8_t cc, uint8_t val) {
  uint8_t msg[3] = { (uint8_t)(0xB0U | (ch & 0x0FU)), (uint8_t)(cc & 0x7FU), (uint8_t)(val & 0x7FU) };
  midi_send(dest, msg, 3U);
}

void midi_program_change(midi_dest_t dest, uint8_t ch, uint8_t program) {
  uint8_t msg[2] = { (uint8_t)(0xC0U | (ch & 0x0FU)), (uint8_t)(program & 0x7FU) };
  midi_send(dest, msg, 2U);
}

void midi_channel_pressure(midi_dest_t dest, uint8_t ch, uint8_t pressure) {
  uint8_t msg[2] = { (uint8_t)(0xD0U | (ch & 0x0FU)), (uint8_t)(pressure & 0x7FU) };
  midi_send(dest, msg, 2U);
}

void midi_pitchbend(midi_dest_t dest, uint8_t ch, int16_t value14b) {
  uint16_t value = (uint16_t)(value14b + 8192);
  uint8_t lsb = (uint8_t)(value & 0x7FU);
  uint8_t msb = (uint8_t)((value >> 7) & 0x7FU);
  uint8_t msg[3] = { (uint8_t)(0xE0U | (ch & 0x0FU)), lsb, msb };
  midi_send(dest, msg, 3U);
}

void midi_mtc_quarter_frame(midi_dest_t dest, uint8_t qf) {
  uint8_t msg[2] = { 0xF1U, (uint8_t)(qf & 0x7FU) };
  midi_send(dest, msg, 2U);
}

void midi_song_position(midi_dest_t dest, uint16_t pos14) {
  uint8_t lsb = (uint8_t)(pos14 & 0x7FU);
  uint8_t msb = (uint8_t)((pos14 >> 7) & 0x7FU);
  uint8_t msg[3] = { 0xF2U, lsb, msb };
  midi_send(dest, msg, 3U);
}

void midi_song_select(midi_dest_t dest, uint8_t song) {
  uint8_t msg[2] = { 0xF3U, (uint8_t)(song & 0x7FU) };
  midi_send(dest, msg, 2U);
}

void midi_tune_request(midi_dest_t dest) {
  uint8_t msg[1] = { 0xF6U };
  midi_send(dest, msg, 1U);
}

void midi_clock(midi_dest_t dest) {
  uint8_t msg[1] = { 0xF8U };
  midi_send(dest, msg, 1U);
}

void midi_start(midi_dest_t dest) {
  uint8_t msg[1] = { 0xFAU };
  midi_send(dest, msg, 1U);
}

void midi_continue(midi_dest_t dest) {
  uint8_t msg[1] = { 0xFBU };
  midi_send(dest, msg, 1U);
}

void midi_stop(midi_dest_t dest) {
  uint8_t msg[1] = { 0xFCU };
  midi_send(dest, msg, 1U);
}

void midi_active_sensing(midi_dest_t dest) {
  uint8_t msg[1] = { 0xFEU };
  midi_send(dest, msg, 1U);
}

void midi_system_reset(midi_dest_t dest) {
  uint8_t msg[1] = { 0xFFU };
  midi_send(dest, msg, 1U);
}

static void midi_channel_mode_cc(midi_dest_t dest, uint8_t ch, uint8_t control, uint8_t value) {
  uint8_t msg[3] = {
    (uint8_t)(0xB0U | (ch & 0x0FU)),
    (uint8_t)(control & 0x7FU),
    (uint8_t)(value & 0x7FU)
  };
  midi_send(dest, msg, 3U);
}

void midi_all_sound_off(midi_dest_t dest, uint8_t ch) {
  midi_channel_mode_cc(dest, ch, 120U, 0U);
}

void midi_reset_all_controllers(midi_dest_t dest, uint8_t ch) {
  midi_channel_mode_cc(dest, ch, 121U, 0U);
}

void midi_local_control(midi_dest_t dest, uint8_t ch, bool on) {
  midi_channel_mode_cc(dest, ch, 122U, on ? 127U : 0U);
}

void midi_all_notes_off(midi_dest_t dest, uint8_t ch) {
  midi_channel_mode_cc(dest, ch, 123U, 0U);
}

void midi_omni_mode_off(midi_dest_t dest, uint8_t ch) {
  midi_channel_mode_cc(dest, ch, 124U, 0U);
}

void midi_omni_mode_on(midi_dest_t dest, uint8_t ch) {
  midi_channel_mode_cc(dest, ch, 125U, 0U);
}

void midi_mono_mode_on(midi_dest_t dest, uint8_t ch, uint8_t num_channels) {
  midi_channel_mode_cc(dest, ch, 126U, num_channels);
}

void midi_poly_mode_on(midi_dest_t dest, uint8_t ch) {
  midi_channel_mode_cc(dest, ch, 127U, 0U);
}

uint16_t midi_usb_queue_high_watermark(void) {
  return midi_usb_tx_high_water;
}

uint16_t midi_usb_rx_high_watermark(void) {
  return midi_usb_rx_high_water;
}

void midi_stats_reset(void) {
  midi_tx_stats = (midi_tx_stats_t){0};
  midi_rx_stats = (midi_rx_stats_t){0};
  midi_usb_rx_drops = 0U;
}

/* ====================================================================== */
/*                       CALLBACKS USB MIDI (ISR)                         */
/* ====================================================================== */

void midi_usb_rx_submit_from_isr(const uint8_t *packet, size_t len) {
  if ((packet == NULL) || (len < 4U)) {
    return;
  }

  size_t packets = len / 4U;
  for (size_t i = 0U; i < packets; i++) {
    if (!usb_rx_queue_push(packet)) {
      midi_usb_rx_drops++;
      midi_rx_stats.usb_rx_drops++;
    } else {
      midi_rx_stats.usb_rx_enqueued++;
    }
    packet += 4U;
  }
}

void tud_midi_rx_cb(uint8_t itf)
{
  (void)itf;

  uint8_t packet[4];

  while (tud_midi_packet_read(packet))
  {
    midi_usb_rx_submit_from_isr(packet, 4);
  }
}



