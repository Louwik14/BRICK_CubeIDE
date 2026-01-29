/**
 * @file brick6_refactor.c
 * @brief Compteurs et instrumentation pour le plan de refactor BRICK6.
 *
 * Ce module centralise les compteurs globaux utilisés pour mesurer
 * l'activité IRQ/tasklets durant les étapes de refactor.
 *
 * Rôle dans le système:
 * - Stockage des compteurs d'instrumentation accessibles partout.
 * - Support aux diagnostics et au suivi des budgets CPU.
 *
 * Contraintes temps réel:
 * - Critique audio: non (données simples, accès atomiques).
 * - IRQ: oui (incrément depuis callbacks).
 * - Tasklet: oui (lecture depuis diagnostics).
 * - Borné: oui (incréments simples).
 *
 * Architecture:
 * - Appelé par: callbacks IRQ audio/SD/USB et main loop.
 * - Appelle: aucun module.
 *
 * Règles:
 * - Pas de malloc.
 * - Pas de logique lourde.
 *
 * @note L’API publique est déclarée dans brick6_refactor.h.
 */

#include "brick6_refactor.h"

#if BRICK6_REFACTOR_STEP_1
volatile uint32_t brick6_audio_tx_half_count = 0U;
volatile uint32_t brick6_audio_tx_full_count = 0U;
volatile uint32_t brick6_audio_rx_half_count = 0U;
volatile uint32_t brick6_audio_rx_full_count = 0U;
volatile uint32_t brick6_sd_rx_cplt_count = 0U;
volatile uint32_t brick6_sd_tx_cplt_count = 0U;
volatile uint32_t brick6_sd_err_count = 0U;
volatile uint32_t brick6_sd_buf0_cplt_count = 0U;
volatile uint32_t brick6_sd_buf1_cplt_count = 0U;
volatile uint32_t brick6_usb_host_poll_count = 0U;
volatile uint32_t brick6_midi_host_poll_count = 0U;
#endif

#if BRICK6_REFACTOR_STEP_6
volatile uint32_t usb_budget_hit_count = 0U;
volatile uint32_t midi_budget_hit_count = 0U;
volatile uint32_t sd_budget_hit_count = 0U;
#endif

#if BRICK6_REFACTOR_STEP_5
volatile uint32_t audio_underflow_count = 0U;
#endif
