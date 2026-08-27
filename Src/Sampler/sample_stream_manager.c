#include "Sampler/sample_stream_manager.h"
#include "Sampler/sample_stream_needs.h"

#include <stddef.h>
#include <string.h>

#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_stream_io.h"
#include "Sampler/sample_stream_publish.h"
#include "Sampler/sample_stream_scheduler.h"
#include "Sampler/sample_stream_transport.h"
#include "Storage/memory_layout.h"
#include "stm32h7xx_hal.h"

#define SAMPLE_STREAM_CANCEL_REASON_RELEASE_KEY (3U)
#define SAMPLE_STREAM_CANCEL_REASON_SUPERSEDED (6U)

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(SAMPLE_CACHE_HOT_SAMPLE_CAPACITY <= SAMPLE_PAGE_CACHE_ID_CAPACITY,
               "stream manager hot scan range must fit in page-cache ids");
_Static_assert(SAMPLE_STREAM_IO_MAX_READERS <= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY,
               "active stream readers must be bounded below hot sample capacity");
#endif
static uint8_t g_sample_stream_manager_initialized;
typedef struct
{
    sample_stream_scheduler_candidate_t candidate;
    sample_page_load_target_t target;
    sample_stream_io_command_t command;
    uint32_t transport_sequence;
    uint8_t active;
} sample_stream_manager_pending_io_t;
SDRAM_STREAM_SERVICE static sample_stream_manager_pending_io_t
    g_sample_stream_manager_pending_io[2];
static uint8_t g_sample_stream_manager_pending_count;
static uint32_t sample_stream_manager_collect_candidates(
    sample_stream_scheduler_candidate_t *out_candidates,
    uint32_t capacity,
    uint32_t *out_loadable_needs,
    uint32_t *out_loading_needs);
static uint8_t sample_stream_manager_finish_io(
    sample_stream_manager_pending_io_t *pending,
    sample_stream_io_result_t *io_result);
static uint8_t sample_stream_manager_submit_classic_prefill(void);
static void sample_stream_manager_init_storage_once(void)
{
    if (g_sample_stream_manager_initialized != 0U)
    {
        return;
    }

    sample_stream_io_init();
    sample_stream_transport_init();
    g_sample_stream_manager_initialized = 1U;
}

void sample_stream_manager_init(void)
{
    sample_stream_manager_init_storage_once();
    sample_stream_manager_reset();
}

void sample_stream_manager_reset(void)
{
    sample_stream_manager_init_storage_once();
    sample_stream_io_reset();
    sample_stream_transport_init();
    memset(g_sample_stream_manager_pending_io, 0,
           sizeof(g_sample_stream_manager_pending_io));
    g_sample_stream_manager_pending_count = 0U;
    sample_stream_scheduler_init();
    sample_stream_needs_registry_reset();
    sample_stream_snapshot_registry_reset();
}

void sample_stream_manager_release_sample(uint16_t sample_id)
{
    sample_stream_manager_release_key(sample_audio_key_classic(sample_id));
}

void sample_stream_manager_release_key(sample_audio_key_t key)
{
    if (sample_stream_needs_registry_contains_key(key) != 0U)
    {
        return;
    }
    (void)sample_stream_transport_request_release(key);
    (void)sample_page_cache_cancel_reserved_key(key, SAMPLE_STREAM_CANCEL_REASON_RELEASE_KEY);
}

static uint8_t sample_stream_manager_finish_io(
    sample_stream_manager_pending_io_t *pending,
    sample_stream_io_result_t *io_result)
{
    if ((pending == 0) || (io_result == 0))
    {
        return 0U;
    }
    if (io_result->load_result != SAMPLE_PAGE_LOAD_OK)
    {
        (void)sample_stream_publish_result(io_result);
        return 0U;
    }
    if (sample_stream_publish_result(io_result) == 0U)
    {
        return 0U;
    }
    return 1U;
}

