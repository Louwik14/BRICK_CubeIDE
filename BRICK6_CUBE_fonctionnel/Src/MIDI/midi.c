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
#include "tim.h"
#include "usbd_midi.h"
#include "Keyboard/keyboard_engine.h"
#include "Seq/seq_runtime.h"
#include <string.h>

extern USBD_HandleTypeDef hUsbDeviceFS;

midi_tx_stats_t midi_tx_stats = {0};
midi_rx_stats_t midi_rx_stats = {0};

volatile uint32_t midi_usb_rx_drops = 0;

static bool midi_initialized = false;
static midi_dest_t midi_rx_dest = MIDI_DEST_BOTH;

/* ====================================================================== */
/*                              CLOCK MIDI                               */
/* ====================================================================== */

static midi_clock_mode_t midi_clock_mode = MIDI_CLOCK_MODE_MASTER;
static volatile bool midi_clock_running = false;
static midi_dest_t midi_clock_dest = MIDI_DEST_BOTH;

#define MIDI_CLOCK_TIMER_HZ           1000000ULL
#define MIDI_CLOCK_TICKS_PER_MIN_X1K  (MIDI_CLOCK_TIMER_HZ * 60ULL * 1000ULL)
#define MIDI_CLOCK_PPQN               24ULL
#define MIDI_CLOCK_DEFAULT_BPM_MILLI  120000UL

static volatile uint32_t midi_clock_bpm_milli = MIDI_CLOCK_DEFAULT_BPM_MILLI;
static volatile uint32_t midi_clock_period_ticks = 0U;
static volatile uint32_t midi_clock_period_rem = 0U;
static volatile uint32_t midi_clock_period_den = 1U;
static volatile uint32_t midi_clock_rem_accum = 0U;
static volatile uint32_t midi_clock_next_ccr = 0U;
static volatile bool midi_clock_timer_armed = false;

static inline uint32_t midi_clock_compute_next_delta_ticks(void) {
  uint32_t delta = midi_clock_period_ticks;
  uint32_t rem_accum = midi_clock_rem_accum + midi_clock_period_rem;
  if (rem_accum >= midi_clock_period_den) {
    rem_accum -= midi_clock_period_den;
    delta += 1U;
  }
  midi_clock_rem_accum = rem_accum;
  return delta;
}

static void midi_clock_recompute_period(uint32_t bpm_milli) {
  if (bpm_milli == 0U) {
    bpm_milli = MIDI_CLOCK_DEFAULT_BPM_MILLI;
  }

  const uint32_t den = (uint32_t)(MIDI_CLOCK_PPQN * (uint64_t)bpm_milli);
  const uint64_t num = MIDI_CLOCK_TICKS_PER_MIN_X1K;

  midi_clock_bpm_milli = bpm_milli;
  midi_clock_period_ticks = (uint32_t)(num / den);
  midi_clock_period_rem = (uint32_t)(num % den);
  midi_clock_period_den = den;
  midi_clock_rem_accum = 0U;
}

static void midi_clock_hw_start(void) {
  if (midi_clock_timer_armed) {
    return;
  }

  const uint32_t now = __HAL_TIM_GET_COUNTER(&htim5);
  const uint32_t warmup = 200U;
  const uint32_t first_delta = midi_clock_compute_next_delta_ticks();
  midi_clock_next_ccr = now + warmup + first_delta;
  __HAL_TIM_SET_COMPARE(&htim5, TIM_CHANNEL_1, midi_clock_next_ccr);
  __HAL_TIM_ENABLE_IT(&htim5, TIM_IT_CC1);
  midi_clock_timer_armed = true;
}

static void midi_clock_hw_stop(void) {
  __HAL_TIM_DISABLE_IT(&htim5, TIM_IT_CC1);
  midi_clock_timer_armed = false;
  midi_clock_rem_accum = 0U;
}

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

