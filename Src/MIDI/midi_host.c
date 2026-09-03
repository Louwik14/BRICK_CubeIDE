/**
 * @file midi_host.c
 * @brief Pont MIDI USB Host vers l'API MIDI interne.
 *
 * Ce module lit/écrit des paquets MIDI via la pile USB Host et les
 * convertit en messages MIDI internes pour le moteur et l'UI.
 *
 * Rôle dans le système:
 * - Bridge entre USB Host et l'API midi.c.
 * - Traitement borné pour ne pas monopoliser les tâches applicatives.
 *
 * Contraintes temps réel:
 * - Critique audio: non.
 * - USB_SERVICE/CONTROL_RT: oui.
 * - IRQ: non (pas d'accès en ISR).
 * - Borné: oui (transport et contrôle avec budgets fixes).
 *
 * Architecture:
 * - Appelé par: USB_SERVICE et CONTROL_RT.
 * - Appelle: USBH_MIDI_ReadPacket/Transmit, midi_internal_receive, midi_send_raw.
 *
 * Règles:
 * - Pas de malloc.
 * - Ne pas bloquer les tâches applicatives.
 *
 * @note L’API publique est déclarée dans midi_host.h.
 */

#include "midi_host.h"
#include "midi.h"
#include "usb_host.h"
#include "usbh_midi.h"

extern USBH_HandleTypeDef hUsbHostHS;

static uint8_t midi_host_rx_packet[USBH_MIDI_PACKET_SIZE];
static uint8_t midi_host_tx_packet[USBH_MIDI_PACKET_SIZE];

typedef struct
{
  uint8_t packet[USBH_MIDI_PACKET_SIZE];
  uint32_t tim5_tick;
  uint32_t ingress_serial;
} midi_host_event_t;

#define MIDI_HOST_EVENT_QUEUE_LENGTH  (32U)
static midi_host_event_t midi_host_event_queue[MIDI_HOST_EVENT_QUEUE_LENGTH];
static volatile uint16_t midi_host_event_head;
static volatile uint16_t midi_host_event_tail;
static volatile uint8_t midi_host_discard_requested;

/**
 * @brief Point d'entrée midi_host_cin_to_length.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_host_cin_to_length.
 *
 * @param cin Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
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
  /* USBH is owned by USB_SERVICE; request the discard there. */
  midi_host_discard_requested = 1U;
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
  if (next == midi_host_event_tail)
  {
    return false;
  }

  midi_host_event_queue[head].packet[0] = packet[0];
  midi_host_event_queue[head].packet[1] = packet[1];
  midi_host_event_queue[head].packet[2] = packet[2];
  midi_host_event_queue[head].packet[3] = packet[3];
  midi_host_event_queue[head].tim5_tick = tim5_tick;
  midi_host_event_queue[head].ingress_serial = ingress_serial;
  __DMB();
  midi_host_event_head = next;
  return true;
}

static bool midi_host_event_pop(midi_host_event_t *event)
{
  const uint16_t tail = midi_host_event_tail;
  if (tail == midi_host_event_head)
  {
    return false;
  }

  __DMB();
  *event = midi_host_event_queue[tail];
  midi_host_event_tail =
      (uint16_t)((tail + 1U) % MIDI_HOST_EVENT_QUEUE_LENGTH);
  return true;
}

void midi_host_transport_poll_bounded(uint32_t max_msgs)
{
  if (midi_host_discard_requested != 0U)
  {
    uint8_t packet[USBH_MIDI_PACKET_SIZE];
    for (uint32_t i = 0U; i < USBH_MIDI_RX_QUEUE_LEN; ++i)
    {
      if (USBH_MIDI_ReadPacket(&hUsbHostHS, packet) != USBH_OK)
      {
        break;
      }
    }
    midi_host_event_tail = midi_host_event_head;
    midi_host_discard_requested = 0U;
    return;
  }

  if (!USBH_MIDI_IsReady(&hUsbHostHS))
  {
    return;
  }

  for (uint32_t n = 0U; n < max_msgs; ++n)
  {
    uint32_t tim5_tick = 0U;
    uint32_t ingress_serial = 0U;
    if (USBH_MIDI_ReadPacketWithTimestamp(&hUsbHostHS, midi_host_rx_packet,
                                          &tim5_tick,
                                          &ingress_serial) != USBH_OK)
    {
      break;
    }

    if (!midi_host_event_push(midi_host_rx_packet, tim5_tick, ingress_serial))
    {
      break;
    }
  }
}