static uint32_t sample_stream_manager_collect_candidates(
    sample_stream_scheduler_candidate_t *out_candidates,
    uint32_t capacity,
    uint32_t *out_loadable_needs,
    uint32_t *out_loading_needs)
{
    if ((out_candidates == 0) || (capacity == 0U))
    {
        return 0U;
    }

    if (out_loadable_needs != 0)
    {
        *out_loadable_needs = 0U;
    }
    if (out_loading_needs != 0)
    {
        *out_loading_needs = 0U;
    }
    uint32_t count = 0U;
    for (uint8_t source = (uint8_t)SAMPLE_STREAM_SNAPSHOT_CLASSIC;
         source <= (uint8_t)SAMPLE_STREAM_SNAPSHOT_MULTI;
         ++source)
    {
        const uint8_t voice_capacity =
            (source == (uint8_t)SAMPLE_STREAM_SNAPSHOT_CLASSIC)
                ? (uint8_t)SAMPLE_STREAM_SNAPSHOT_CLASSIC_CAPACITY
                : (uint8_t)SAMPLE_STREAM_SNAPSHOT_MULTI_CAPACITY;
        for (uint8_t voice_id = 0U; voice_id < voice_capacity; ++voice_id)
        {
            if (count >= capacity)
            {
                return count;
            }

            sample_stream_target_voice_registry_entry_t entry;
            if (sample_stream_needs_registry_read(
                    (sample_stream_snapshot_source_t)source, voice_id, &entry) == 0U)
            {
                continue;
            }

            uint8_t first_missing_seen = 0U;
            for (uint8_t need_index = 0U;
                 need_index < entry.need_count;
                 ++need_index)
            {
                const sample_stream_target_voice_need_t *const need = &entry.needs[need_index];
                if (need->valid == 0U)
                {
                    continue;
                }
                const sample_page_state_t state = sample_page_cache_get_page_state_key(
                    need->key, need->page_index);
                if (state == SAMPLE_PAGE_READY)
                {
                    continue;
                }
                if (first_missing_seen == 0U)
                {
                    first_missing_seen = 1U;
                    if (state == SAMPLE_PAGE_LOADING)
                    {
                        if (out_loading_needs != 0)
                        {
                            (*out_loading_needs)++;
                        }
                    }
                    else if (out_loadable_needs != 0)
                    {
                        (*out_loadable_needs)++;
                    }
                }
                if (state == SAMPLE_PAGE_LOADING)
                {
                    break;
                }

                sample_stream_scheduler_candidate_t *const candidate =
                    &out_candidates[count++];
                memset(candidate, 0, sizeof(*candidate));
                candidate->key = need->key;
                candidate->page_index = need->page_index;
                candidate->registration_epoch = need->registration_epoch;
                candidate->voice_generation = entry.generation;
                candidate->diagnostic_deadline_audio_frame =
                    need->consume_deadline_audio_frame;
                candidate->source = source;
                candidate->voice_id = voice_id;
                candidate->need_index = need_index;
                candidate->round_robin_slot =
                    (source == (uint8_t)SAMPLE_STREAM_SNAPSHOT_CLASSIC)
                        ? voice_id
                        : (uint8_t)(SAMPLE_STREAM_SNAPSHOT_CLASSIC_CAPACITY + voice_id);
                candidate->active = 1U;
                break;
            }
        }
    }
    if ((count == 0U) && (out_loading_needs != 0) && (*out_loading_needs != 0U)
        && (out_loadable_needs != 0) && (*out_loadable_needs == 0U))
    {
        /* The caller records the exact no-selection reason. */
    }
    return count;
}

static uint8_t sample_stream_manager_pick_next(
    sample_page_load_target_t *out_target,
    sample_stream_scheduler_candidate_t *out_candidate)
{
    if ((out_target == 0) || (out_candidate == 0))
    {
        return 0U;
    }

    sample_stream_scheduler_candidate_t candidates[SAMPLE_STREAM_SCHEDULER_MAX_CANDIDATES];
    const uint32_t candidate_count = sample_stream_manager_collect_candidates(
        candidates,
        SAMPLE_STREAM_SCHEDULER_MAX_CANDIDATES,
        0,
        0);
    if (candidate_count == 0U)
    {
        return 0U;
    }

    sample_stream_scheduler_decision_t decision;
    if (sample_stream_scheduler_pick(candidates, candidate_count, &decision) == 0U)
    {
        return 0U;
    }
    sample_stream_scheduler_candidate_t *const candidate =
        &candidates[decision.candidate_index];
    const sample_page_state_t state = sample_page_cache_get_page_state_key(
        candidate->key, candidate->page_index);
    uint8_t reserved_here = 0U;
    if ((state == SAMPLE_PAGE_FREE) || (state == SAMPLE_PAGE_FAILED))
    {
        if (sample_page_cache_reserve_page_key_alloc(
                candidate->key,
                candidate->page_index,
                SAMPLE_PAGE_ALLOC_VOICE_WINDOW) == 0U)
        {
            return 0U;
        }
        reserved_here = 1U;
    }

    sample_page_load_target_t target;
    if (sample_page_cache_get_load_target_key(candidate->key,
                                              candidate->page_index,
                                              &target) == 0U)
    {
        if (reserved_here != 0U)
        {
            (void)sample_page_cache_cancel_reserved_page_key(
                candidate->key,
                candidate->page_index,
                SAMPLE_STREAM_CANCEL_REASON_SUPERSEDED);
        }
        return 0U;
    }
    if ((candidate->registration_epoch != 0U)
        && (target.registration_epoch != candidate->registration_epoch))
    {
        (void)sample_page_cache_cancel_reserved_page_key(
            candidate->key,
            candidate->page_index,
            SAMPLE_STREAM_CANCEL_REASON_SUPERSEDED);
        return 0U;
    }
    *out_candidate = *candidate;
    *out_target = target;
    return 1U;
}