/**
 * @brief Point d'entrée midi_enter_critical.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_enter_critical.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline uint32_t midi_enter_critical(void) {
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

/**
 * @brief Point d'entrée midi_exit_critical.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_exit_critical.
 *
 * @param primask Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline void midi_exit_critical(uint32_t primask) {
  __set_PRIMASK(primask);
}

/**
 * @brief Point d'entrée midi_in_isr.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_in_isr.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
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

/**
 * @brief Point d'entrée usb_device_ready.
 *
 * Rôle:
 * - Exécuter le traitement associé à usb_device_ready.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static bool usb_device_ready(void) {
  return (USBD_MIDI_GetState(&hUsbDeviceFS) == MIDI_IDLE);
}

/**
 * @brief Point d'entrée usb_device_send_packets.
 *
 * Rôle:
 * - Exécuter le traitement associé à usb_device_send_packets.
 *
 * @param buffer Paramètre d'entrée de l'API.
 * @param bytes_len Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static bool usb_device_send_packets(const uint8_t *buffer, uint16_t bytes_len) {
  if (!usb_device_ready()) {
    return false;
  }

  USBD_MIDI_SendPackets(&hUsbDeviceFS, (uint8_t *)buffer, bytes_len);
  return true;
}

/**
 * @brief Point d'entrée usb_tx_queue_push.
 *
 * Rôle:
 * - Exécuter le traitement associé à usb_tx_queue_push.
 *
 * @param packet Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
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

/**
 * @brief Point d'entrée usb_tx_queue_pop.
 *
 * Rôle:
 * - Exécuter le traitement associé à usb_tx_queue_pop.
 *
 * @param out Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
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

static bool usb_tx_queue_push_front(const midi_usb_packet_t *packet) {
  if (packet == NULL) {
    return false;
  }

  uint32_t primask = midi_enter_critical();
  if (midi_usb_tx_count >= MIDI_USB_TX_QUEUE_LEN) {
    midi_exit_critical(primask);
    return false;
  }

  midi_usb_tx_tail = (uint16_t)((midi_usb_tx_tail + MIDI_USB_TX_QUEUE_LEN - 1U) % MIDI_USB_TX_QUEUE_LEN);
  midi_usb_tx_queue[midi_usb_tx_tail] = *packet;
  midi_usb_tx_count++;
  if (midi_usb_tx_count > midi_usb_tx_high_water) {
    midi_usb_tx_high_water = midi_usb_tx_count;
  }
  midi_exit_critical(primask);
  return true;
}

/**
 * @brief Point d'entrée usb_rx_queue_push.
 *
 * Rôle:
 * - Exécuter le traitement associé à usb_rx_queue_push.
 *
 * @param packet Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
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

/**
 * @brief Point d'entrée usb_rx_queue_pop.
 *
 * Rôle:
 * - Exécuter le traitement associé à usb_rx_queue_pop.
 *
 * @param out Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
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

/**
 * @brief Point d'entrée midi_usb_try_flush.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_usb_try_flush.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
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
    for (uint16_t i = 0U; i < packets; ++i) {
      midi_usb_packet_t packet;
      packet.bytes[0] = buffer[(packets - 1U - i) * 4U + 0U];
      packet.bytes[1] = buffer[(packets - 1U - i) * 4U + 1U];
      packet.bytes[2] = buffer[(packets - 1U - i) * 4U + 2U];
      packet.bytes[3] = buffer[(packets - 1U - i) * 4U + 3U];
      if (!usb_tx_queue_push_front(&packet)) {
        midi_tx_stats.usb_not_ready_drops++;
      }
    }
  }
}

/* ====================================================================== */
/*                        RÉCEPTION USB (DÉCODAGE)                        */
/* ====================================================================== */

