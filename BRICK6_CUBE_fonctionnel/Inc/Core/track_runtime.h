#pragma once

#include <stdint.h>

#include "param_registry.h"
#include "ui_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    TRACK_RUNTIME_BIND_UNBOUND = 0,
    TRACK_RUNTIME_BIND_BOUND,
    TRACK_RUNTIME_BIND_QUOTA_BLOCKED
} track_runtime_bind_state_t;

typedef struct
{
    uint8_t track_id;
    ui_track_family_t family;
    ui_track_type_t type;
    track_runtime_bind_state_t bind_state;
    uint8_t flags;
} track_runtime_ctx_t;

typedef enum
{
    TRACK_RUNTIME_PARAM_DOMAIN_NONE = 0,
    TRACK_RUNTIME_PARAM_DOMAIN_COLORS,
    TRACK_RUNTIME_PARAM_DOMAIN_TONE,
    TRACK_RUNTIME_PARAM_DOMAIN_PLAY
} track_runtime_param_domain_t;

typedef enum
{
    TRACK_RUNTIME_RESOURCE_NONE = 0,
    TRACK_RUNTIME_RESOURCE_FILTER,
    TRACK_RUNTIME_RESOURCE_SYNTH,
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
const track_runtime_ctx_t *track_runtime_get_ctx(uint8_t track);
track_runtime_param_status_t track_runtime_get_effective_param_status(uint8_t track, param_id_t param);
track_runtime_param_rule_t track_runtime_get_param_rule(param_id_t param);

#ifdef __cplusplus
}
#endif

