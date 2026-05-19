/**
 * @file midi_host.c
 * @brief Pont MIDI USB Host vers l'API MIDI interne.
 *
 * Ce module lit/écrit des paquets MIDI via la pile USB Host et les
 * convertit en messages MIDI internes pour le moteur et l'UI.
 *
 * Rôle dans le système:
 * - Bridge entre USB Host et l'API midi.c.
 * - Traitement borné pour ne pas monopoliser la boucle principale.
 *
 * Contraintes temps réel:
 * - Critique audio: non.
 * - Tasklet: oui (poll en boucle principale).
 * - IRQ: non (pas d'accès en ISR).
 * - Borné: oui (midi_host_poll_bounded avec budget fixe).
 *
 * Architecture:
 * - Appelé par: main loop (midi_host_poll).
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

/**
 * @brief Point d'entrée midi_host_poll.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_host_poll.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_host_poll(void)
{
  midi_host_poll_bounded(8); // budget fixe simple
}

/**
 * @brief Point d'entrée midi_host_poll_bounded.
 *
 * Rôle:
 * - Exécuter le traitement associé à midi_host_poll_bounded.
 *
 * @param max_msgs Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void midi_host_poll_bounded(uint32_t max_msgs)
{
  uint32_t n = 0U;

#if BRICK6_ENABLE_DIAGNOSTICS
  brick6_midi_host_poll_count++;
#endif

  if (!USBH_MIDI_IsReady(&hUsbHostHS))
  {
    return;
  }

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

    midi_internal_receive_with_source(&midi_host_rx_packet[1], length, SEQ_CLOCK_SRC_EXTERNAL_USB);
    midi_send_raw(MIDI_DEST_USB, &midi_host_rx_packet[1], length);
  }

#if BRICK6_ENABLE_DIAGNOSTICS
  if ((max_msgs > 0U) && (n >= max_msgs))
  {
    midi_budget_hit_count++;
  }
#endif
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
