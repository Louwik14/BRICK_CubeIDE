/**
 * @file midi_host.h
 * @brief USB MIDI Host bridge (USB Host -> moteur interne)
 */

#ifndef MIDI_HOST_H
#define MIDI_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  volatile uint32_t calls;
  volatile uint32_t last_cycles;
  volatile uint32_t max_cycles;
  volatile uint32_t last_messages;
  volatile uint32_t max_messages;
  volatile uint32_t cap_hit_count;
  volatile uint32_t not_ready_count;
} midi_host_poll_metrics_t;

extern volatile midi_host_poll_metrics_t g_midi_host_poll_metrics;

void midi_host_poll(void);
void midi_host_poll_bounded(uint32_t max_msgs);

bool midi_host_send(const uint8_t *msg, size_t len);

#endif /* MIDI_HOST_H */
