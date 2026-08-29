#include "Sampler/sample_voice_reader.h"

#include <string.h>

#include "Sampler/sample_stream_limits.h"
#include "Platform/memory_layout.h"

#define SAMPLE_Q16_ONE (65536U)

typedef struct
{
    uint8_t cache_voice_id;
    uint8_t cache_voice_valid;
    uint16_t sample_id;
    sample_audio_key_t key;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
    float position;
    float step;
    uint32_t frame_pos;
    uint8_t active;
    sample_play_plan_t plan;
    sample_audio_cursor_t audio_cursor;
    uint8_t plan_valid;
    uint8_t loop_cache_voice_id;
    uint8_t loop_cache_valid;
    uint32_t loop_cache_generation;
} sample_voice_reader_state_t;

#if BRICK6_STREAM_PRODUCT_VOICE_LOOP_CACHE_PAGES > 0U
typedef struct
{
    sample_voice_reader_state_t *reader;
    sample_audio_key_t key;
    sample_page_ref_t refs[SAMPLE_PAGE_VOICE_LOOP_CACHE_MAX_PAGES];
    uint32_t generation;
    uint8_t voice_id;
    uint8_t valid_mask;
} sample_voice_loop_cache_t;

SDRAM_STREAM_SERVICE static sample_voice_loop_cache_t
    g_sample_voice_loop_cache[SAMPLE_STREAM_TARGET_MAX_VOICES];
#endif


/* Cursor/page/loop handling and render kernels remain in their original sequence.
 * Private fragments share this translation unit to preserve static state and call order. */

#include "VoiceReader/sample_voice_reader_cursor.inc"

#include "VoiceReader/sample_voice_reader_kernels.inc"

#include "VoiceReader/sample_voice_reader_multi_kernels.inc"
