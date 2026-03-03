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

    PARAM_MIX_TRACK0_GAIN,
    PARAM_MIX_TRACK1_GAIN,
    PARAM_MIX_TRACK2_GAIN,
    PARAM_MIX_TRACK3_GAIN,
    PARAM_MIX_TRACK0_PAN,
    PARAM_MIX_TRACK1_PAN,
    PARAM_MIX_TRACK2_PAN,
    PARAM_MIX_TRACK3_PAN,
    PARAM_MIX_TRACK0_MUTE,
    PARAM_MIX_TRACK1_MUTE,
    PARAM_MIX_TRACK2_MUTE,
    PARAM_MIX_TRACK3_MUTE,
    PARAM_MIX_TRACK0_ROUTE,
    PARAM_MIX_TRACK1_ROUTE,
    PARAM_MIX_TRACK2_ROUTE,
    PARAM_MIX_TRACK3_ROUTE,
    PARAM_MIX_TRACK0_INSERT0,
    PARAM_MIX_TRACK0_INSERT1,
    PARAM_MIX_TRACK1_INSERT0,
    PARAM_MIX_TRACK1_INSERT1,
    PARAM_MIX_TRACK2_INSERT0,
    PARAM_MIX_TRACK2_INSERT1,
    PARAM_MIX_TRACK3_INSERT0,
    PARAM_MIX_TRACK3_INSERT1,
    PARAM_MIX_TRACK0_SEND0,
    PARAM_MIX_TRACK0_SEND1,
    PARAM_MIX_TRACK1_SEND0,
    PARAM_MIX_TRACK1_SEND1,
    PARAM_MIX_TRACK2_SEND0,
    PARAM_MIX_TRACK2_SEND1,
    PARAM_MIX_TRACK3_SEND0,
    PARAM_MIX_TRACK3_SEND1,
    PARAM_MIX_SEND0_FX,
    PARAM_MIX_SEND1_FX,

    PARAM_BUS_COMP_THRESHOLD_DB,
    PARAM_BUS_COMP_RATIO,
    PARAM_BUS_COMP_ATTACK_INDEX,
    PARAM_BUS_COMP_RELEASE_INDEX,

    PARAM_COUNT = 64
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
