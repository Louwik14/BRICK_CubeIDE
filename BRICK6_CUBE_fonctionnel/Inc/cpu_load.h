#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file cpu_load.h
 * @brief Mesure légère de charge CPU audio (DSP) via DWT->CYCCNT.
 *
 * Ce module mesure le nombre de cycles CPU consommés par un bloc audio,
 * puis calcule une charge en permille (0..1000 = 0..100.0%).
 *
 * Contexte:
 * - Écriture des métriques dans l'IRQ audio.
 * - Lecture des métriques dans la main loop/UI.
 * - Variables 32-bit volatiles (accès atomiques simples sur Cortex-M7).
 */

#define CPU_LOAD_MODE_DSP_ONLY  0U
#define CPU_LOAD_MODE_IRQ_TOTAL 1U

#ifndef CPU_LOAD_MODE
#define CPU_LOAD_MODE CPU_LOAD_MODE_DSP_ONLY
#endif

#if (CPU_LOAD_MODE != CPU_LOAD_MODE_DSP_ONLY) && \
    (CPU_LOAD_MODE != CPU_LOAD_MODE_IRQ_TOTAL)
#error "CPU_LOAD_MODE invalide"
#endif

/**
 * @brief Initialise le compteur DWT et le budget cycles par bloc.
 *
 * @param sample_rate_hz Fréquence d'échantillonnage audio (ex: 48000 Hz).
 * @param frames_per_block Taille bloc audio en frames (ex: 32).
 */
void cpu_load_init(uint32_t sample_rate_hz, uint32_t frames_per_block);

/**
 * @brief Marque le début de mesure d'un bloc audio (IRQ).
 */
void cpu_load_block_start_irq(void);

/**
 * @brief Marque la fin de mesure d'un bloc audio et met à jour les métriques (IRQ).
 */
void cpu_load_block_end_irq(void);

/**
 * @brief Charge CPU du dernier bloc, en permille (0..1000).
 */
uint32_t cpu_load_get_permille(void);

/**
 * @brief Charge CPU max observée depuis init/reset, en permille.
 */
uint32_t cpu_load_get_max_permille(void);

/**
 * @brief Nombre de dépassements de budget (permille > 1000).
 */
uint32_t cpu_load_get_overruns(void);

/**
 * @brief Remet à zéro le pic de charge max.
 */
void cpu_load_reset_max(void);

#ifdef __cplusplus
}
#endif
