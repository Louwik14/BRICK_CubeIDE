#pragma once

#include <stdint.h>

#include "Mod/mod_destination_catalog.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MODULATION_PUBLICATION_SLOT_COUNT 4U
#define MODULATION_PUBLICATION_LFO_COUNT 3U
#define MODULATION_PUBLICATION_ROUTE_COUNT 8U

typedef struct
{
    float rate;
    float phase;
    uint8_t shape;
    uint8_t trig;
    uint16_t reserved;
} modulation_lfo_publication_t;

typedef struct
{
    float attack;
    float decay;
    float sustain;
    float release;
    uint8_t retrigger_hard;
    uint8_t reserved[3];
} modulation_env3_publication_t;

typedef struct
{
    uint16_t destination;
    uint8_t flags;
    uint8_t reserved;
    float base_value;
    float min_value;
    float max_value;
} modulation_destination_publication_t;

typedef struct
{
    uint8_t source;
    uint8_t destination_index;
    uint8_t flags;
    uint8_t reserved;
    float scale;
} modulation_route_publication_t;

typedef struct
{
    uint32_t generation;
    modulation_lfo_publication_t lfo[MODULATION_PUBLICATION_LFO_COUNT];
    modulation_env3_publication_t env3;
    uint8_t multi_source[2][2];
    uint8_t slew_source[2];
    float slew_amount[2];
    uint16_t source_mask;
    uint8_t poly_source_mask;
    uint8_t destination_count;
    uint8_t route_count;
    modulation_destination_publication_t destinations[MODULATION_PUBLICATION_ROUTE_COUNT];
    modulation_route_publication_t routes[MODULATION_PUBLICATION_ROUTE_COUNT];
} modulation_publication_t;

_Static_assert(sizeof(modulation_publication_t) == 276U,
               "modulation publication ABI changed");

#ifdef __cplusplus
}
#endif
