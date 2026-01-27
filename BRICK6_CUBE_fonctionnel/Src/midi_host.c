/**
 * @file midi_host.c
 * @brief USB MIDI Host bridge (USB Host -> moteur interne)
 */

#include "midi_host.h"
#include "midi.h"
#include "usb_host.h"
#include "usbh_midi.h"

extern USBH_HandleTypeDef hUsbHostHS;

static uint8_t midi_host_cin_to_length(uint8_t cin)
{
  switch (cin)
  {
  case 0x2U: /* System Common (2 bytes) */
  case 0xCU: /* Program Change */
  case 0xDU: /* Channel Pressure */
    return 2U;
  case 0x3U: /* System Common (3 bytes) */
  case 0x8U: /* Note Off */
  case 0x9U: /* Note On */
  case 0xAU: /* Poly Key Pressure */
  case 0xBU: /* Control Change */
  case 0xEU: /* Pitch Bend */
    return 3U;
  case 0x5U: /* Single-byte System Common */
  case 0xFU: /* Single-byte System Realtime */
    return 1U;
  default:
    return 0U;
  }
}

void midi_host_poll(void)
{
  if (!USBH_MIDI_IsReady(&hUsbHostHS))
  {
    return;
  }

  while (1)
  {
    uint8_t packet[USBH_MIDI_PACKET_SIZE];
    if (USBH_MIDI_ReadPacket(&hUsbHostHS, packet) != USBH_OK)
    {
      break;
    }

    uint8_t cin = packet[0] & 0x0FU;
    uint8_t length = midi_host_cin_to_length(cin);
    if (length == 0U)
    {
      continue;
    }

    midi_internal_receive(&packet[1], length);
    midi_send_raw(MIDI_DEST_USB, &packet[1], length);
  }
}

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

  uint8_t packet[USBH_MIDI_PACKET_SIZE];
  if (!midi_host_encode_packet(msg, len, packet))
  {
    return false;
  }

  return (USBH_MIDI_Transmit(&hUsbHostHS, packet) == USBH_OK);
}