void midi_host_control_poll_bounded(uint32_t max_msgs)
{
  if (midi_host_discard_requested != 0U)
  {
    return;
  }

  for (uint32_t n = 0U; n < max_msgs; ++n)
  {
    midi_host_event_t event;
    if (!midi_host_event_pop(&event))
    {
      break;
    }

    const uint8_t cin = event.packet[0] & 0x0FU;
    const uint8_t length = midi_host_cin_to_length(cin);
    if (length == 0U)
    {
      continue;
    }

    midi_internal_receive_with_timestamp(&event.packet[1], length,
                                         SEQ_CLOCK_SRC_EXTERNAL_USB,
                                         event.tim5_tick,
                                         event.ingress_serial);
    midi_send_raw(MIDI_DEST_USB, &event.packet[1], length);
  }

}

/**
 * @brief Point d'entrée midi_host_encode_packet.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_host_encode_packet.
 *
 * @param msg Paramètre d'entrée de l'API.
 * @param len Paramètre d'entrée de l'API.
 * @param packet Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static bool midi_host_encode_packet(const uint8_t *msg, size_t len,
                                    uint8_t packet[USBH_MIDI_PACKET_SIZE])
{
  if ((msg == NULL) || (len == 0U) || (len > 3U))
  {
    return false;
  }

  uint8_t status = msg[0];
  uint8_t cin = 0U;
  size_t expected = 0U;

  if (status < 0x80U)
  {
    return false;
  }

  if (status >= 0xF8U)
  {
    cin = 0xFU; expected = 1U;
  }
  else if ((status & 0xF0U) == 0x80U) { cin = 0x8U; expected = 3U; }
  else if ((status & 0xF0U) == 0x90U) { cin = 0x9U; expected = 3U; }
  else if ((status & 0xF0U) == 0xA0U) { cin = 0xAU; expected = 3U; }
  else if ((status & 0xF0U) == 0xB0U) { cin = 0xBU; expected = 3U; }
  else if ((status & 0xF0U) == 0xC0U) { cin = 0xCU; expected = 2U; }
  else if ((status & 0xF0U) == 0xD0U) { cin = 0xDU; expected = 2U; }
  else if ((status & 0xF0U) == 0xE0U) { cin = 0xEU; expected = 3U; }
  else if (status == 0xF1U) { cin = 0x2U; expected = 2U; }
  else if (status == 0xF2U) { cin = 0x3U; expected = 3U; }
  else if (status == 0xF3U) { cin = 0x2U; expected = 2U; }
  else if (status == 0xF6U) { cin = 0x5U; expected = 1U; }
  else
  {
    return false;
  }

  if (len < expected)
  {
    return false;
  }

  packet[0] = (uint8_t)((MIDI_USB_CABLE << 4U) | (cin & 0x0FU));
  packet[1] = msg[0];
  packet[2] = (len > 1U) ? msg[1] : 0U;
  packet[3] = (len > 2U) ? msg[2] : 0U;

  return true;
}

/**
 * @brief Point d'entrée midi_host_send.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_host_send.
 *
 * @param msg Paramètre d'entrée de l'API.
 * @param len Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
bool midi_host_send(const uint8_t *msg, size_t len)
{
  if (!USBH_MIDI_IsReady(&hUsbHostHS))
  {
    return false;
  }

  if (!midi_host_encode_packet(msg, len, midi_host_tx_packet))
  {
    return false;
  }

  return (USBH_MIDI_Transmit(&hUsbHostHS, midi_host_tx_packet) == USBH_OK);
}
