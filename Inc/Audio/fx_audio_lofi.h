#ifndef FX_AUDIO_LOFI_H
#define FX_AUDIO_LOFI_H
#include <stdint.h>

typedef enum { FX_AUDIO_LOFI_ENGINE_SOFT = 0U, FX_AUDIO_LOFI_ENGINE_MID,
    FX_AUDIO_LOFI_ENGINE_HARD,
    FX_AUDIO_LOFI_ENGINE_COUNT } fx_audio_lofi_engine_t;
typedef enum { FX_AUDIO_LOFI_MODE_BYPASS = 0U, FX_AUDIO_LOFI_MODE_BIT_ONLY,
    FX_AUDIO_LOFI_MODE_SRR_ONLY, FX_AUDIO_LOFI_MODE_BIT_AND_SRR } fx_audio_lofi_mode_t;

typedef struct { float last[2], grabbed[2], last_grabbed[2]; uint32_t low_pos, high_pos; } fx_audio_lofi_float_state_t;
typedef struct
{
    fx_audio_lofi_float_state_t srr;
    float bit_scale, bit_reciprocal;
} fx_audio_lofi_hybrid_state_t;
typedef struct { float position, increment_srr, increment_bit, held[2], last[2]; } fx_audio_lofi_derez_state_t;
typedef struct
{
    /* Airwindows' bez[] layout is retained: AL/BL/CL/InL/UnInL/SampL,
     * AR/BR/CR/InR/UnInR/SampR/cycle. */
    float bez[13];
    float rez;
    float bit_factor;
    float bit_inverse;
} fx_audio_lofi_derez3_state_t;

typedef struct
{
    /* Only one kernel runs at once. The selected member is cleared on every
     * engine change, so the union saves per-entity RAM without history leak. */
    union {
        fx_audio_lofi_hybrid_state_t hybrid;
        fx_audio_lofi_derez_state_t derez;
        fx_audio_lofi_derez3_state_t derez3;
    };
    uint32_t low_increment, high_increment;
    float derez_target_srr, derez_target_bit, derez_soften;
    uint8_t engine, mode;
} fx_audio_lofi_state_t;

_Static_assert(sizeof(fx_audio_lofi_state_t) == 88U,
               "LOFI final state size changed");

void fx_audio_lofi_reset(fx_audio_lofi_state_t *state);
void fx_audio_lofi_set_engine(fx_audio_lofi_state_t *state, fx_audio_lofi_engine_t engine);
void fx_audio_lofi_prepare(fx_audio_lofi_state_t *state, float bit, float srr);
float fx_audio_lofi_process_mono_sample(fx_audio_lofi_state_t *state, float sample);
void fx_audio_lofi_process_stereo_sample(fx_audio_lofi_state_t *state, float *left, float *right);
void fx_audio_lofi_process_stereo(fx_audio_lofi_state_t *state, float *left, float *right, uint32_t frames);
void fx_audio_lofi_process_mono(fx_audio_lofi_state_t *state, float *buffer, uint32_t frames);
#endif
