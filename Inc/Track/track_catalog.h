#ifndef TRACK_CATALOG_H
#define TRACK_CATALOG_H

#include <stdbool.h>
#include <stdint.h>

#include "Track/track_types.h"

#ifdef __cplusplus
extern "C" {
#endif

bool track_catalog_family_is_engine(track_family_t family);
bool track_catalog_type_is_valid_for_family(track_family_t family, track_type_t type);
bool track_catalog_type_is_available(
    uint8_t track, track_family_t family, track_type_t type,
    const track_config_t track_configs[BRICK_ENTITY_CAPACITY]);
bool track_catalog_family_is_available(
    uint8_t track, track_family_t family,
    const track_config_t track_configs[BRICK_ENTITY_CAPACITY]);
uint8_t track_catalog_type_count_for_family(
    track_family_t family, uint8_t track,
    const track_config_t track_configs[BRICK_ENTITY_CAPACITY]);
track_type_t track_catalog_type_at(track_family_t family, uint8_t index);
track_type_t track_catalog_first_available_type(
    track_family_t family, uint8_t track,
    const track_config_t track_configs[BRICK_ENTITY_CAPACITY]);
track_type_t track_catalog_default_type_for_family(track_family_t family);
const char *track_catalog_family_short_name(track_family_t family);

#ifdef __cplusplus
}
#endif

#endif /* TRACK_CATALOG_H */
