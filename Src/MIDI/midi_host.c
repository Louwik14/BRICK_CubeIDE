#include "midi_host.h"

#include "stm32h7xx.h"
#include "App/control_rt_wakeup.h"
#include "App/usb_service_wakeup.h"
#include "live_clock_control.h"
#include "midi.h"
#include "usb_host.h"
#include <host/hcd.h>
#include <host/usbh.h>
#include <class/midi/midi_host.h>

#define MIDI_HOST_PACKET_SIZE       4U
#define MIDI_HOST_EVENT_QUEUE_LENGTH 32U
#define MIDI_HOST_TX_QUEUE_LENGTH    32U

typedef struct
{
  uint8_t packet[MIDI_HOST_PACKET_SIZE];
  uint32_t tim5_tick;
  uint32_t ingress_serial;
} midi_host_event_t;

static uint8_t midi_host_rx_packet[MIDI_HOST_PACKET_SIZE];
static uint8_t midi_host_tx_packet[MIDI_HOST_PACKET_SIZE];
static midi_host_event_t midi_host_event_queue[MIDI_HOST_EVENT_QUEUE_LENGTH];
static uint8_t midi_host_tx_queue[MIDI_HOST_TX_QUEUE_LENGTH][MIDI_HOST_PACKET_SIZE];
static volatile uint16_t midi_host_event_head;
static volatile uint16_t midi_host_event_tail;
static volatile uint16_t midi_host_tx_head;
static volatile uint16_t midi_host_tx_tail;
static volatile uint8_t midi_host_discard_requested;
static volatile uint8_t midi_host_tx_waiting_for_hcd;
static volatile uint8_t midi_host_rx_work_pending;
static volatile uint8_t midi_host_rx_waiting_for_control;
static uint32_t midi_host_ingress_serial;

static uint8_t midi_host_cin_to_length(uint8_t cin)
{
  static const uint8_t cin_len[16] = {
    0U, 0U, 2U, 3U, 3U, 1U, 2U, 3U,
    3U, 3U, 3U, 3U, 2U, 2U, 3U, 1U
  };
  return cin_len[cin & 0x0FU];
}

void midi_host_rx_discard_pending(void)
{
  /* USB host transport is owned and drained by USB_SERVICE. */
  midi_host_discard_requested = 1U;
  usb_service_wakeup(USB_SERVICE_WAKE_WORK);
}

void midi_host_transport_reset(void)
{
  midi_host_event_tail = midi_host_event_head;
  midi_host_tx_tail = midi_host_tx_head;
  midi_host_discard_requested = 0U;
  midi_host_tx_waiting_for_hcd = 0U;
  midi_host_rx_work_pending = 0U;
  midi_host_rx_waiting_for_control = 0U;
}

static bool midi_host_event_queue_has_room(void)
{
  const uint16_t head = midi_host_event_head;
  const uint16_t next = (uint16_t)((head + 1U) % MIDI_HOST_EVENT_QUEUE_LENGTH);
  return next != midi_host_event_tail;
}

uint8_t midi_host_transport_work_pending(void)
{
  if (midi_host_discard_requested != 0U) {
    return 1U;
  }
  if (usb_host_is_ready() == 0U) {
    return 0U;
  }
  if ((midi_host_tx_head != midi_host_tx_tail)
      && (midi_host_tx_waiting_for_hcd == 0U)) {
    return 1U;
  }
  if ((midi_host_rx_work_pending != 0U)
      && midi_host_event_queue_has_room()) {
    return 1U;
  }
  return 0U;
}

void midi_host_poll_bounded(uint32_t max_msgs)
{
  midi_host_transport_poll_bounded(max_msgs);
  midi_host_control_poll_bounded(max_msgs);
}

static bool midi_host_event_push(const uint8_t *packet,
                                 uint32_t tim5_tick,
                                 uint32_t ingress_serial)
{
  const uint16_t head = midi_host_event_head;
  const uint16_t next = (uint16_t)((head + 1U) % MIDI_HOST_EVENT_QUEUE_LENGTH);
  if (next == midi_host_event_tail) {
    return false;
  }

  for (uint32_t i = 0U; i < MIDI_HOST_PACKET_SIZE; ++i) {
    midi_host_event_queue[head].packet[i] = packet[i];
  }
  midi_host_event_queue[head].tim5_tick = tim5_tick;
  midi_host_event_queue[head].ingress_serial = ingress_serial;
  __DMB();
  midi_host_event_head = next;
  return true;
}

