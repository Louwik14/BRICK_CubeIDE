#include "Sampler/sample_voice_reader.h"
#include "SD/stream_end_to_end_bench.h"
#include "Sampler/sample_page_cache_config.h"
#include "Audio/sample_page_lease_audio.h"

#include <string.h>

#include "Sampler/sample_stream_limits.h"
#include "Platform/memory_layout.h"
#include "Platform/stream_rec_perf.h"

#define SAMPLE_Q16_ONE (65536U)

typedef struct
{
    uint8_t lease_slot;
    uint8_t lease_valid;
    uint32_t lease_pages[SAMPLE_PAGE_LEASE_PAGE_COUNT];
    uint8_t lease_valid_mask;
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
    uint8_t musical_credit;
} sample_voice_reader_state_t;

static uint8_t g_sample_voice_reader_musical_credits;

void sample_voice_reader_init(void)
{
    g_sample_voice_reader_musical_credits = 0U;
}


/* Cursor/page/loop handling and render kernels remain in their original sequence.
 * Private fragments share this translation unit to preserve static state and call order. */

#include "VoiceReader/sample_voice_reader_cursor.inc"

#include "VoiceReader/sample_voice_reader_kernels.inc"

#include "VoiceReader/sample_voice_reader_multi_kernels.inc"