/**
 * @brief Point d'entrée usb_midi_decode_packet.
 *
 * Rôle:
 * - Exécuter le traitement associé à usb_midi_decode_packet.
 *
 * @param pkt Paramètre d'entrée de l'API.
 * @param out Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
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

/**
 * @brief Point d'entrée midi_dispatch_rx_message.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_dispatch_rx_message.
 *
 * @param msg Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void midi_dispatch_rx_message(const midi_msg_t *msg) {
  midi_internal_receive(msg->data, msg->len);

  if ((midi_rx_dest == MIDI_DEST_UART) || (midi_rx_dest == MIDI_DEST_BOTH)) {
    backend_din_send(msg->data, msg->len);
  }
}

/**
 * @brief Point d'entrée midi_process_usb_rx.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_process_usb_rx.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
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

/**
 * @brief Point d'entrée usb_device_enqueue_packet.
 *
 * Rôle:
 * - Exécuter le traitement associé à usb_device_enqueue_packet.
 *
 * @param packet Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void usb_device_enqueue_packet(const uint8_t packet[4]) {
  if (!usb_tx_queue_push(packet)) {
    midi_tx_stats.tx_mb_drops++;
  }
}

/**
 * @brief Point d'entrée backend_usb_device_send.
 *
 * Rôle:
 * - Exécuter le traitement associé à backend_usb_device_send.
 *
 * @param msg Paramètre d'entrée de l'API.
 * @param len Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
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

/**
 * @brief Point d'entrée backend_usb_host_send.
 *
 * Rôle:
 * - Exécuter le traitement associé à backend_usb_host_send.
 *
 * @param msg Paramètre d'entrée de l'API.
 * @param len Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void backend_usb_host_send(const uint8_t *msg, size_t len) {
  (void)msg;
  (void)len;
  /* Stub: USB Host MIDI backend à implémenter plus tard. */
}

/**
 * @brief Point d'entrée backend_din_send.
 *
 * Rôle:
 * - Exécuter le traitement associé à backend_din_send.
 *
 * @param msg Paramètre d'entrée de l'API.
 * @param len Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void backend_din_send(const uint8_t *msg, size_t len) {
  (void)msg;
  (void)len;
  /* Stub: MIDI DIN UART backend à implémenter plus tard. */
}

/* ====================================================================== */
/*                            API PUBLIQUE                                */
/* ====================================================================== */

__attribute__((weak)) void midi_internal_receive(const uint8_t *msg, size_t len) {
  if ((msg == NULL) || (len == 0U)) {
    return;
  }

  switch (msg[0]) {
    case 0xF8U: /* MIDI Clock */
      seq_runtime_midi_clock();
      break;
    case 0xFAU: /* MIDI Start */
      seq_runtime_midi_start();
      break;
    case 0xFBU: /* MIDI Continue */
      seq_runtime_midi_continue();
      break;
    case 0xFCU: /* MIDI Stop */
      seq_runtime_midi_stop();
      break;
    default:
      keyboard_engine_midi_receive(msg, len);
      break;
  }
}

