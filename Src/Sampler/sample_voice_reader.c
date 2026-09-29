#include "Sampler/sample_voice_reader.h"
#include "Sampler/sample_page_cache_config.h"
#include "Audio/sample_page_lease_audio.h"

#include <string.h>

#include "Sampler/sample_stream_limits.h"
#include "Sampler/sample_stream_diag.h"
#include "Sampler/sample_page_cache_shared_contract.h"
#include "Platform/intercore_cache.h"
#include "Platform/memory_layout.h"

#define SAMPLE_Q16_ONE (65536U)
#define SAMPLE_STREAM_DIAG_CLIP_READER_BASE (2U)

typedef struct
{
    uint8_t lease_slot;
    uint8_t lease_valid;
    sample_page_lease_range_t lease_ranges[2];
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

#if SAMPLE_AUDIO_FORMAT_VOICE_LOOP_CACHE_FRAMES > 0U
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

void sample_voice_reader_init(void)
{
#if SAMPLE_AUDIO_FORMAT_VOICE_LOOP_CACHE_FRAMES > 0U
    memset(g_sample_voice_loop_cache, 0, sizeof(g_sample_voice_loop_cache));
#endif
}

static uint8_t sample_voice_reader_diag_slot(const sample_voice_reader_state_t *state)
{
    if (state->key.domain == SAMPLE_AUDIO_DOMAIN_CLASSIC
        && state->lease_slot >= SAMPLE_STREAM_DIAG_CLIP_READER_BASE
        && state->lease_slot < SAMPLE_STREAM_DIAG_CLIP_READER_BASE + SAMPLE_STREAM_TARGET_MAX_VOICES)
        return (uint8_t)(state->lease_slot - SAMPLE_STREAM_DIAG_CLIP_READER_BASE);
    if (state->key.domain == SAMPLE_AUDIO_DOMAIN_REC
        && state->lease_slot >= SAMPLE_PAGE_LEASE_REC_BASE + SAMPLE_STREAM_DIAG_CLIP_READER_BASE
        && state->lease_slot < SAMPLE_PAGE_LEASE_REC_BASE + SAMPLE_STREAM_DIAG_CLIP_READER_BASE
            + SAMPLE_STREAM_TARGET_MAX_VOICES)
        return (uint8_t)(state->lease_slot - SAMPLE_PAGE_LEASE_REC_BASE
                         - SAMPLE_STREAM_DIAG_CLIP_READER_BASE);
    if (state->key.domain == SAMPLE_AUDIO_DOMAIN_MULTI) {
        if (state->loop_cache_valid && state->loop_cache_voice_id < SAMPLE_STREAM_TARGET_MAX_VOICES)
            return state->loop_cache_voice_id;
        if (state->lease_slot >= SAMPLE_PAGE_LEASE_MULTI_BASE
            && state->lease_slot < SAMPLE_PAGE_LEASE_MULTI_BASE + SAMPLE_STREAM_TARGET_MAX_VOICES)
            return (uint8_t)(state->lease_slot - SAMPLE_PAGE_LEASE_MULTI_BASE);
    }
    return UINT8_MAX;
}

static void sample_voice_reader_diag_fault_page(sample_voice_reader_state_t *state,
                                                uint32_t frame, uint32_t page)
{
    const uint8_t slot = sample_voice_reader_diag_slot(state);
    if (slot >= SAMPLE_STREAM_TARGET_MAX_VOICES) return;
    const sample_page_state_t page_state = sample_page_cache_audio_get_page_state_key(state->key, page);
    sample_stream_diag_fault(slot, state->key, frame, page, page_state,
        (page_state == SAMPLE_PAGE_FREE) ? STREAM_DIAG_AUDIO_MISS : STREAM_DIAG_AUDIO_NOT_READY);
}

static void sample_voice_reader_diag_fault(sample_voice_reader_state_t *state,
                                           uint32_t frame)
{
    if (state->frames_per_page)
        sample_voice_reader_diag_fault_page(state, frame, frame / state->frames_per_page);
}

static void sample_voice_reader_diag_check_ref(sample_voice_reader_state_t *state,
                                               const sample_page_ref_t *ref)
{
    const uint8_t slot = sample_voice_reader_diag_slot(state);
    if (slot >= SAMPLE_STREAM_TARGET_MAX_VOICES || ref->slot_index >= SAMPLE_PAGE_MAX_COUNT) return;
    const sample_page_shared_descriptor_t *const page = &g_sample_page_shared_descriptor[ref->slot_index];
    intercore_cache_consume(page, sizeof(*page));
    uint32_t event = 0U;
    if (sample_audio_key_equal(&page->key, &ref->key) == 0U || page->page_index != ref->page_index)
        event = STREAM_DIAG_AUDIO_BAD_KEY;
    else if (page->generation != ref->page_generation ||
             page->registration_epoch != ref->registration_epoch)
        event = STREAM_DIAG_AUDIO_BAD_EPOCH;
    else if (page->state != SAMPLE_PAGE_READY) event = STREAM_DIAG_AUDIO_NOT_READY;
    if (event) sample_stream_diag_fault(slot, state->key, state->frame_pos,
        ref->page_index, page->state, event);
}

static void sample_voice_reader_diag_check_cursor(sample_voice_reader_state_t *state)
{
    if (state->audio_cursor.current_acquired)
        sample_voice_reader_diag_check_ref(state, &state->audio_cursor.current_page_ref);
}

static void sample_voice_reader_diag_check_neighbor(sample_voice_reader_state_t *state)
{
    if (state->audio_cursor.neighbor_acquired)
        sample_voice_reader_diag_check_ref(state, &state->audio_cursor.neighbor_page_ref);
}


/* Cursor/page/loop handling and render kernels remain in their original sequence.
 * Private fragments share this translation unit to preserve static state and call order. */

#include "VoiceReader/sample_voice_reader_cursor.inc"

#include "VoiceReader/sample_voice_reader_kernels.inc"

#include "VoiceReader/sample_voice_reader_multi_kernels.inc"
