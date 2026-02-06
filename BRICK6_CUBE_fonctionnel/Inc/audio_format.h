#ifndef AUDIO_FORMAT_H
#define AUDIO_FORMAT_H

#include <stdint.h>

#ifndef AUDIO_BITS_PER_SAMPLE
#define AUDIO_BITS_PER_SAMPLE 16U
#endif

#ifndef AUDIO_TDM_SLOT_BITS
#define AUDIO_TDM_SLOT_BITS 32U
#endif

#if (AUDIO_BITS_PER_SAMPLE != 16U) && (AUDIO_BITS_PER_SAMPLE != 24U)
#error "AUDIO_BITS_PER_SAMPLE must be 16 or 24."
#endif

#if (AUDIO_TDM_SLOT_BITS != 16U) && (AUDIO_TDM_SLOT_BITS != 32U)
#error "AUDIO_TDM_SLOT_BITS must be 16 or 32."
#endif

#if (AUDIO_TDM_SLOT_BITS == 16U) && (AUDIO_BITS_PER_SAMPLE != 16U)
#error "16-bit slots only support 16-bit samples."
#endif

#define AUDIO_SAMPLE_SHIFT (AUDIO_TDM_SLOT_BITS - AUDIO_BITS_PER_SAMPLE)

#if AUDIO_TDM_SLOT_BITS == 16U
typedef int16_t audio_word_t;
#else
typedef int32_t audio_word_t;
#endif

#endif /* AUDIO_FORMAT_H */
