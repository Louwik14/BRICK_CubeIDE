/**
 * @file midi_host.c
 * @brief USB MIDI Host backend (polling + queues + stats).
 */

#include "midi_host.h"
#include "midi.h"
#include "usb_host.h"
#include "usbh_midi.h"

extern USBH_HandleTypeDef hUsbHostHS;

#define MIDI_HOST_TX_QUEUE_LEN 128U
#define MIDI_HOST_RX_QUEUE_LEN 128U
#define MIDI_HOST_MAX_BURST    16U
#define MIDI_HOST_LOG_INTERVAL_MS 1000U
#define MIDI_HOST_RX_WATCHDOG_MS  2000U

typedef struct {
  uint8_t bytes[USBH_MIDI_PACKET_SIZE];
} midi_host_packet_t;

static midi_host_packet_t midi_host_rx_queue[MIDI_HOST_RX_QUEUE_LEN];
static midi_host_packet_t midi_host_tx_queue[MIDI_HOST_TX_QUEUE_LEN];

static volatile uint16_t midi_host_rx_head = 0U;
static volatile uint16_t midi_host_rx_tail = 0U;
static volatile uint16_t midi_host_rx_count = 0U;

static volatile uint16_t midi_host_tx_head = 0U;
static volatile uint16_t midi_host_tx_tail = 0U;
static volatile uint16_t midi_host_tx_count = 0U;

midi_host_stats_t midi_host_stats = {0};
static bool midi_host_last_ready = false;
static uint32_t midi_host_last_rx_tick = 0U;
static uint32_t midi_host_last_no_rx_warning = 0U;
static uint32_t midi_host_last_ok_log = 0U;
static uint32_t midi_host_last_busy_log = 0U;
static uint32_t midi_host_last_fail_log = 0U;

static inline uint32_t midi_host_enter_critical(void) {
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static inline void midi_host_exit_critical(uint32_t primask) {
  __set_PRIMASK(primask);
}

static bool midi_host_rx_queue_push(const uint8_t packet[USBH_MIDI_PACKET_SIZE]) {
  uint32_t primask = midi_host_enter_critical();
  if (midi_host_rx_count >= MIDI_HOST_RX_QUEUE_LEN) {
    midi_host_exit_critical(primask);
    return false;
  }

  midi_host_rx_queue[midi_host_rx_head].bytes[0] = packet[0];
  midi_host_rx_queue[midi_host_rx_head].bytes[1] = packet[1];
  midi_host_rx_queue[midi_host_rx_head].bytes[2] = packet[2];
  midi_host_rx_queue[midi_host_rx_head].bytes[3] = packet[3];

  midi_host_rx_head = (uint16_t)((midi_host_rx_head + 1U) % MIDI_HOST_RX_QUEUE_LEN);
  midi_host_rx_count++;
  if (midi_host_rx_count > midi_host_stats.high_water_rx) {
    midi_host_stats.high_water_rx = midi_host_rx_count;
  }
  midi_host_exit_critical(primask);
  return true;
}

static bool midi_host_rx_queue_pop(midi_host_packet_t *out) {
  uint32_t primask = midi_host_enter_critical();
  if (midi_host_rx_count == 0U) {
    midi_host_exit_critical(primask);
    return false;
  }

  *out = midi_host_rx_queue[midi_host_rx_tail];
  midi_host_rx_tail = (uint16_t)((midi_host_rx_tail + 1U) % MIDI_HOST_RX_QUEUE_LEN);
  midi_host_rx_count--;
  midi_host_exit_critical(primask);
  return true;
}

static bool midi_host_tx_queue_push(const uint8_t packet[USBH_MIDI_PACKET_SIZE]) {
  uint32_t primask = midi_host_enter_critical();
  if (midi_host_tx_count >= MIDI_HOST_TX_QUEUE_LEN) {
    midi_host_exit_critical(primask);
    return false;
  }

  midi_host_tx_queue[midi_host_tx_head].bytes[0] = packet[0];
  midi_host_tx_queue[midi_host_tx_head].bytes[1] = packet[1];
  midi_host_tx_queue[midi_host_tx_head].bytes[2] = packet[2];
  midi_host_tx_queue[midi_host_tx_head].bytes[3] = packet[3];

  midi_host_tx_head = (uint16_t)((midi_host_tx_head + 1U) % MIDI_HOST_TX_QUEUE_LEN);
  midi_host_tx_count++;
  if (midi_host_tx_count > midi_host_stats.high_water_tx) {
    midi_host_stats.high_water_tx = midi_host_tx_count;
  }
  midi_host_exit_critical(primask);
  return true;
}

static bool midi_host_tx_queue_peek(midi_host_packet_t *out) {
  uint32_t primask = midi_host_enter_critical();
  if (midi_host_tx_count == 0U) {
    midi_host_exit_critical(primask);
    return false;
  }

  *out = midi_host_tx_queue[midi_host_tx_tail];
  midi_host_exit_critical(primask);
  return true;
}

static bool midi_host_tx_queue_pop(midi_host_packet_t *out) {
  uint32_t primask = midi_host_enter_critical();
  if (midi_host_tx_count == 0U) {
    midi_host_exit_critical(primask);
    return false;
  }

  *out = midi_host_tx_queue[midi_host_tx_tail];
  midi_host_tx_tail = (uint16_t)((midi_host_tx_tail + 1U) % MIDI_HOST_TX_QUEUE_LEN);
  midi_host_tx_count--;
  midi_host_exit_critical(primask);
  return true;
}

static void midi_host_reset_queues(void) {
  uint32_t primask = midi_host_enter_critical();
  midi_host_rx_head = 0U;
  midi_host_rx_tail = 0U;
  midi_host_rx_count = 0U;
  midi_host_tx_head = 0U;
  midi_host_tx_tail = 0U;
  midi_host_tx_count = 0U;
  midi_host_exit_critical(primask);
}

static bool midi_host_decode_packet(const uint8_t packet[USBH_MIDI_PACKET_SIZE], midi_msg_t *out) {
  if (out == NULL) {
    return false;
  }

  const uint8_t cin = (uint8_t)(packet[0] & 0x0FU);

  switch (cin) {
    case 0x08: /* Note Off */
    case 0x09: /* Note On */
    case 0x0A: /* Polyphonic Key Pressure */
    case 0x0B: /* Control Change */
    case 0x0E: /* Pitch Bend */
      out->len = 3U;
      out->data[0] = packet[1];
      out->data[1] = packet[2];
      out->data[2] = packet[3];
      return true;

    case 0x0C: /* Program Change */
    case 0x0D: /* Channel Pressure */
      out->len = 2U;
      out->data[0] = packet[1];
      out->data[1] = packet[2];
      return true;

    case 0x02: /* System Common 2 bytes (MTC Quarter Frame, Song Select) */
      out->len = 2U;
      out->data[0] = packet[1];
      out->data[1] = packet[2];
      return true;

    case 0x03: /* System Common 3 bytes (Song Position Pointer) */
      out->len = 3U;
      out->data[0] = packet[1];
      out->data[1] = packet[2];
      out->data[2] = packet[3];
      return true;

    case 0x0F: /* Single-byte real-time */
      switch (packet[1]) {
        case 0xF8: /* Clock */
        case 0xFA: /* Start */
        case 0xFB: /* Continue */
        case 0xFC: /* Stop */
        case 0xFE: /* Active Sensing */
        case 0xFF: /* System Reset */
          out->len = 1U;
          out->data[0] = packet[1];
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

static bool midi_host_encode_packet(const uint8_t *msg, size_t len,
                                    uint8_t packet[USBH_MIDI_PACKET_SIZE]) {
  if ((msg == NULL) || (len == 0U) || (len > 3U)) {
    return false;
  }

  uint8_t status = msg[0];
  uint8_t cin = 0U;
  size_t expected = 0U;

  if (status < 0x80U) {
    return false;
  }

  if (status >= 0xF8U) {
    cin = 0xFU;
    expected = 1U;
  } else if ((status & 0xF0U) == 0x80U) {
    cin = 0x8U;
    expected = 3U;
  } else if ((status & 0xF0U) == 0x90U) {
    cin = 0x9U;
    expected = 3U;
  } else if ((status & 0xF0U) == 0xA0U) {
    cin = 0xAU;
    expected = 3U;
  } else if ((status & 0xF0U) == 0xB0U) {
    cin = 0xBU;
    expected = 3U;
  } else if ((status & 0xF0U) == 0xC0U) {
    cin = 0xCU;
    expected = 2U;
  } else if ((status & 0xF0U) == 0xD0U) {
    cin = 0xDU;
    expected = 2U;
  } else if ((status & 0xF0U) == 0xE0U) {
    cin = 0xEU;
    expected = 3U;
  } else if (status == 0xF1U) {
    cin = 0x2U;
    expected = 2U;
  } else if (status == 0xF2U) {
    cin = 0x3U;
    expected = 3U;
  } else if (status == 0xF3U) {
    cin = 0x2U;
    expected = 2U;
  } else if (status == 0xF6U) {
    cin = 0x5U;
    expected = 1U;
  } else {
    return false;
  }

  if (len < expected) {
    return false;
  }

  packet[0] = (uint8_t)((MIDI_USB_CABLE << 4U) | (cin & 0x0FU));
  packet[1] = msg[0];
  packet[2] = (len > 1U) ? msg[1] : 0U;
  packet[3] = (len > 2U) ? msg[2] : 0U;

  return true;
}

void midi_host_poll(void) {
  uint32_t processed = 0U;
  uint32_t now = HAL_GetTick();
  bool ready = USBH_MIDI_IsReady(&hUsbHostHS);

  if (ready != midi_host_last_ready) {
    USBH_UsrLog("midi_host: USBH_MIDI_IsReady -> %u", (unsigned int)ready);
    midi_host_last_ready = ready;
    midi_host_last_rx_tick = now;
    midi_host_last_no_rx_warning = now;
  }

  /* Do not cache ready: the USB stack can deinit between stages after refactor. */
  if (ready) {
    uint8_t packet_bytes[USBH_MIDI_PACKET_SIZE];
    while (true) {
      USBH_StatusTypeDef status = USBH_MIDI_ReadPacket(&hUsbHostHS, packet_bytes);
      if ((status == USBH_OK) && ((now - midi_host_last_ok_log) >= MIDI_HOST_LOG_INTERVAL_MS)) {
        USBH_UsrLog("midi_host: USBH_MIDI_ReadPacket -> OK");
        midi_host_last_ok_log = now;
      } else if ((status == USBH_BUSY) && ((now - midi_host_last_busy_log) >= MIDI_HOST_LOG_INTERVAL_MS)) {
        USBH_UsrLog("midi_host: USBH_MIDI_ReadPacket -> BUSY");
        midi_host_last_busy_log = now;
      } else if ((status == USBH_FAIL) && ((now - midi_host_last_fail_log) >= MIDI_HOST_LOG_INTERVAL_MS)) {
        USBH_UsrLog("midi_host: USBH_MIDI_ReadPacket -> FAIL");
        midi_host_last_fail_log = now;
      }
      if (status != USBH_OK) {
        break;
      }
      if (!midi_host_rx_queue_push(packet_bytes)) {
        midi_host_stats.rx_drops++;
      } else {
        midi_host_stats.rx_packets++;
        midi_host_last_rx_tick = now;
      }
    }
  }

  while (processed < MIDI_HOST_MAX_BURST) {
    midi_host_packet_t packet;
    midi_msg_t msg;

    if (!midi_host_rx_queue_pop(&packet)) {
      break;
    }

    if (midi_host_decode_packet(packet.bytes, &msg)) {
      midi_internal_receive(msg.data, msg.len);
    }
    processed++;
  }

  if (!USBH_MIDI_IsReady(&hUsbHostHS)) {
    return;
  }

  processed = 0U;
  while (processed < MIDI_HOST_MAX_BURST) {
    midi_host_packet_t packet;
    if (!midi_host_tx_queue_peek(&packet)) {
      break;
    }

    if (!USBH_MIDI_IsReady(&hUsbHostHS)) {
      break;
    }

    USBH_StatusTypeDef status = USBH_MIDI_Transmit(&hUsbHostHS, packet.bytes);
    if (status == USBH_OK) {
      midi_host_tx_queue_pop(&packet);
      midi_host_stats.tx_packets++;
      processed++;
    } else if (status == USBH_BUSY) {
      midi_host_stats.tx_busy++;
      break;
    } else {
      midi_host_tx_queue_pop(&packet);
      midi_host_stats.tx_drops++;
    }
  }

  if (USBH_MIDI_IsReady(&hUsbHostHS)) {
    if ((now - midi_host_last_rx_tick) >= MIDI_HOST_RX_WATCHDOG_MS) {
      if ((now - midi_host_last_no_rx_warning) >= MIDI_HOST_RX_WATCHDOG_MS) {
        USBH_UsrLog("WARNING: MIDI Host alive but no RX traffic");
        midi_host_last_no_rx_warning = now;
      }
    }
  }
}

bool midi_host_send(const uint8_t *msg, size_t len) {
  uint8_t packet[USBH_MIDI_PACKET_SIZE];

  if (!midi_host_encode_packet(msg, len, packet)) {
    midi_host_stats.tx_drops++;
    return false;
  }

  if (!midi_host_tx_queue_push(packet)) {
    midi_host_stats.tx_drops++;
    return false;
  }

  if (!USBH_MIDI_IsReady(&hUsbHostHS)) {
    midi_host_stats.tx_not_ready++;
  }

  return true;
}

bool midi_host_is_connected(void) {
  return USBH_MIDI_IsReady(&hUsbHostHS);
}

void midi_host_stats_reset(void) {
  midi_host_reset_queues();
  midi_host_stats = (midi_host_stats_t){0};
}

void midi_host_on_disconnect(void) {
  midi_host_reset_queues();
  midi_host_stats.rx_packets = 0U;
  midi_host_stats.rx_drops = 0U;
  midi_host_stats.tx_packets = 0U;
  midi_host_stats.tx_drops = 0U;
  midi_host_stats.tx_busy = 0U;
  midi_host_stats.tx_not_ready = 0U;
  midi_host_stats.high_water_rx = 0U;
  midi_host_stats.high_water_tx = 0U;
  midi_host_stats.disconnect_count++;
}
