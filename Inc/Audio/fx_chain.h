#pragma once
#include <stdint.h>

/**
 * @file fx_chain.h
 * @brief API de traitement des chaînes FX sur buffers stéréo.
 *
 * Rôle du module:
 * - Appliquer une chaîne prédéfinie (track0) ou un slot unique.
 * - Uniformiser l'appel des processeurs FX block-based.
 *
 * Contraintes temps réel:
 * - Usage principal en IRQ audio.
 * - Aucune allocation dynamique.
 */

void fx_chain_process_track0(
    float* in_l,
    float* in_r,
    uint32_t frames
);

void fx_chain_process_slot(
    uint32_t slot,
    float* in_l,
    float* in_r,
    uint32_t frames
);

void fx_chain_process_slot_for_track(
    uint32_t track,
    uint32_t slot,
    float* in_l,
    float* in_r,
    uint32_t frames
);