static bool midi_host_event_pop(midi_host_event_t *event)
{
  const uint16_t tail = midi_host_event_tail;
  if (tail == midi_host_event_head) {
    return false;
  }

  __DMB();
  *event = midi_host_event_queue[tail];
  midi_host_event_tail =
      (uint16_t)((tail + 1U) % MIDI_HOST_EVENT_QUEUE_LENGTH);
  return true;
}

static bool midi_host_tx_push(const uint8_t packet[MIDI_HOST_PACKET_SIZE])
{
  const uint16_t head = midi_host_tx_head;
  const uint16_t next = (uint16_t)((head + 1U) % MIDI_HOST_TX_QUEUE_LENGTH);
  if (next == midi_host_tx_tail) {
    return false;
  }

  for (uint32_t i = 0U; i < MIDI_HOST_PACKET_SIZE; ++i) {
    midi_host_tx_queue[head][i] = packet[i];
  }
  __DMB();
  midi_host_tx_head = next;
  return true;
}

static bool midi_host_tx_peek(uint8_t packet[MIDI_HOST_PACKET_SIZE])
{
  const uint16_t tail = midi_host_tx_tail;
  if (tail == midi_host_tx_head) {
    return false;
  }

  __DMB();
  for (uint32_t i = 0U; i < MIDI_HOST_PACKET_SIZE; ++i) {
    packet[i] = midi_host_tx_queue[tail][i];
  }
  return true;
}

static void midi_host_tx_drop(void)
{
  midi_host_tx_tail =
      (uint16_t)((midi_host_tx_tail + 1U) % MIDI_HOST_TX_QUEUE_LENGTH);
}

void midi_host_transport_poll_bounded(uint32_t max_msgs)
{
  if (midi_host_discard_requested != 0U) {
    if (usb_host_is_ready() != 0U) {
      for (uint32_t n = 0U; n < max_msgs; ++n) {
        if (!tuh_midi_packet_read(0U, midi_host_rx_packet)) {
          break;
        }
      }
      if (tuh_midi_read_available(0U) != 0U) {
        midi_host_rx_work_pending = 1U;
        usb_service_wakeup(USB_SERVICE_WAKE_WORK);
        return;
      }
    }
    midi_host_event_tail = midi_host_event_head;
    midi_host_discard_requested = 0U;
    midi_host_rx_work_pending = 0U;
    midi_host_rx_waiting_for_control = 0U;
    return;
  }

  if (usb_host_is_ready() == 0U) {
    return;
  }

  for (uint32_t n = 0U; n < max_msgs; ++n) {
    if (!midi_host_event_queue_has_room()) {
      midi_host_rx_work_pending = 0U;
      midi_host_rx_waiting_for_control = 1U;
      break;
    }
    if (!tuh_midi_packet_read(0U, midi_host_rx_packet)) {
      break;
    }

    uint32_t ingress_serial = midi_host_ingress_serial + 1U;
    if (ingress_serial == 0U) {
      ingress_serial = 1U;
    }
    midi_host_ingress_serial = ingress_serial;
    if (!midi_host_event_push(midi_host_rx_packet,
                              live_clock_capture_tick(),
                              ingress_serial)) {
      break;
    }
    control_rt_wakeup(CONTROL_RT_WAKE_MIDI);
  }

  if (midi_host_tx_waiting_for_hcd == 0U) {
    for (uint32_t n = 0U; n < max_msgs; ++n) {
      if (!midi_host_tx_peek(midi_host_tx_packet)) {
        break;
      }
      if (!tuh_midi_packet_write(0U, midi_host_tx_packet)) {
        break;
      }
      midi_host_tx_drop();
    }
    const uint32_t flushed = tuh_midi_write_flush(0U);
    if ((midi_host_tx_head != midi_host_tx_tail) && (flushed == 0U)) {
      midi_host_tx_waiting_for_hcd = 1U;
    }
  }

  if (tuh_midi_read_available(0U) != 0U) {
    if (midi_host_event_queue_has_room()) {
      midi_host_rx_work_pending = 1U;
      midi_host_rx_waiting_for_control = 0U;
      usb_service_wakeup(USB_SERVICE_WAKE_WORK);
    } else {
      midi_host_rx_work_pending = 0U;
      midi_host_rx_waiting_for_control = 1U;
    }
  } else {
    midi_host_rx_work_pending = 0U;
  }
}

