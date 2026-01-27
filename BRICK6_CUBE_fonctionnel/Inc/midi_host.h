/**
 * @file midi_host.h
 * @brief USB MIDI Host bridge (USB Host -> moteur interne)
 */

#ifndef MIDI_HOST_H
#define MIDI_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void midi_host_poll(void);

bool midi_host_send(const uint8_t *msg, size_t len);

#endif /* MIDI_HOST_H */
