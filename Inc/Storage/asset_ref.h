#ifndef ASSET_REF_H
#define ASSET_REF_H

#include <stdint.h>
#include "Storage/persistent_control_model.h"

uint8_t asset_ref_kind_valid(persist_control_asset_kind_key_t kind);
uint8_t asset_ref_make_canonical(persist_control_asset_kind_key_t kind,
                                 const char *source_path,
                                 persist_control_asset_ref_t *out_ref);
uint8_t asset_ref_is_canonical(const persist_control_asset_ref_t *ref);

#endif
