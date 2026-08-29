#include "Mod/mod_matrix.h"
#include "Audio/audio_mod_matrix.h"
#include "Audio/audio_fx_runtime.h"
#include "Audio/fx_audio_drift.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Audio/drum_synth.h"
#include "Audio/md_model.h"
#include "Audio/Engines/prism_engine.h"
#include "Audio/Engines/stack_engine.h"
#include <string.h>

#include "Audio/mixer.h"
#include "Track/track_sound_state.h"
#include "Track/entity_topology.h"
#include "IPC/control_audio_command.h"
#include "Core/live_clock.h"
#include "IPC/live_parameter_audio_publication.h"
#include "Core/live_parameter_event.h"
#include "Track/track_state.h"
#include "Mod/mod_destination_catalog.h"
#include "Mod/mod_lfo_v1.h"
#include "Param/param_registry.h"
#include "Param/param_registry_runtime_state.h"
#include "Seq/seq_types.h"

/* Runtime remains entity-scoped; GROUP control state is owned by the master. */
#undef SEQ_TRACK_COUNT
#define SEQ_TRACK_COUNT SEQ_LANE_CAPACITY


typedef struct
{
    uint8_t valid;
    uint8_t modulation_active;
    uint16_t destination;
    float base_value;
    float sum;
    float sum_end;
    float min_value;
    float max_value;
    mod_destination_ramp_t ramp;
    mod_destination_prepared_t prepared;
} mod_matrix_runtime_destination_t;

typedef struct
{
    mod_matrix_runtime_destination_t destinations[MOD_MATRIX_SLOT_COUNT];
} mod_matrix_runtime_track_t;

typedef struct
{
    uint8_t any_route;
    uint16_t required_source_mask;
} mod_matrix_route_cache_t;

typedef struct
{
    uint8_t source;
    uint8_t destination_index;
    float scale;
} mod_matrix_track_route_t;

typedef struct
{
    uint8_t runtime_destination_index;
    uint16_t discontinuity_source_mask;
} mod_matrix_track_destination_t;

typedef struct
{
    uint8_t route_count;
    uint8_t destination_count;
    mod_matrix_track_route_t routes[MOD_MATRIX_SLOT_COUNT];
    mod_matrix_track_destination_t destinations[MOD_MATRIX_SLOT_COUNT];
} mod_matrix_track_plan_t;

typedef struct
{
    float multi[2];
    float slew[2];
    uint8_t multi_valid[2];
    uint8_t slew_valid[2];
} mod_matrix_operator_runtime_t;

typedef struct
{
    float slew_amount[2];
    uint8_t multi_source[2][2];
    uint8_t slew_source[2];
    uint8_t drum_md_slot_count;
} mod_matrix_operator_config_t;

typedef struct
{
    track_mod_matrix_slot_t slots[MOD_MATRIX_SLOT_COUNT];
    uint8_t multi_source[2][2];
    uint8_t slew_source[2];
    float slew_amount[2];
} mod_matrix_audio_state_t;

static mod_matrix_runtime_track_t g_mod_matrix_runtime[SEQ_TRACK_COUNT];
static mod_matrix_operator_config_t g_mod_matrix_operator_config[SEQ_TRACK_COUNT];
static mod_matrix_route_cache_t g_mod_matrix_route_cache[SEQ_TRACK_COUNT];
typedef struct
{
    uint8_t source;
    uint8_t destination_index;
    float scale;
} mod_matrix_poly_route_t;

typedef struct
{
    uint8_t runtime_destination_index;
} mod_matrix_poly_destination_t;

typedef struct
{
    uint8_t source_mask;
    uint8_t route_count;
    uint8_t destination_count;
    mod_matrix_poly_route_t routes[MOD_MATRIX_SLOT_COUNT];
    mod_matrix_poly_destination_t destinations[MOD_MATRIX_SLOT_COUNT];
} mod_matrix_poly_plan_t;

static mod_matrix_poly_plan_t g_mod_matrix_poly_plan[SEQ_TRACK_COUNT];
static mod_matrix_track_plan_t g_mod_matrix_track_plan[SEQ_TRACK_COUNT];
static mod_matrix_operator_runtime_t g_mod_matrix_operator_runtime[SEQ_TRACK_COUNT];
static mod_matrix_audio_state_t g_mod_matrix_audio_state[SEQ_TRACK_COUNT];
static uint16_t g_mod_matrix_audio_dirty_mask;
static uint8_t g_mod_matrix_any_route = 0U;


/* Private CONTROL/AUDIO fragments share this translation unit to preserve
 * the existing canonical state, derived caches and symbol visibility. */

#include "Matrix/mod_matrix_control_state.inc"

#include "Matrix/audio_mod_matrix_resolution.inc"

#include "Matrix/mod_matrix_control_defaults.inc"

#include "Matrix/audio_mod_matrix_plan.inc"

#include "Matrix/mod_matrix_control_routes.inc"

#include "Matrix/audio_mod_matrix_process.inc"
