/**
 * @file midi_host.h
 * @brief USB MIDI Host bridge (USB Host -> moteur interne)
 */

#ifndef MIDI_HOST_H
#define MIDI_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "brick6_refactor.h"

void midi_host_poll(void);
void midi_host_poll_bounded(uint32_t max_msgs);

bool midi_host_send(const uint8_t *msg, size_t len);

#endif /* MIDI_HOST_H */
