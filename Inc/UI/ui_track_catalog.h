#ifndef UI_TRACK_CATALOG_H
#define UI_TRACK_CATALOG_H

#include <stdbool.h>
#include <stdint.h>

#include "Track/track_types.h"
#include "Platform/brick_build_config.h"

bool ui_track_catalog_family_is_engine(track_family_t family);
bool ui_track_catalog_type_is_valid_for_family(track_family_t family, track_type_t type);
bool ui_track_catalog_type_is_available(uint8_t track,
                                        track_family_t family,
                                        track_type_t type,
                                        const track_config_t track_configs[BRICK_ENTITY_CAPACITY]);
bool ui_track_catalog_family_is_available(uint8_t track,
                                          track_family_t family,
                                          const track_config_t track_configs[BRICK_ENTITY_CAPACITY]);
uint8_t ui_track_catalog_cfg_family_order_count(void);
track_family_t ui_track_catalog_cfg_family_order_at(uint8_t index);
bool ui_track_catalog_cfg_family_order_index(track_family_t family, uint8_t *out_index);
track_family_t ui_track_catalog_cfg_family_step(
    track_family_t current,
    int8_t direction,
    uint8_t track,
    const track_config_t track_configs[BRICK_ENTITY_CAPACITY]);
bool ui_track_catalog_family_has_available_type(uint8_t track,
                                                track_family_t family,
                                                const track_config_t track_configs[BRICK_ENTITY_CAPACITY]);
uint8_t ui_track_catalog_type_count_for_family(track_family_t family,
                                               uint8_t track,
                                               const track_config_t track_configs[BRICK_ENTITY_CAPACITY]);
uint8_t ui_track_catalog_type_index_for_family(track_family_t family,
                                               track_type_t type,
                                               uint8_t track,
                                               const track_config_t track_configs[BRICK_ENTITY_CAPACITY]);
track_type_t ui_track_catalog_type_from_family_index(track_family_t family,
                                                        uint8_t index,
                                                        uint8_t track,
                                                        const track_config_t track_configs[BRICK_ENTITY_CAPACITY]);
track_type_t ui_track_catalog_first_available_type(track_family_t family,
                                                      uint8_t track,
                                                      const track_config_t track_configs[BRICK_ENTITY_CAPACITY]);
track_type_t ui_track_catalog_default_type_for_family(track_family_t family);
const char *ui_track_catalog_family_display_name(track_family_t family);
const char *ui_track_catalog_family_short_name(track_family_t family);
const char *ui_track_catalog_type_display_name(track_family_t family, track_type_t type);
const char *ui_track_catalog_type_short_name(track_family_t family, track_type_t type);

#endif /* UI_TRACK_CATALOG_H */