static uint8_t sample_stream_manager_submit_classic_prefill(void)
{
    sample_page_load_target_t target;
    if ((g_sample_stream_manager_pending_count >= 2U)
        || (sample_page_cache_get_reserved_load_target_domain_range(
                SAMPLE_AUDIO_DOMAIN_CLASSIC, 0U,
                SAMPLE_CACHE_HOT_SAMPLE_CAPACITY, &target) == 0U))
    {
        return 0U;
    }
    sample_page_stream_info_t stream_info;
    if ((sample_page_cache_get_stream_info_key(target.key, &stream_info) == 0U)
        || (sample_audio_key_equal(&target.key, &stream_info.key) == 0U)
        || (target.format != stream_info.format)
        || (target.stride_floats != stream_info.stride_floats)
        || (target.frames_per_page != stream_info.frames_per_page)
        || ((target.registration_epoch != 0U)
            && (target.registration_epoch != stream_info.registration_epoch)))
    {
        (void)sample_page_cache_set_page_state_key(
            target.key, target.page_index, SAMPLE_PAGE_FAILED);
        return 0U;
    }
    sample_page_load_token_t token;
    if (sample_page_cache_begin_loading(&target, &token) == 0U)
    {
        return 0U;
    }
    sample_stream_io_command_t command;
    if (sample_stream_io_command_init(&command, &token, &target,
                                      &stream_info) == 0U)
    {
        (void)sample_page_cache_finish_loading(
            &token, SAMPLE_PAGE_FINISH_ERROR);
        return 0U;
    }
    command.deadline_margin_us = UINT32_MAX;
    sample_stream_manager_pending_io_t *const pending =
        &g_sample_stream_manager_pending_io[g_sample_stream_manager_pending_count];
    memset(pending, 0, sizeof(*pending));
    pending->target = target;
    pending->command = command;
    if (sample_stream_transport_submit(
            &pending->command, &pending->transport_sequence) == 0U)
    {
        (void)sample_page_cache_finish_loading(
            &token, SAMPLE_PAGE_FINISH_ERROR);
        return 0U;
    }
    pending->active = 1U;
    ++g_sample_stream_manager_pending_count;
    return 1U;
}