/**
 * @brief Point d'entrée midi_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
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
  midi_clock_recompute_period(MIDI_CLOCK_DEFAULT_BPM_MILLI);
  midi_clock_hw_stop();

  midi_stats_reset();
}

void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef *htim) {
  if ((htim != NULL) && (htim->Instance == TIM5) && (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)) {
    midi_clock_on_timer_tick();
  }
}

/**
 * @brief Point d'entrée midi_is_initialized.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_is_initialized.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
bool midi_is_initialized(void) {
  return midi_initialized;
}

/**
 * @brief Point d'entrée midi_set_rx_destination.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_set_rx_destination.
 *
 * @param dest Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
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

/**
 * @brief Point d'entrée midi_get_rx_destination.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_get_rx_destination.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
midi_dest_t midi_get_rx_destination(void) {
  return midi_rx_dest;
}

/**
 * @brief Point d'entrée midi_poll.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_poll.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
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

/**
 * @brief Point d'entrée midi_send_raw.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_send_raw.
 *
 * @param dest Paramètre d'entrée de l'API.
 * @param msg Paramètre d'entrée de l'API.
 * @param len Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_send_raw(midi_dest_t dest, const uint8_t *msg, size_t len) {
  if ((msg == NULL) || (len == 0U)) {
    return;
  }

  midi_send(dest, msg, len);
}

/**
 * @brief Point d'entrée midi_clock_set_mode.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_clock_set_mode.
 *
 * @param mode Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_clock_set_mode(midi_clock_mode_t mode) {
  midi_clock_mode = mode;
  if (mode != MIDI_CLOCK_MODE_MASTER) {
    midi_clock_hw_stop();
  } else if (midi_clock_running) {
    midi_clock_hw_start();
  }
}

/**
 * @brief Point d'entrée midi_clock_get_mode.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_clock_get_mode.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
midi_clock_mode_t midi_clock_get_mode(void) {
  return midi_clock_mode;
}

void midi_clock_set_bpm_milli(uint32_t bpm_milli) {
  uint32_t primask = midi_enter_critical();
  midi_clock_recompute_period(bpm_milli);
  midi_exit_critical(primask);
}

uint32_t midi_clock_get_bpm_milli(void) {
  return midi_clock_bpm_milli;
}

/**
 * @brief Point d'entrée midi_clock_set_running.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_clock_set_running.
 *
 * @param running Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_clock_set_running(bool running) {
  uint32_t primask = midi_enter_critical();
  midi_clock_running = running;
  if (!running) {
    midi_clock_hw_stop();
  } else if (midi_clock_mode == MIDI_CLOCK_MODE_MASTER) {
    midi_clock_hw_start();
  }
  midi_exit_critical(primask);
}

/**
 * @brief Point d'entrée midi_clock_is_running.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_clock_is_running.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
bool midi_clock_is_running(void) {
  return midi_clock_running;
}

/**
 * @brief Point d'entrée midi_clock_set_destination.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_clock_set_destination.
 *
 * @param dest Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_clock_set_destination(midi_dest_t dest) {
  midi_clock_dest = dest;
}

/**
 * @brief Point d'entrée midi_clock_get_destination.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_clock_get_destination.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
midi_dest_t midi_clock_get_destination(void) {
  return midi_clock_dest;
}

/**
 * @brief Point d'entrée midi_clock_on_timer_tick.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_clock_on_timer_tick.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_clock_on_timer_tick(void) {
  if (!midi_clock_timer_armed) {
    return;
  }

  if (midi_clock_mode == MIDI_CLOCK_MODE_MASTER && midi_clock_running) {
    midi_clock(midi_clock_dest);
    const uint32_t delta = midi_clock_compute_next_delta_ticks();
    midi_clock_next_ccr += delta;
    __HAL_TIM_SET_COMPARE(&htim5, TIM_CHANNEL_1, midi_clock_next_ccr);
    return;
  }

  midi_clock_hw_stop();
}

/* ====================================================================== */
/*                            ROUTAGE MIDI                                */
/* ====================================================================== */

