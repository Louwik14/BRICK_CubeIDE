#pragma once
#include <stddef.h>
#include <stdint.h>

#include "Core/entity_topology.h"

/**
 * @file fx_chain.h
 * @brief API de traitement des chaînes FX sur buffers stéréo.
 *
 * Rôle du module:
 * - Appliquer un slot FX sur un buffer stéréo.
 * - Uniformiser l'appel des processeurs FX block-based.
 *
 * Contraintes temps réel:
 * - Usage principal en IRQ audio.
 * - Aucune allocation dynamique.
 */

void fx_chain_process_global_slot(
    uint32_t slot,
    float* in_l,
    float* in_r,
    uint32_t frames
);

/* Legacy inserts and the fixed COMP stage run before the track fader. */
void fx_chain_process_track_inserts_pre_fader(
    brick_entity_id_t entity_id,
    uint32_t legacy_track,
    const int8_t *legacy_slots,
    size_t legacy_slot_count,
    uint8_t process_audio_fx_comp,
    float* in_l,
    float* in_r,
    uint32_t frames
);

/* DRIVE/FOLD/LOFI keep their existing post-fader Audio FX placement. */
void fx_chain_process_audio_fx_post_fader(
    brick_entity_id_t entity_id,
    float* in_l,
    float* in_r,
    uint32_t frames
);

/* Keeps mono/poly fan-out optimizations inside the insert abstraction. */
uint8_t fx_chain_track_inserts_require_stereo(
    brick_entity_id_t entity_id,
    const int8_t *legacy_slots,
    size_t legacy_slot_count
);
uint8_t fx_chain_track_has_pre_fader_insert(
    brick_entity_id_t entity_id,
    const int8_t *legacy_slots,
    size_t legacy_slot_count
);

uint8_t fx_chain_audio_fx_is_pre_filter(brick_entity_id_t entity_id);
uint8_t fx_chain_audio_fx_is_active(brick_entity_id_t entity_id);
uint8_t fx_chain_audio_fx_is_comp(brick_entity_id_t entity_id);

void fx_chain_process_audio_fx_pre_filter_mono(
    brick_entity_id_t entity_id,
    float *buffer,
    uint32_t frames
);

void fx_chain_process_audio_fx_pre_filter_stereo(
    brick_entity_id_t entity_id,
    float *left,
    float *right,
    uint32_t frames
);

/* Fixed COMP stage used by the mono-native fast path before track gain/pan. */
float fx_chain_process_audio_fx_comp_mono_sample(
    brick_entity_id_t entity_id,
    float sample
);

void fx_chain_process_audio_fx_comp_stereo_sample(
    brick_entity_id_t entity_id,
    float *left,
    float *right
);

/* Existing post-fader placement for non-COMP Audio FX. */
void fx_chain_process_audio_fx_post_fader_stereo_sample(
    brick_entity_id_t entity_id,
    float *left,
    float *right
);