void sample_stream_manager_service(uint32_t byte_budget)
{
    if (byte_budget == 0U)
    {
        return;
    }

    uint32_t pages_this_call = 0U;

    if (g_sample_stream_manager_pending_count != 0U)
    {
        sample_stream_io_result_t pending_result;
        if (sample_stream_transport_take_result(
                g_sample_stream_manager_pending_io[0].transport_sequence,
                &pending_result) != 0U)
        {
            const uint8_t finished = sample_stream_manager_finish_io(
                &g_sample_stream_manager_pending_io[0], &pending_result);
            if (g_sample_stream_manager_pending_count > 1U)
            {
                g_sample_stream_manager_pending_io[0] =
                    g_sample_stream_manager_pending_io[1];
            }
            memset(&g_sample_stream_manager_pending_io[
                       g_sample_stream_manager_pending_count - 1U],
                   0, sizeof(g_sample_stream_manager_pending_io[0]));
            --g_sample_stream_manager_pending_count;
            pages_this_call = (finished != 0U) ? 1U : 0U;
            return;
        }
        if (g_sample_stream_manager_pending_count >= 2U)
        {
            return;
        }
    }

    sample_stream_scheduler_begin_round();

    for (;;)
    {
        sample_page_load_target_t target;
        sample_page_stream_info_t stream_info;
        sample_stream_scheduler_candidate_t candidate;
        if (sample_stream_manager_pick_next(&target, &candidate) == 0U)
        {
            break;
        }

        if (sample_page_cache_get_stream_info_key(target.key, &stream_info) == 0U)
        {
            (void)sample_page_cache_set_page_state_key(target.key,
                                                   target.page_index,
                                                   SAMPLE_PAGE_FAILED);
            return;
        }

        if ((sample_audio_key_equal(&target.key, &stream_info.key) == 0U)
            || (target.format != stream_info.format)
            || (target.stride_floats != stream_info.stride_floats)
            || (target.frames_per_page != stream_info.frames_per_page)
            || ((target.registration_epoch != 0U)
                && (target.registration_epoch != stream_info.registration_epoch)))
        {
            (void)sample_page_cache_set_page_state_key(target.key,
                                                       target.page_index,
                                                       SAMPLE_PAGE_FAILED);
            return;
        }

        sample_page_load_token_t load_token;
        uint32_t consumed = target.frame_count * stream_info.info.block_align;
        const sample_stream_audio_frame_t selected_frame = sample_stream_time_now();
        const uint64_t remaining_64 =
            (candidate.diagnostic_deadline_audio_frame > selected_frame)
                ? (candidate.diagnostic_deadline_audio_frame - selected_frame)
                : 0U;
        const uint32_t remaining_frames = (remaining_64 > UINT32_MAX)
                                              ? UINT32_MAX
                                              : (uint32_t)remaining_64;
        if (sample_page_cache_begin_loading(&target, &load_token) == 0U)
        {
            continue;
        }
        sample_stream_io_command_t io_command;
        if (sample_stream_io_command_init(&io_command,
                                          &load_token,
                                          &target,
                                          &stream_info) == 0U)
        {
            (void)sample_page_cache_finish_loading(&load_token,
                                                   SAMPLE_PAGE_FINISH_ERROR);
            continue;
        }
        io_command.deadline_margin_us = (remaining_frames >= 206159U)
            ? UINT32_MAX
            : (uint32_t)(((uint64_t)remaining_frames * 1000000ULL) / 48000ULL);
        sample_stream_manager_pending_io_t *pending =
            &g_sample_stream_manager_pending_io[g_sample_stream_manager_pending_count];
        memset(pending, 0, sizeof(*pending));
        pending->candidate = candidate;
        pending->target = target;
        pending->command = io_command;
        sample_stream_io_result_t io_result;
        memset(&io_result, 0, sizeof(io_result));
        io_result.token = load_token;
        io_result.load_result = SAMPLE_PAGE_LOAD_INVALID_ARG;
        if (sample_stream_transport_submit(
                &pending->command, &pending->transport_sequence) == 0U)
        {
            (void)sample_stream_manager_finish_io(pending, &io_result);
            return;
        }
        pending->active = 1U;
        ++g_sample_stream_manager_pending_count;
        if (sample_stream_transport_take_result(
                g_sample_stream_manager_pending_io[0].transport_sequence,
                &io_result) == 0U)
        {
            return;
        }
        sample_stream_manager_pending_io_t completed_pending =
            g_sample_stream_manager_pending_io[0];
        if (g_sample_stream_manager_pending_count > 1U)
        {
            g_sample_stream_manager_pending_io[0] =
                g_sample_stream_manager_pending_io[1];
        }
        --g_sample_stream_manager_pending_count;
        memset(&g_sample_stream_manager_pending_io[g_sample_stream_manager_pending_count],
               0, sizeof(g_sample_stream_manager_pending_io[0]));
        pending = &completed_pending;
        pending->active = 0U;
        if (io_result.read_bytes > consumed)
        {
            consumed = io_result.read_bytes;
        }
        if (sample_stream_manager_finish_io(pending, &io_result) == 0U)
        {
            return;
        }
        ++pages_this_call;

        if (consumed >= byte_budget)
        {
            break;
        }
        byte_budget -= consumed;

    }
    (void)sample_stream_manager_submit_classic_prefill();
}

uint8_t sample_stream_manager_has_pending_sd_work(void)
{
    if (g_sample_stream_manager_pending_count != 0U)
    {
        return 1U;
    }
    sample_stream_scheduler_candidate_t candidates[SAMPLE_STREAM_SCHEDULER_MAX_CANDIDATES];
    uint32_t loading_needs = 0U;
    const uint32_t candidate_count = sample_stream_manager_collect_candidates(
        candidates, SAMPLE_STREAM_SCHEDULER_MAX_CANDIDATES, 0, &loading_needs);
    return ((candidate_count != 0U) || (loading_needs != 0U)) ? 1U : 0U;
}

uint8_t sample_stream_manager_io_in_flight(void)
{
    return (g_sample_stream_manager_pending_count != 0U) ? 1U : 0U;
}