/**
 * @brief Point d'entrée midi_send.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_send.
 *
 * @param dest Paramètre d'entrée de l'API.
 * @param msg Paramètre d'entrée de l'API.
 * @param len Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
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

/**
 * @brief Point d'entrée midi_note_on.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_note_on.
 *
 * @param dest Paramètre d'entrée de l'API.
 * @param ch Paramètre d'entrée de l'API.
 * @param note Paramètre d'entrée de l'API.
 * @param vel Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_note_on(midi_dest_t dest, uint8_t ch, uint8_t note, uint8_t vel) {
  if ((vel & 0x7FU) == 0U) {
    midi_note_off(dest, ch, note, 0U);
    return;
  }
  uint8_t msg[3] = { (uint8_t)(0x90U | (ch & 0x0FU)), (uint8_t)(note & 0x7FU), (uint8_t)(vel & 0x7FU) };
  midi_send(dest, msg, 3U);
}

/**
 * @brief Point d'entrée midi_note_off.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_note_off.
 *
 * @param dest Paramètre d'entrée de l'API.
 * @param ch Paramètre d'entrée de l'API.
 * @param note Paramètre d'entrée de l'API.
 * @param vel Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_note_off(midi_dest_t dest, uint8_t ch, uint8_t note, uint8_t vel) {
  uint8_t msg[3] = { (uint8_t)(0x80U | (ch & 0x0FU)), (uint8_t)(note & 0x7FU), (uint8_t)(vel & 0x7FU) };
  midi_send(dest, msg, 3U);
}

/**
 * @brief Point d'entrée midi_poly_aftertouch.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_poly_aftertouch.
 *
 * @param dest Paramètre d'entrée de l'API.
 * @param ch Paramètre d'entrée de l'API.
 * @param note Paramètre d'entrée de l'API.
 * @param pressure Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_poly_aftertouch(midi_dest_t dest, uint8_t ch, uint8_t note, uint8_t pressure) {
  uint8_t msg[3] = { (uint8_t)(0xA0U | (ch & 0x0FU)), (uint8_t)(note & 0x7FU), (uint8_t)(pressure & 0x7FU) };
  midi_send(dest, msg, 3U);
}

/**
 * @brief Point d'entrée midi_cc.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_cc.
 *
 * @param dest Paramètre d'entrée de l'API.
 * @param ch Paramètre d'entrée de l'API.
 * @param cc Paramètre d'entrée de l'API.
 * @param val Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_cc(midi_dest_t dest, uint8_t ch, uint8_t cc, uint8_t val) {
  uint8_t msg[3] = { (uint8_t)(0xB0U | (ch & 0x0FU)), (uint8_t)(cc & 0x7FU), (uint8_t)(val & 0x7FU) };
  midi_send(dest, msg, 3U);
}

/**
 * @brief Point d'entrée midi_program_change.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_program_change.
 *
 * @param dest Paramètre d'entrée de l'API.
 * @param ch Paramètre d'entrée de l'API.
 * @param program Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_program_change(midi_dest_t dest, uint8_t ch, uint8_t program) {
  uint8_t msg[2] = { (uint8_t)(0xC0U | (ch & 0x0FU)), (uint8_t)(program & 0x7FU) };
  midi_send(dest, msg, 2U);
}

/**
 * @brief Point d'entrée midi_channel_pressure.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_channel_pressure.
 *
 * @param dest Paramètre d'entrée de l'API.
 * @param ch Paramètre d'entrée de l'API.
 * @param pressure Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_channel_pressure(midi_dest_t dest, uint8_t ch, uint8_t pressure) {
  uint8_t msg[2] = { (uint8_t)(0xD0U | (ch & 0x0FU)), (uint8_t)(pressure & 0x7FU) };
  midi_send(dest, msg, 2U);
}

/**
 * @brief Point d'entrée midi_pitchbend.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_pitchbend.
 *
 * @param dest Paramètre d'entrée de l'API.
 * @param ch Paramètre d'entrée de l'API.
 * @param value14b Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_pitchbend(midi_dest_t dest, uint8_t ch, int16_t value14b) {
  uint16_t value = (uint16_t)(value14b + 8192);
  uint8_t lsb = (uint8_t)(value & 0x7FU);
  uint8_t msb = (uint8_t)((value >> 7) & 0x7FU);
  uint8_t msg[3] = { (uint8_t)(0xE0U | (ch & 0x0FU)), lsb, msb };
  midi_send(dest, msg, 3U);
}

/**
 * @brief Point d'entrée midi_mtc_quarter_frame.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_mtc_quarter_frame.
 *
 * @param dest Paramètre d'entrée de l'API.
 * @param qf Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_mtc_quarter_frame(midi_dest_t dest, uint8_t qf) {
  uint8_t msg[2] = { 0xF1U, (uint8_t)(qf & 0x7FU) };
  midi_send(dest, msg, 2U);
}

/**
 * @brief Point d'entrée midi_song_position.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_song_position.
 *
 * @param dest Paramètre d'entrée de l'API.
 * @param pos14 Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_song_position(midi_dest_t dest, uint16_t pos14) {
  uint8_t lsb = (uint8_t)(pos14 & 0x7FU);
  uint8_t msb = (uint8_t)((pos14 >> 7) & 0x7FU);
  uint8_t msg[3] = { 0xF2U, lsb, msb };
  midi_send(dest, msg, 3U);
}

/**
 * @brief Point d'entrée midi_song_select.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_song_select.
 *
 * @param dest Paramètre d'entrée de l'API.
 * @param song Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_song_select(midi_dest_t dest, uint8_t song) {
  uint8_t msg[2] = { 0xF3U, (uint8_t)(song & 0x7FU) };
  midi_send(dest, msg, 2U);
}

/**
 * @brief Point d'entrée midi_tune_request.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_tune_request.
 *
 * @param dest Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_tune_request(midi_dest_t dest) {
  uint8_t msg[1] = { 0xF6U };
  midi_send(dest, msg, 1U);
}

/**
 * @brief Point d'entrée midi_clock.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_clock.
 *
 * @param dest Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_clock(midi_dest_t dest) {
  uint8_t msg[1] = { 0xF8U };
  midi_send(dest, msg, 1U);
}

/**
 * @brief Point d'entrée midi_start.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_start.
 *
 * @param dest Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_start(midi_dest_t dest) {
  uint8_t msg[1] = { 0xFAU };
  midi_send(dest, msg, 1U);

  if (midi_clock_mode == MIDI_CLOCK_MODE_MASTER) {
    midi_clock_set_running(true);
  }
}

/**
 * @brief Point d'entrée midi_continue.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_continue.
 *
 * @param dest Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_continue(midi_dest_t dest) {
  uint8_t msg[1] = { 0xFBU };
  midi_send(dest, msg, 1U);

  if (midi_clock_mode == MIDI_CLOCK_MODE_MASTER) {
    midi_clock_set_running(true);
  }
}

/**
 * @brief Point d'entrée midi_stop.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_stop.
 *
 * @param dest Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_stop(midi_dest_t dest) {
  uint8_t msg[1] = { 0xFCU };
  midi_send(dest, msg, 1U);

  if (midi_clock_mode == MIDI_CLOCK_MODE_MASTER) {
    midi_clock_set_running(false);
  }
}

/**
 * @brief Point d'entrée midi_active_sensing.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_active_sensing.
 *
 * @param dest Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_active_sensing(midi_dest_t dest) {
  uint8_t msg[1] = { 0xFEU };
  midi_send(dest, msg, 1U);
}

/**
 * @brief Point d'entrée midi_system_reset.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_system_reset.
 *
 * @param dest Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_system_reset(midi_dest_t dest) {
  uint8_t msg[1] = { 0xFFU };
  midi_send(dest, msg, 1U);
}

/**
 * @brief Point d'entrée midi_channel_mode_cc.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_channel_mode_cc.
 *
 * @param dest Paramètre d'entrée de l'API.
 * @param ch Paramètre d'entrée de l'API.
 * @param control Paramètre d'entrée de l'API.
 * @param value Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void midi_channel_mode_cc(midi_dest_t dest, uint8_t ch, uint8_t control, uint8_t value) {
  uint8_t msg[3] = {
    (uint8_t)(0xB0U | (ch & 0x0FU)),
    (uint8_t)(control & 0x7FU),
    (uint8_t)(value & 0x7FU)
  };
  midi_send(dest, msg, 3U);
}

/**
 * @brief Point d'entrée midi_all_sound_off.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_all_sound_off.
 *
 * @param dest Paramètre d'entrée de l'API.
 * @param ch Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_all_sound_off(midi_dest_t dest, uint8_t ch) {
  midi_channel_mode_cc(dest, ch, 120U, 0U);
}

/**
 * @brief Point d'entrée midi_reset_all_controllers.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_reset_all_controllers.
 *
 * @param dest Paramètre d'entrée de l'API.
 * @param ch Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_reset_all_controllers(midi_dest_t dest, uint8_t ch) {
  midi_channel_mode_cc(dest, ch, 121U, 0U);
}

/**
 * @brief Point d'entrée midi_local_control.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_local_control.
 *
 * @param dest Paramètre d'entrée de l'API.
 * @param ch Paramètre d'entrée de l'API.
 * @param on Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_local_control(midi_dest_t dest, uint8_t ch, bool on) {
  midi_channel_mode_cc(dest, ch, 122U, on ? 127U : 0U);
}

/**
 * @brief Point d'entrée midi_all_notes_off.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_all_notes_off.
 *
 * @param dest Paramètre d'entrée de l'API.
 * @param ch Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_all_notes_off(midi_dest_t dest, uint8_t ch) {
  midi_channel_mode_cc(dest, ch, 123U, 0U);
}

/**
 * @brief Point d'entrée midi_omni_mode_off.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_omni_mode_off.
 *
 * @param dest Paramètre d'entrée de l'API.
 * @param ch Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_omni_mode_off(midi_dest_t dest, uint8_t ch) {
  midi_channel_mode_cc(dest, ch, 124U, 0U);
}

/**
 * @brief Point d'entrée midi_omni_mode_on.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_omni_mode_on.
 *
 * @param dest Paramètre d'entrée de l'API.
 * @param ch Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_omni_mode_on(midi_dest_t dest, uint8_t ch) {
  midi_channel_mode_cc(dest, ch, 125U, 0U);
}

/**
 * @brief Point d'entrée midi_mono_mode_on.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_mono_mode_on.
 *
 * @param dest Paramètre d'entrée de l'API.
 * @param ch Paramètre d'entrée de l'API.
 * @param num_channels Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_mono_mode_on(midi_dest_t dest, uint8_t ch, uint8_t num_channels) {
  midi_channel_mode_cc(dest, ch, 126U, num_channels);
}

/**
 * @brief Point d'entrée midi_poly_mode_on.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_poly_mode_on.
 *
 * @param dest Paramètre d'entrée de l'API.
 * @param ch Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_poly_mode_on(midi_dest_t dest, uint8_t ch) {
  midi_channel_mode_cc(dest, ch, 127U, 0U);
}

/**
 * @brief Point d'entrée midi_usb_queue_high_watermark.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_usb_queue_high_watermark.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
uint16_t midi_usb_queue_high_watermark(void) {
  return midi_usb_tx_high_water;
}

/**
 * @brief Point d'entrée midi_usb_rx_high_watermark.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_usb_rx_high_watermark.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
uint16_t midi_usb_rx_high_watermark(void) {
  return midi_usb_rx_high_water;
}

/**
 * @brief Point d'entrée midi_stats_reset.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_stats_reset.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_stats_reset(void) {
  midi_tx_stats = (midi_tx_stats_t){0};
  midi_rx_stats = (midi_rx_stats_t){0};
  midi_usb_rx_drops = 0U;
}

/* ====================================================================== */
/*                       CALLBACKS USB MIDI (ISR)                         */
/* ====================================================================== */

/**
 * @brief Point d'entrée midi_usb_rx_submit_from_isr.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_usb_rx_submit_from_isr.
 *
 * @param packet Paramètre d'entrée de l'API.
 * @param len Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
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

/**
 * @brief Point d'entrée USBD_MIDI_OnPacketsReceived.
 *
 * Rôle:
 * - Exécuter le traitement associé à USBD_MIDI_OnPacketsReceived.
 *
 * @param data Paramètre d'entrée de l'API.
 * @param len Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void USBD_MIDI_OnPacketsReceived(uint8_t *data, uint8_t len) {
  midi_usb_rx_submit_from_isr(data, len);
}

/**
 * @brief Point d'entrée USBD_MIDI_OnPacketsSent.
 *
 * Rôle:
 * - Exécuter le traitement associé à USBD_MIDI_OnPacketsSent.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void USBD_MIDI_OnPacketsSent(void) {
  /* Interruption USB: ne pas émettre ici, seulement demander un flush. */
  midi_usb_tx_kick = true;
}
