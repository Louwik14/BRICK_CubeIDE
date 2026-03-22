#pragma once
#ifndef DSY_SVF_H
#define DSY_SVF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Implémentation C-compatible du SVF Daisy en passe simple.
 *
 * Ce module garde le principe du SVF actuel, mais expose une structure et des
 * fonctions C pour pouvoir l'utiliser depuis `mixer.c` sans partager
 * d'instance entre tracks.
 */
typedef struct
{
    float sr;
    float fc;
    float res;
    float drive;
    float freq;
    float damp;

    float low;
    float band;
    float pre_drive;
    float fc_max;
} svf_t;

typedef enum
{
    SVF_MODE_LP = 0,
    SVF_MODE_HP,
    SVF_MODE_BP
} svf_mode_t;

void svf_init(svf_t *svf, float sample_rate);
void svf_set_freq(svf_t *svf, float cutoff_hz);
void svf_set_res(svf_t *svf, float resonance_0_1);
void svf_set_drive(svf_t *svf, float drive);
float svf_process_mode(svf_t *svf, float in, svf_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif
