/**
 * @file midi_host.c
 * @brief Pont MIDI USB Host vers l'API MIDI interne.
 *
 * Ce module lit/écrit des paquets MIDI via la pile USB Host et les
 * convertit en messages MIDI internes pour le moteur et l'UI.
 *
 * Rôle dans le système:
 * - Bridge entre USB Host et l'API midi.c.
 * - Applique un budget pour ne pas monopoliser la boucle principale.
 *
 * Contraintes temps réel:
 * - Critique audio: non.
 * - Tasklet: oui (poll en boucle principale).
 * - IRQ: non (pas d'accès en ISR).
 * - Borné: oui (midi_host_poll_bounded).
 *
 * Architecture:
 * - Appelé par: main loop (midi_host_poll[_bounded]).
 * - Appelle: USBH_MIDI_ReadPacket/Transmit, midi_internal_receive, midi_send_raw.
 *
 * Règles:
 * - Pas de malloc.
 * - Ne pas bloquer la boucle principale.
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

static uint8_t midi_host_cin_to_length(uint8_t cin)
{
  static const uint8_t cin_len[16] = {
    0U, /* 0x0 */
    0U, /* 0x1 */
    2U, /* 0x2: System Common (2 bytes) */
    3U, /* 0x3: System Common (3 bytes) */
    3U, /* 0x4: SysEx start/continue */
    1U, /* 0x5: SysEx ends with 1 byte */
    2U, /* 0x6: SysEx ends with 2 bytes */
    3U, /* 0x7: SysEx ends with 3 bytes */
    3U, /* 0x8: Note Off */
    3U, /* 0x9: Note On */
    3U, /* 0xA: Poly Key Pressure */
    3U, /* 0xB: Control Change */
    2U, /* 0xC: Program Change */
    2U, /* 0xD: Channel Pressure */
    3U, /* 0xE: Pitch Bend */
    1U  /* 0xF: Single-byte System Realtime */
  };

  return cin_len[cin & 0x0FU];
}

void midi_host_poll(void)
{
#if BRICK6_REFACTOR_STEP_6
  midi_host_poll_bounded(MIDI_BUDGET_MSGS);
  return;
#endif

  if (!USBH_MIDI_IsReady(&hUsbHostHS))
  {
    return;
  }

  while (1)
  {
    if (USBH_MIDI_ReadPacket(&hUsbHostHS, midi_host_rx_packet) != USBH_OK)
    {
      break;
    }

    uint8_t cin = midi_host_rx_packet[0] & 0x0FU;
    uint8_t length = midi_host_cin_to_length(cin);
    if (length == 0U)
    {
      continue;
    }

    midi_internal_receive(&midi_host_rx_packet[1], length);
    midi_send_raw(MIDI_DEST_USB, &midi_host_rx_packet[1], length);
  }
}

#if BRICK6_REFACTOR_STEP_6
void midi_host_poll_bounded(uint32_t max_msgs)
{
  if (!USBH_MIDI_IsReady(&hUsbHostHS))
  {
    return;
  }

  uint32_t n = 0U;
  for (; n < max_msgs; n++)
  {
    if (USBH_MIDI_ReadPacket(&hUsbHostHS, midi_host_rx_packet) != USBH_OK)
    {
      break;
    }

    uint8_t cin = midi_host_rx_packet[0] & 0x0FU;
    uint8_t length = midi_host_cin_to_length(cin);
    if (length == 0U)
    {
      continue;
    }

    midi_internal_receive(&midi_host_rx_packet[1], length);
    midi_send_raw(MIDI_DEST_USB, &midi_host_rx_packet[1], length);
  }

  if ((max_msgs > 0U) && (n >= max_msgs))
  {
    midi_budget_hit_count++;
  }
}
#endif

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
    cin = 0xFU;
    expected = 1U;
  }
  else if ((status & 0xF0U) == 0x80U)
  {
    cin = 0x8U;
    expected = 3U;
  }
  else if ((status & 0xF0U) == 0x90U)
  {
    cin = 0x9U;
    expected = 3U;
  }
  else if ((status & 0xF0U) == 0xA0U)
  {
    cin = 0xAU;
    expected = 3U;
  }
  else if ((status & 0xF0U) == 0xB0U)
  {
    cin = 0xBU;
    expected = 3U;
  }
  else if ((status & 0xF0U) == 0xC0U)
  {
    cin = 0xCU;
    expected = 2U;
  }
  else if ((status & 0xF0U) == 0xD0U)
  {
    cin = 0xDU;
    expected = 2U;
  }
  else if ((status & 0xF0U) == 0xE0U)
  {
    cin = 0xEU;
    expected = 3U;
  }
  else if (status == 0xF1U)
  {
    cin = 0x2U;
    expected = 2U;
  }
  else if (status == 0xF2U)
  {
    cin = 0x3U;
    expected = 3U;
  }
  else if (status == 0xF3U)
  {
    cin = 0x2U;
    expected = 2U;
  }
  else if (status == 0xF6U)
  {
    cin = 0x5U;
    expected = 1U;
  }
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
