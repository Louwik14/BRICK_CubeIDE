/**
 * @file midi_host.h
 * @brief USB MIDI Host bridge (USB Host -> moteur interne)
 */

#ifndef MIDI_HOST_H
#define MIDI_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void midi_host_transport_poll_bounded(uint32_t max_msgs);
uint8_t midi_host_transport_work_pending(void);
void midi_host_control_poll_bounded(uint32_t max_msgs);
uint16_t midi_host_control_pending_count(void);
void midi_host_poll_bounded(uint32_t max_msgs);
void midi_host_rx_discard_pending(void);
void midi_host_transport_reset(void);
void midi_host_transport_hcd_event(uint32_t eventid);

bool midi_host_send(const uint8_t *msg, size_t len);

#endif /* MIDI_HOST_H */
