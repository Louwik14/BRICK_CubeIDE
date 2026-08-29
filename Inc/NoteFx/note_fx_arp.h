#ifndef NOTE_FX_ARP_H
#define NOTE_FX_ARP_H

#include <stdint.h>

#define NOTE_FX_ARP_MAX_SOURCES 16U

typedef enum { NOTE_FX_ARP_ORDER = 0, NOTE_FX_ARP_UP, NOTE_FX_ARP_DOWN,
               NOTE_FX_ARP_UP_DOWN, NOTE_FX_ARP_RANDOM } note_fx_arp_style_t;

typedef struct {
    uint8_t note[NOTE_FX_ARP_MAX_SOURCES];
    uint8_t velocity[NOTE_FX_ARP_MAX_SOURCES];
    uint32_t source_token[NOTE_FX_ARP_MAX_SOURCES];
    uint32_t causal_source_token[NOTE_FX_ARP_MAX_SOURCES];
    uint32_t source_generation[NOTE_FX_ARP_MAX_SOURCES];
    uint8_t count;
    uint8_t cursor;
    uint8_t last_source_note;
    uint8_t reserved;
    uint32_t last_source_token;
    uint32_t last_causal_source_token;
    uint32_t last_source_generation;
    int8_t direction;
    uint32_t phase;
    uint32_t random_state;
} note_fx_arp_t;

void note_fx_arp_init(note_fx_arp_t *arp, uint32_t seed);
uint8_t note_fx_arp_note_on(note_fx_arp_t *arp, uint8_t note, uint8_t velocity,
                            uint32_t source_token, uint32_t source_generation,
                            uint32_t causal_source_token);
uint8_t note_fx_arp_note_off(note_fx_arp_t *arp, uint32_t source_token,
                             uint32_t source_generation);
uint8_t note_fx_arp_next(note_fx_arp_t *arp, note_fx_arp_style_t style,
                         uint8_t range, uint8_t *note, uint8_t *velocity);

#endif
