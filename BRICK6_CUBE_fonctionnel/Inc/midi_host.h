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
  volatile uint32_t rx_packets;
  volatile uint32_t rx_drops;
  volatile uint32_t tx_packets;
  volatile uint32_t tx_drops;
  volatile uint32_t tx_busy;
  volatile uint32_t high_water_rx;
  volatile uint32_t high_water_tx;
} midi_host_stats_t;

extern midi_host_stats_t midi_host_stats;

void midi_host_poll(void);

bool midi_host_send(const uint8_t *msg, size_t len);

void midi_host_stats_reset(void);

#endif /* MIDI_HOST_H */
