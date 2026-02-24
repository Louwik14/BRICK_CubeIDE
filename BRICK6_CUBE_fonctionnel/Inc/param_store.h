#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t param_id_t;

enum {
    PARAM_GRAN_DENSITY = 0,
    PARAM_GRAN_PITCH,
    PARAM_GRAN_MIX,
    PARAM_GRAN_FREEZE,
    PARAM_GRAN_SPREAD,
    PARAM_GRAN_STEREO,
    PARAM_COUNT = 32
};

void param_store_init(void);
void param_store_set_staging(param_id_t id, float v);
bool param_store_commit_if_block_advanced(void);
float param_store_get_active(param_id_t id);

uint32_t param_store_get_commit_count(void);
uint32_t param_store_get_last_commit_block(void);

#ifdef __cplusplus
}
#endif
