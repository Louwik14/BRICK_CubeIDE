#ifndef NOTE_FX_ARP_H
#define NOTE_FX_ARP_H

#include <stdint.h>

#define NOTE_FX_HELD_PITCH_CAPACITY 8U

typedef enum { NOTE_FX_ARP_ORDER = 0, NOTE_FX_ARP_UP, NOTE_FX_ARP_DOWN,
               NOTE_FX_ARP_UP_DOWN, NOTE_FX_ARP_RANDOM } note_fx_arp_style_t;

#endif
