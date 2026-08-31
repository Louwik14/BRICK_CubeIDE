#pragma once
#include "Mod/mod_destination_contract.h"
void mod_destination_catalog_init(void);
uint16_t mod_destination_catalog_count(uint8_t track);
param_id_t mod_destination_catalog_param_from_index(uint8_t track, uint16_t index);
uint16_t mod_destination_catalog_index_from_param(uint8_t track, param_id_t dest);
mod_destination_address_t mod_destination_catalog_address_from_index(uint8_t owner, uint16_t index);
uint16_t mod_destination_catalog_index_from_address(uint8_t owner, mod_destination_address_t address);
uint8_t mod_destination_catalog_label(uint8_t track, uint16_t index, char *out, uint32_t len);
uint8_t mod_destination_catalog_short_label(uint8_t track, uint16_t index, char *out, uint32_t len);
void mod_destination_catalog_invalidate_track(uint8_t track);
void mod_destination_catalog_invalidate_all(void);
