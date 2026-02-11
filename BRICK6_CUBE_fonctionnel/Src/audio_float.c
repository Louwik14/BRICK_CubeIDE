/* ============================================================
   audio_float.c
   STM32H743 + CS42448 TDM8
   Daisy-inspired Float Boundary Engine
   ============================================================ */

#include "audio_float.h"
#include <string.h>
#include <stdint.h>

/* ============================================================
   CONFIG
   ============================================================ */

#define AUDIO_TDM_SLOTS          8
#define AUDIO_ADC_CHANNELS       6
#define AUDIO_DAC_CHANNELS       8

/* Must match AUDIO_FRAMES_PER_HALF in audio.c */
#define AUDIO_BLOCK_SIZE         32

/* ============================================================
   Daisy-style gain staging
   ============================================================ */
static float postgain_recip = 1.0f;   /* = 1/postgain */
static float output_adjust  = 1.0f;   /* postgain * output_comp */

static float postgain       = 1.0f;
static float output_comp    = 1.0f;

void audio_float_set_postgain(float gain)
{
    if(gain <= 0.0f)
        gain = 1.0f;

    postgain = gain;
    postgain_recip = 1.0f / postgain;

    output_adjust = postgain * output_comp;
}

void audio_float_set_output_compensation(float comp)
{
    output_comp = comp;
    output_adjust = postgain * output_comp;
}

/* ============================================================
   INTERNAL FLOAT BUFFERS
   ============================================================ */

static float in_buf[AUDIO_ADC_CHANNELS][AUDIO_BLOCK_SIZE];
static float out_buf[AUDIO_DAC_CHANNELS][AUDIO_BLOCK_SIZE];

static float *in_ptrs[AUDIO_ADC_CHANNELS];
static float *out_ptrs[AUDIO_DAC_CHANNELS];

/* ============================================================
   USER CALLBACK
   ============================================================ */

static audio_float_cb float_cb = NULL;

void audio_set_float_callback(audio_float_cb cb)
{
    float_cb = cb;
}

/* ============================================================
   CONVERSION HELPERS : CS42448 + STM32H7 SAI (TDM8, 24-bit)

   IMPORTANT DISCOVERY (DEBUGGED WITH GDB):

   Even though the CS42448 datasheet describes audio samples as
   "left-justified in a 32-bit slot", the STM32H7 SAI peripheral,
   when configured with:

       Init.DataSize = SAI_DATASIZE_24

   delivers samples in DMA buffers as:

       24-bit SIGNED audio, RIGHT-ALIGNED in the 32-bit word
       (bits [23:0] contain the sample, bits [31:24] may be zero)

   Example captured in GDB:

       rx[0] = 0x00FFFFFF
       rx[1] = 0x00000904
       rx[2] = 0x00FFF960

   => The lower 8 bits are NOT zero, so this is NOT left-aligned.

   CONSEQUENCE:

   We MUST NOT do ">>8" or "<<8" shifts here.
   Instead we treat DMA words as signed 24-bit integers packed
   into bits [23:0].

   This fix was required for correct gain scaling and to prevent
   mysterious saturation when applying float DSP or mixer gains.
   ============================================================ */


/* ------------------------------------------------------------
   int24 (right-aligned) -> float [-1.0 .. +1.0]

   DMA word layout:

       [31........24][23..................0]
           unused        signed 24-bit audio

   We must sign-extend manually before converting.
   ------------------------------------------------------------ */
static inline float s242f(int32_t x)
{
    /* Sign extend from 24-bit to full int32 */
    if(x & 0x00800000)
        x |= 0xFF000000;

    /* Scale to float range [-1..1] */
    return (float)x * (1.0f / 8388608.0f); /* 2^23 */
}


/* ------------------------------------------------------------
   float [-1.0 .. +1.0] -> int24 (right-aligned)

   We clamp, convert back to signed int24,
   then keep only the lower 24 bits.

   Output DMA layout:

       0x00XXXXXX
   ------------------------------------------------------------ */
static inline int32_t f2s24(float x)
{
    /* Hard clamp */
    if(x > 0.999999f) x = 0.999999f;
    if(x < -1.0f)     x = -1.0f;

    /* Convert float -> signed int24 */
    int32_t v = (int32_t)(x * 8388607.0f);

    /* Keep only 24 bits (right-aligned) */
    return v & 0x00FFFFFF;
}
/* ============================================================
   MAIN DSP BLOCK PROCESSOR
   Called by audio.c IRQ layer
   ============================================================ */

void audio_process_block_int32(int32_t *rx,
                               int32_t *tx,
                               uint32_t frames)
{
    /* ------------------------------------------------------------
       1. Unpack RX -> float inputs (with postgain recip)
       ------------------------------------------------------------ */

    for(uint32_t n = 0; n < frames; n++)
    {
        for(uint32_t ch = 0; ch < AUDIO_ADC_CHANNELS; ch++)
        {
            int32_t sample = rx[n * AUDIO_TDM_SLOTS +(ch )];
            in_buf[ch][n] = s242f(sample) * postgain_recip;
        }
    }

    /* ------------------------------------------------------------
       2. Clear outputs
       ------------------------------------------------------------ */

    for(uint32_t ch = 0; ch < AUDIO_DAC_CHANNELS; ch++)
    {
        for(uint32_t n = 0; n < frames; n++)
            out_buf[ch][n] = 0.0f;
    }

    /* ------------------------------------------------------------
       3. Prepare pointer arrays
       ------------------------------------------------------------ */

    for(uint32_t ch = 0; ch < AUDIO_ADC_CHANNELS; ch++)
        in_ptrs[ch] = in_buf[ch];

    for(uint32_t ch = 0; ch < AUDIO_DAC_CHANNELS; ch++)
        out_ptrs[ch] = out_buf[ch];

    /* ------------------------------------------------------------
       4. Run DSP callback
       ------------------------------------------------------------ */

    if(float_cb)
    {
        float_cb(in_ptrs, out_ptrs, frames);
    }
    else
    {
        /* Default passthrough */
        for(uint32_t ch = 0; ch < AUDIO_ADC_CHANNELS; ch++)
        {
            for(uint32_t n = 0; n < frames; n++)
                out_buf[ch][n] = in_buf[ch][n];
        }
    }

    /* ------------------------------------------------------------
       5. Force DAC7/8 = zero for now
       ------------------------------------------------------------ */

    /*for(uint32_t n = 0; n < frames; n++)
    {
        out_buf[6][n] = 0.0f;
        out_buf[7][n] = 0.0f;
    }*/

    /* ------------------------------------------------------------
       6. Pack float outputs -> TX (with output adjust)
       ------------------------------------------------------------ */

    /* ------------------------------------------------------------
       6. Pack float outputs -> TX (with output adjust)
       ------------------------------------------------------------ */



    for(uint32_t n = 0; n < frames; n++)
    {
        for(uint32_t slot = 0; slot < AUDIO_TDM_SLOTS; slot++)
        {
            tx[n * AUDIO_TDM_SLOTS + slot] =
                f2s24(out_buf[slot][n]);
        }
    }
}
