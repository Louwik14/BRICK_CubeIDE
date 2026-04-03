#pragma once

#include <stdint.h>

#include "param_registry.h"
#include "Seq/seq_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    TRACK_RUNTIME_BIND_UNBOUND = 0,
    TRACK_RUNTIME_BIND_BOUND,
    TRACK_RUNTIME_BIND_QUOTA_BLOCKED
} track_runtime_bind_state_t;

typedef enum
{
    TRACK_RUNTIME_BIND_REASON_NONE = 0,
    TRACK_RUNTIME_BIND_REASON_TRACK_OFF,
    TRACK_RUNTIME_BIND_REASON_UNSUPPORTED,
    TRACK_RUNTIME_BIND_REASON_QUOTA_EXCEEDED
} track_runtime_bind_reason_t;

typedef enum
{
    TRACK_RUNTIME_ENGINE_NONE = 0,
    TRACK_RUNTIME_ENGINE_AUDIO_TRACK,
    TRACK_RUNTIME_ENGINE_DX7,
    TRACK_RUNTIME_ENGINE_MONOB,
    TRACK_RUNTIME_ENGINE_TB3
} track_runtime_engine_t;

typedef struct
{
    uint8_t track_id;
    uint8_t mix_track_id;
    uint8_t family;
    uint8_t type;
    uint8_t engine;
    uint8_t instance_id;
    track_runtime_bind_state_t bind_state;
    track_runtime_bind_reason_t bind_reason;
    uint8_t flags;
} track_runtime_ctx_t;

typedef enum
{
    TRACK_RUNTIME_FAMILY_OFF = 0,
    TRACK_RUNTIME_FAMILY_INPUT,
    TRACK_RUNTIME_FAMILY_SYNTH,
    TRACK_RUNTIME_FAMILY_OTHER
} track_runtime_family_t;

typedef enum
{
    TRACK_RUNTIME_TYPE_AUDIO = 0,
    TRACK_RUNTIME_TYPE_HYBRID,
    TRACK_RUNTIME_TYPE_DX7,
    TRACK_RUNTIME_TYPE_MONOB,
    TRACK_RUNTIME_TYPE_TB3,
    TRACK_RUNTIME_TYPE_OTHER
} track_runtime_type_t;

typedef enum
{
    TRACK_RUNTIME_PARAM_DOMAIN_NONE = 0,
    TRACK_RUNTIME_PARAM_DOMAIN_COLORS,
    TRACK_RUNTIME_PARAM_DOMAIN_TONE,
    TRACK_RUNTIME_PARAM_DOMAIN_MIX,
    TRACK_RUNTIME_PARAM_DOMAIN_PLAY
} track_runtime_param_domain_t;

typedef enum
{
    TRACK_RUNTIME_RESOURCE_NONE = 0,
    TRACK_RUNTIME_RESOURCE_FILTER,
    TRACK_RUNTIME_RESOURCE_SYNTH,
    TRACK_RUNTIME_RESOURCE_MIX,
    TRACK_RUNTIME_RESOURCE_PLAY
} track_runtime_resource_t;

typedef enum
{
    TRACK_RUNTIME_CARDINALITY_PER_TRACK = 0,
    TRACK_RUNTIME_CARDINALITY_SHARED,
    TRACK_RUNTIME_CARDINALITY_GLOBAL
} track_runtime_cardinality_t;

typedef enum
{
    TRACK_RUNTIME_PARAM_ALLOWED = 0,
    TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL,
    TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED
} track_runtime_param_status_t;

typedef struct
{
    track_runtime_param_domain_t domain;
    track_runtime_resource_t resource;
    track_runtime_cardinality_t cardinality;
    track_runtime_param_status_t status;
} track_runtime_param_rule_t;

void track_runtime_init(void);
void track_runtime_refresh_track(uint8_t track);
void track_runtime_refresh_all(void);
const track_runtime_ctx_t *track_runtime_get_ctx(uint8_t track);
uint8_t track_runtime_get_mix_target_track(uint8_t track, uint8_t *out_mix_track);
track_runtime_param_status_t track_runtime_get_effective_param_status(uint8_t track, param_id_t param);
track_runtime_param_rule_t track_runtime_get_param_rule(param_id_t param);

#ifdef __cplusplus
}
#endif
