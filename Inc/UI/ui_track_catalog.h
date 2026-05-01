#ifndef UI_TRACK_CATALOG_H
#define UI_TRACK_CATALOG_H

#include <stdbool.h>
#include <stdint.h>

#include "ui_core.h"

#define BRICK6_MAX_CLIP_TRACKS 4U

bool ui_track_catalog_family_is_input(ui_track_family_t family);
bool ui_track_catalog_family_is_engine(ui_track_family_t family);
bool ui_track_catalog_type_is_valid_for_family(ui_track_family_t family, ui_track_type_t type);
bool ui_track_catalog_type_is_available(uint8_t track,
                                        ui_track_family_t family,
                                        ui_track_type_t type,
                                        const ui_track_config_t track_configs[UI_TRACK_COUNT]);
bool ui_track_catalog_family_is_available(uint8_t track,
                                          ui_track_family_t family,
                                          const ui_track_config_t track_configs[UI_TRACK_COUNT]);
uint8_t ui_track_catalog_type_count_for_family(ui_track_family_t family,
                                               uint8_t track,
                                               const ui_track_config_t track_configs[UI_TRACK_COUNT]);
uint8_t ui_track_catalog_type_index_for_family(ui_track_family_t family,
                                               ui_track_type_t type,
                                               uint8_t track,
                                               const ui_track_config_t track_configs[UI_TRACK_COUNT]);
ui_track_type_t ui_track_catalog_type_from_family_index(ui_track_family_t family,
                                                        uint8_t index,
                                                        uint8_t track,
                                                        const ui_track_config_t track_configs[UI_TRACK_COUNT]);
ui_track_type_t ui_track_catalog_first_available_type(ui_track_family_t family,
                                                      uint8_t track,
                                                      const ui_track_config_t track_configs[UI_TRACK_COUNT]);
ui_track_type_t ui_track_catalog_default_type_for_family(ui_track_family_t family);
const char *ui_track_catalog_family_display_name(ui_track_family_t family);
const char *ui_track_catalog_family_short_name(ui_track_family_t family);
const char *ui_track_catalog_type_display_name(ui_track_family_t family, ui_track_type_t type);
const char *ui_track_catalog_type_short_name(ui_track_family_t family, ui_track_type_t type);

#endif /* UI_TRACK_CATALOG_H */
