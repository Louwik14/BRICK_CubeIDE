#include "Mod/mod_matrix.h"

#include "IPC/live_clock_control.h"
#include "IPC/live_parameter_audio_publication.h"
#include "IPC/live_parameter_event.h"
#include "Mod/mod_destination_control.h"
#include "Param/param_registry.h"
#include "Param/param_registry_runtime_state.h"
#include "Track/entity_topology.h"
#include "Track/track_sound_state.h"

/* CONTROL owns the editable matrix state and publishes immutable updates. */
#undef SEQ_TRACK_COUNT
#define SEQ_TRACK_COUNT SEQ_LANE_CAPACITY

#include "Matrix/mod_matrix_control_state.inc"
#include "Matrix/mod_matrix_control_routes.inc"
