/**
 * @file param_registry.c
 * @brief Module applicatif param_registry.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à param_registry.
 * - Fournir les services internes utilisés par le firmware utilisateur.
 *
 * Architecture:
 * - Appelé par: modules applicatifs selon l'orchestration du firmware.
 * - Appelle: dépendances matérielles et/ou modules utilisateur associés.
 *
 * Contraintes temps réel:
 * - IRQ: selon les API appelées.
 * - Hard realtime: selon le chemin d'exécution.
 * - malloc: éviter en chemin critique.
 *
 * Notes:
 * - Documentation ajoutée sans modification de la logique d'exécution.
 */

#include "param_registry.h"
#include "param_store.h"
#include "Param/engine_model_catalog.h"
#include "Sampler/audio_wave_table_projection_control.h"
#include "Track/control_music_output.h"
#include "NoteFx/note_fx_state.h"
#include "NoteFx/note_fx_pipeline.h"
#define SEQ_RUNTIME_INTERNAL_USE 1
#include "Seq/seq_play_scheduler.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Param/param_macro.h"
#include "Param/param_filter.h"
#include "Param/param_registry_backends.h"
#include "Param/param_control_backends.h"
#include "Param/param_registry_runtime_state.h"
#include "Seq/seq_param_iface.h"
#include "Track/track_runtime.h"
#include "Track/entity_topology.h"
#include "Track/track_mute.h"
#include "Track/track_sound_state.h"
#include "Track/track_state.h"
#include "Param/live_parameter_migration.h"
#include "IPC/live_parameter_audio_publication.h"
#include "IPC/live_parameter_event.h"
#include "IPC/live_clock_control.h"
#include "Mod/mod_lfo_v1_control.h"
#include "Mod/mod_env3.h"
#include "Mod/mod_matrix_control.h"
#include "Mod/mod_destination_control.h"
#include "UI/ui_core.h"
#include "UI/ui_track_catalog.h"
#include "Keyboard/keyboard_engine.h"
#include <stddef.h>
#include <string.h>
#include <math.h>


#include "Registry/param_audio_projection.inc"

#include "Registry/param_control_config.inc"

#include "Registry/param_control_registry.inc"

#include "Registry/param_audio_apply.inc"

#include "Registry/param_control_api.inc"
