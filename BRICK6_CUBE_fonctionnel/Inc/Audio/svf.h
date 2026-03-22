#pragma once
#ifndef DSY_SVF_H
#define DSY_SVF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Implémentation C-compatible du SVF double-samplé Daisy.
 *
 * Ce module garde l'algorithme stable de DaisySP, mais expose une structure
 * et des fonctions C pour pouvoir l'utiliser depuis `mixer.c` sans partager
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

    float notch;
    float low;
    float high;
    float band;
    float peak;
    float input;

    float out_low;
    float out_high;
    float out_band;
    float out_peak;
    float out_notch;

    float pre_drive;
    float fc_max;
} svf_t;

void svf_init(svf_t *svf, float sample_rate);
void svf_process(svf_t *svf, float in);
void svf_set_freq(svf_t *svf, float cutoff_hz);
void svf_set_res(svf_t *svf, float resonance_0_1);
void svf_set_drive(svf_t *svf, float drive);

static inline float svf_low(const svf_t *svf) { return svf->out_low; }
static inline float svf_high(const svf_t *svf) { return svf->out_high; }
static inline float svf_band(const svf_t *svf) { return svf->out_band; }
static inline float svf_notch(const svf_t *svf) { return svf->out_notch; }
static inline float svf_peak(const svf_t *svf) { return svf->out_peak; }

#ifdef __cplusplus
}
#endif

#endif