void midi_host_control_poll_bounded(uint32_t max_msgs)
{
  if (midi_host_discard_requested != 0U) {
    return;
  }

  for (uint32_t n = 0U; n < max_msgs; ++n) {
    midi_host_event_t event;
    if (!midi_host_event_pop(&event)) {
      break;
    }

    const uint8_t length = midi_host_cin_to_length(event.packet[0]);
    if (length == 0U) {
      continue;
    }

    midi_internal_receive_with_timestamp(&event.packet[1], length,
                                         SEQ_CLOCK_SRC_EXTERNAL_USB,
                                         event.tim5_tick,
                                         event.ingress_serial);
    midi_send_raw(MIDI_DEST_USB, &event.packet[1], length);
  }

  if (midi_host_control_pending_count() != 0U) {
    control_rt_wakeup(CONTROL_RT_WAKE_MIDI);
  }
  if ((midi_host_rx_waiting_for_control != 0U)
      && midi_host_event_queue_has_room()) {
    midi_host_rx_waiting_for_control = 0U;
    usb_service_wakeup(USB_SERVICE_WAKE_WORK);
  }
}

void midi_host_transport_hcd_event(uint32_t eventid)
{
  if (eventid == HCD_EVENT_XFER_COMPLETE) {
    midi_host_tx_waiting_for_hcd = 0U;
  }
}

uint16_t midi_host_control_pending_count(void)
{
  if (midi_host_discard_requested != 0U) {
    return 0U;
  }

  const uint16_t head = midi_host_event_head;
  const uint16_t tail = midi_host_event_tail;
  return (head >= tail)
      ? (uint16_t)(head - tail)
      : (uint16_t)(MIDI_HOST_EVENT_QUEUE_LENGTH + head - tail);
}

static bool midi_host_encode_packet(const uint8_t *msg, size_t len,
                                    uint8_t packet[MIDI_HOST_PACKET_SIZE])
{
  if ((msg == NULL) || (len == 0U) || (len > 3U)) {
    return false;
  }

  const uint8_t status = msg[0];
  uint8_t cin = 0U;
  size_t expected = 0U;

  if (status < 0x80U) {
    return false;
  }
  if (status >= 0xF8U) {
    cin = 0xFU; expected = 1U;
  } else if ((status & 0xF0U) == 0x80U) { cin = 0x8U; expected = 3U;
  } else if ((status & 0xF0U) == 0x90U) { cin = 0x9U; expected = 3U;
  } else if ((status & 0xF0U) == 0xA0U) { cin = 0xAU; expected = 3U;
  } else if ((status & 0xF0U) == 0xB0U) { cin = 0xBU; expected = 3U;
  } else if ((status & 0xF0U) == 0xC0U) { cin = 0xCU; expected = 2U;
  } else if ((status & 0xF0U) == 0xD0U) { cin = 0xDU; expected = 2U;
  } else if ((status & 0xF0U) == 0xE0U) { cin = 0xEU; expected = 3U;
  } else if (status == 0xF1U) { cin = 0x2U; expected = 2U;
  } else if (status == 0xF2U) { cin = 0x3U; expected = 3U;
  } else if (status == 0xF3U) { cin = 0x2U; expected = 2U;
  } else if (status == 0xF6U) { cin = 0x5U; expected = 1U;
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

bool midi_host_send(const uint8_t *msg, size_t len)
{
  if (usb_host_is_ready() == 0U) {
    return false;
  }
  if (!midi_host_encode_packet(msg, len, midi_host_tx_packet)) {
    return false;
  }
  if (!midi_host_tx_push(midi_host_tx_packet)) {
    return false;
  }

  usb_service_wakeup(USB_SERVICE_WAKE_WORK);
  return true;
}
