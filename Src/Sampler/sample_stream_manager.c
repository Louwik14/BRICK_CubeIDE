#include "Sampler/sample_stream_manager.h"
#include "Sampler/sample_stream_needs.h"

#include <stddef.h>
#include <string.h>

#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_multi_stream_diag.h"
#include "Sampler/sample_stream_admission.h"
#include "Sampler/sample_stream_benchmark.h"
#include "Sampler/sample_stream_io.h"
#include "Sampler/sample_stream_publish.h"
#include "Sampler/sample_stream_scheduler.h"
#include "Sampler/sample_stream_transport.h"
#include "Sampler/sample_stream_underrun_trace.h"
#include "Storage/memory_layout.h"
#include "stm32h7xx_hal.h"

#define SAMPLE_STREAM_SERVICE_MAX_PAGES (16U)
#define SAMPLE_STREAM_SERVICE_MAX_FATFS_OPS (16U)
#define SAMPLE_STREAM_SERVICE_MAX_TICKS (2U)
#define SAMPLE_STREAM_CANCEL_REASON_RELEASE_KEY (3U)
#define SAMPLE_STREAM_CANCEL_REASON_SUPERSEDED (6U)
#define SAMPLE_STREAM_CRITICAL_ADVANCE_FRAMES (1024U)

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(SAMPLE_CACHE_HOT_SAMPLE_CAPACITY <= SAMPLE_PAGE_CACHE_ID_CAPACITY,
               "stream manager hot scan range must fit in page-cache ids");
_Static_assert(SAMPLE_STREAM_MAX_ACTIVE <= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY,
               "active stream readers must be bounded below hot sample capacity");
#endif
static uint32_t g_sample_stream_service_fatfs_ops;
static uint8_t g_sample_stream_manager_initialized;
static uint32_t sample_stream_manager_collect_candidates(
    sample_stream_scheduler_candidate_t *out_candidates,
    uint32_t capacity,
    uint32_t *out_critical_voices,
    uint32_t *out_loadable_needs,
    uint32_t *out_loading_needs);
static uint8_t sample_stream_manager_candidate_source(
    const sample_stream_scheduler_candidate_t *candidate);
static uint8_t sample_stream_manager_entry_has_critical_advance(
    const sample_stream_target_voice_registry_entry_t *entry,
    sample_stream_audio_frame_t now);
#if defined(BRICK6_MULTI_STREAM_DIAG)
SDRAM_STREAM_SERVICE volatile sample_multi_stream_diag_snapshot_t g_sample_multi_stream_diag;
volatile uint32_t g_sample_multi_stream_diag_frozen;
static volatile uint32_t g_sample_multi_stream_diag_breakpoint_seen;
#endif


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
    sample_stream_scheduler_init();
    sample_stream_event_trace_reset();
    brick6_stream_underrun_trace_reset();
    sample_stream_admission_init(0);
    sample_stream_needs_registry_reset();
    sample_stream_snapshot_registry_reset();
#if BRICK6_STREAM_BENCH
    sample_stream_benchmark_reset();
#endif
}

void sample_stream_manager_trace_consume_miss(sample_audio_key_t key,
                                              uint32_t page_index,
                                              uint32_t reader_position,
                                              uint32_t frames_remaining)
{
    (void)sample_stream_event_trace_record_miss(key,
                                                page_index,
                                                reader_position,
                                                frames_remaining);
    brick6_stream_underrun_trace_consume_miss(
        key, page_index, reader_position, frames_remaining);
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
    sample_stream_io_release_key(key);
    (void)sample_page_cache_cancel_reserved_key(key, SAMPLE_STREAM_CANCEL_REASON_RELEASE_KEY);
}

static uint8_t sample_stream_manager_candidate_source(
    const sample_stream_scheduler_candidate_t *candidate)
{
    if (candidate == 0)
    {
        return UINT8_MAX;
    }
    return candidate->source;
}

static uint32_t sample_stream_manager_collect_candidates(
    sample_stream_scheduler_candidate_t *out_candidates,
    uint32_t capacity,
    uint32_t *out_critical_voices,
    uint32_t *out_loadable_needs,
    uint32_t *out_loading_needs)
{
    if ((out_candidates == 0) || (capacity == 0U))
    {
        return 0U;
    }

    if (out_critical_voices != 0)
    {
        *out_critical_voices = 0U;
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
    const sample_stream_audio_frame_t now = sample_stream_time_now();
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

            if ((out_critical_voices != 0)
                && (sample_stream_manager_entry_has_critical_advance(&entry, now) != 0U))
            {
                (*out_critical_voices)++;
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
                candidate->consume_deadline_audio_frame = need->consume_deadline_audio_frame;
                candidate->source = source;
                candidate->voice_id = voice_id;
                candidate->advance = need_index;
                candidate->round_robin_slot =
                    (source == (uint8_t)SAMPLE_STREAM_SNAPSHOT_CLASSIC)
                        ? voice_id
                        : (uint8_t)(SAMPLE_STREAM_SNAPSHOT_CLASSIC_CAPACITY + voice_id);
                candidate->active = 1U;
                brick6_stream_underrun_trace_need_selectable(
                    candidate, state, count + 1U);
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

static void sample_stream_manager_trace_voice_states(void)
{
#if BRICK6_STREAM_UNDERRUN_TRACE
    for (uint8_t source = (uint8_t)SAMPLE_STREAM_SNAPSHOT_CLASSIC;
         source <= (uint8_t)SAMPLE_STREAM_SNAPSHOT_MULTI;
         ++source)
    {
        const uint8_t capacity = (source == (uint8_t)SAMPLE_STREAM_SNAPSHOT_CLASSIC)
                                     ? SAMPLE_STREAM_SNAPSHOT_CLASSIC_CAPACITY
                                     : SAMPLE_STREAM_SNAPSHOT_MULTI_CAPACITY;
        for (uint8_t voice_id = 0U; voice_id < capacity; ++voice_id)
        {
            sample_stream_snapshot_t snapshot;
            sample_stream_target_voice_registry_entry_t entry;
            if ((sample_stream_snapshot_read(
                     (sample_stream_snapshot_source_t)source, voice_id, &snapshot) != 0U)
                && (sample_stream_needs_registry_read(
                        (sample_stream_snapshot_source_t)source, voice_id, &entry) != 0U))
            {
                brick6_stream_underrun_trace_voice_state(
                    (sample_stream_snapshot_source_t)source,
                    voice_id,
                    &snapshot,
                    &entry);
            }
        }
    }
#endif
}

static uint8_t sample_stream_manager_pick_next(
    sample_page_load_target_t *out_target,
    sample_stream_scheduler_candidate_t *out_candidate,
    sample_stream_scheduler_decision_t *out_decision)
{
    if ((out_target == 0) || (out_candidate == 0) || (out_decision == 0))
    {
        return 0U;
    }

    sample_stream_scheduler_candidate_t candidates[SAMPLE_STREAM_SCHEDULER_MAX_CANDIDATES];
    for (uint32_t attempt = 0U; attempt < SAMPLE_STREAM_SCHEDULER_MAX_CANDIDATES; ++attempt)
    {
        uint32_t critical_voices = 0U;
        uint32_t loadable_needs = 0U;
        uint32_t loading_needs = 0U;
        const uint32_t candidate_count = sample_stream_manager_collect_candidates(
            candidates,
            SAMPLE_STREAM_SCHEDULER_MAX_CANDIDATES,
            &critical_voices,
            &loadable_needs,
            &loading_needs);
        if (candidate_count == 0U)
        {
            const uint8_t reason = (loadable_needs == 0U)
                                       ? ((loading_needs != 0U)
                                              ? BRICK6_STREAM_TRACE_REASON_ALL_LOADING
                                              : BRICK6_STREAM_TRACE_REASON_ALL_READY)
                                       : BRICK6_STREAM_TRACE_REASON_NO_CANDIDATE;
            brick6_stream_underrun_trace_scheduler(
                0, 0, 0U, critical_voices, loadable_needs, reason);
            return 0U;
        }

        sample_stream_scheduler_decision_t decision;
        if (sample_stream_scheduler_pick(candidates, candidate_count, &decision) == 0U)
        {
            brick6_stream_underrun_trace_scheduler(
                0, 0, candidate_count, critical_voices, loadable_needs,
                BRICK6_STREAM_TRACE_REASON_NO_CANDIDATE);
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
                brick6_stream_underrun_trace_scheduler(
                    candidate, 0, candidate_count, critical_voices, loadable_needs,
                    BRICK6_STREAM_TRACE_REASON_RESERVE_FAILED);
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
            brick6_stream_underrun_trace_scheduler(
                candidate, 0, candidate_count, critical_voices, loadable_needs,
                BRICK6_STREAM_TRACE_REASON_TARGET_FAILED);
            return 0U;
        }
        if ((candidate->registration_epoch != 0U)
            && (target.registration_epoch != candidate->registration_epoch))
        {
            (void)sample_page_cache_cancel_reserved_page_key(
                candidate->key,
                candidate->page_index,
                SAMPLE_STREAM_CANCEL_REASON_SUPERSEDED);
            brick6_stream_underrun_trace_scheduler(
                candidate, 0, candidate_count, critical_voices, loadable_needs,
                BRICK6_STREAM_TRACE_REASON_EPOCH_MISMATCH);
            return 0U;
        }
        brick6_stream_underrun_trace_scheduler(
            candidate, &decision, candidate_count, critical_voices, loadable_needs,
            BRICK6_STREAM_TRACE_REASON_NONE);
        *out_candidate = *candidate;
        *out_decision = decision;
        *out_target = target;
        return 1U;
    }
    return 0U;
}

void sample_stream_manager_service(uint32_t byte_budget)
{
    if (byte_budget == 0U)
    {
        brick6_stream_underrun_trace_manager_end(
            0U, 0U, BRICK6_STREAM_TRACE_REASON_ZERO_BUDGET);
        return;
    }

    const uint32_t start_tick = HAL_GetTick();
#if BRICK6_STREAM_BENCH
    const uint32_t benchmark_service_begin_cycle = DWT->CYCCNT;
#endif
    uint32_t pages_this_call = 0U;
    g_sample_stream_service_fatfs_ops = 0U;
    uint8_t service_exit_reason = BRICK6_STREAM_TRACE_REASON_NO_CANDIDATE;
    sample_stream_manager_trace_voice_states();
    const uint32_t service_sequence = sample_stream_event_trace_record(
        SAMPLE_STREAM_EVENT_SERVICE_BEGIN,
        (sample_audio_key_t){ 0U, 0U },
        UINT32_MAX,
        0U,
        UINT8_MAX,
        0U,
        0U,
        byte_budget,
        sample_stream_needs_registry_count_active(),
        0U);

    for (;;)
    {
        sample_page_load_target_t target;
        sample_page_stream_info_t stream_info;
        sample_stream_scheduler_candidate_t candidate;
        sample_stream_scheduler_decision_t decision;
        if (sample_stream_manager_pick_next(&target, &candidate, &decision) == 0U)
        {
            service_exit_reason = BRICK6_STREAM_TRACE_REASON_NO_CANDIDATE;
            break;
        }

        if (sample_page_cache_get_stream_info_key(target.key, &stream_info) == 0U)
        {
            (void)sample_page_cache_set_page_state_key(target.key,
                                                   target.page_index,
                                                   SAMPLE_PAGE_FAILED);
            brick6_stream_underrun_trace_manager_end(
                pages_this_call, g_sample_stream_service_fatfs_ops,
                BRICK6_STREAM_TRACE_REASON_LOAD_ERROR);
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
            brick6_stream_underrun_trace_manager_end(
                pages_this_call, g_sample_stream_service_fatfs_ops,
                BRICK6_STREAM_TRACE_REASON_EPOCH_MISMATCH);
            return;
        }

        sample_page_load_token_t load_token;
        uint32_t delivered_pages = 1U;
        uint32_t consumed = target.frame_count * stream_info.info.block_align;
        const sample_stream_audio_frame_t selected_frame = sample_stream_time_now();
        const uint64_t remaining_64 =
            (decision.consume_deadline_audio_frame > selected_frame)
                ? (decision.consume_deadline_audio_frame - selected_frame)
                : 0U;
        const uint32_t remaining_frames = (remaining_64 > UINT32_MAX)
                                              ? UINT32_MAX
                                              : (uint32_t)remaining_64;
        const uint32_t selected_sequence = sample_stream_event_trace_record(
        SAMPLE_STREAM_EVENT_SELECT,
            target.key,
            target.page_index,
            sample_stream_manager_candidate_source(&candidate),
            candidate.voice_id,
            candidate.voice_generation,
            service_sequence,
            candidate.advance,
            remaining_frames,
            0U);
        if (sample_page_cache_begin_loading(&target, &load_token) == 0U)
        {
            continue;
        }
        brick6_stream_underrun_trace_load_begin(&candidate, &target, &load_token);
        const uint32_t io_begin_sequence = sample_stream_event_trace_record(
            SAMPLE_STREAM_EVENT_LOAD_BEGIN,
            target.key,
            target.page_index,
            sample_stream_manager_candidate_source(&candidate),
            candidate.voice_id,
            candidate.voice_generation,
            selected_sequence,
            target.frame_count,
            target.page_generation,
            0U);
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
        sample_stream_io_result_t io_result;
        brick6_stream_underrun_trace_io_begin(&candidate, &io_command);
#if BRICK6_STREAM_BENCH
        const uint32_t benchmark_io_begin_cycle = DWT->CYCCNT;
#endif
        sample_stream_transport_execute_monocore(&io_command, &io_result);
        brick6_stream_underrun_trace_io_end(
            &candidate, &io_command, &io_result, 0U);
#if BRICK6_STREAM_BENCH
        uint32_t benchmark_backlog = 0U;
        benchmark_backlog = sample_stream_needs_registry_count_active();
        sample_stream_benchmark_note_io(
            target.key,
            &io_result,
            DWT->CYCCNT - benchmark_io_begin_cycle,
            0U,
            benchmark_backlog,
            (sample_stream_time_now() > decision.consume_deadline_audio_frame) ? 1U : 0U);
#endif
        if (io_result.read_bytes > consumed)
        {
            consumed = io_result.read_bytes;
        }
        g_sample_stream_service_fatfs_ops += io_result.fatfs_ops;

        if (io_result.load_result != SAMPLE_PAGE_LOAD_OK)
        {
            brick6_stream_underrun_trace_load_end(
                &candidate, &io_result, BRICK6_STREAM_TRACE_REASON_LOAD_ERROR);
            (void)sample_stream_event_trace_record(
                SAMPLE_STREAM_EVENT_LOAD_ERROR,
                target.key,
                target.page_index,
                sample_stream_manager_candidate_source(&candidate),
                candidate.voice_id,
                candidate.voice_generation,
                io_begin_sequence,
                io_result.backend,
                io_result.read_bytes,
                (uint8_t)io_result.load_result);
            (void)sample_stream_publish_result(&io_result);
            brick6_stream_underrun_trace_manager_end(
                pages_this_call, g_sample_stream_service_fatfs_ops,
                BRICK6_STREAM_TRACE_REASON_LOAD_ERROR);
            return;
        }

        if (sample_stream_publish_result(&io_result) == 0U)
        {
            (void)sample_stream_event_trace_record(
                SAMPLE_STREAM_EVENT_LOAD_ERROR,
                target.key,
                target.page_index,
                sample_stream_manager_candidate_source(&candidate),
                candidate.voice_id,
                candidate.voice_generation,
                io_begin_sequence,
                io_result.backend,
                io_result.read_bytes,
                (uint8_t)SAMPLE_PAGE_LOAD_DECODE_FAILED);
            brick6_stream_underrun_trace_load_end(
                &candidate, &io_result, BRICK6_STREAM_TRACE_REASON_PUBLISH_ERROR);
            brick6_stream_underrun_trace_manager_end(
                pages_this_call, g_sample_stream_service_fatfs_ops,
                BRICK6_STREAM_TRACE_REASON_PUBLISH_ERROR);
            return;
        }
        brick6_stream_underrun_trace_load_end(
            &candidate, &io_result, BRICK6_STREAM_TRACE_REASON_NONE);
        brick6_stream_underrun_trace_ready(&candidate, &target, &io_result);
        (void)sample_stream_event_trace_record(
            SAMPLE_STREAM_EVENT_LOAD_END,
            target.key,
            target.page_index,
            sample_stream_manager_candidate_source(&candidate),
            candidate.voice_id,
            candidate.voice_generation,
            io_begin_sequence,
            io_result.backend,
            io_result.read_bytes,
            (uint8_t)io_result.load_result);
        (void)sample_stream_event_trace_record(
            SAMPLE_STREAM_EVENT_READY,
            target.key,
            target.page_index,
            sample_stream_manager_candidate_source(&candidate),
            candidate.voice_id,
            candidate.voice_generation,
            io_begin_sequence,
            target.page_generation,
            io_result.read_bytes,
            0U);
        pages_this_call += delivered_pages;

        if (consumed >= byte_budget)
        {
            service_exit_reason = BRICK6_STREAM_TRACE_REASON_SERVICE_BYTE_BUDGET;
            break;
        }
        byte_budget -= consumed;

        const uint32_t elapsed_ticks = HAL_GetTick() - start_tick;
        if (pages_this_call >= SAMPLE_STREAM_SERVICE_MAX_PAGES)
        {
            service_exit_reason = BRICK6_STREAM_TRACE_REASON_SERVICE_PAGE_LIMIT;
            break;
        }
        if (g_sample_stream_service_fatfs_ops >= SAMPLE_STREAM_SERVICE_MAX_FATFS_OPS)
        {
            service_exit_reason = BRICK6_STREAM_TRACE_REASON_SERVICE_FATFS_LIMIT;
            break;
        }
        if (elapsed_ticks >= SAMPLE_STREAM_SERVICE_MAX_TICKS)
        {
            service_exit_reason = BRICK6_STREAM_TRACE_REASON_SERVICE_TICK_LIMIT;
            break;
        }
    }
    brick6_stream_underrun_trace_manager_end(
        pages_this_call, g_sample_stream_service_fatfs_ops, service_exit_reason);
    (void)sample_stream_event_trace_record(
        SAMPLE_STREAM_EVENT_SERVICE_END,
        (sample_audio_key_t){ 0U, 0U },
        UINT32_MAX,
        0U,
        UINT8_MAX,
        0U,
        service_sequence,
        pages_this_call,
        g_sample_stream_service_fatfs_ops,
        0U);
#if BRICK6_STREAM_BENCH
    sample_stream_benchmark_note_service(
        pages_this_call, DWT->CYCCNT - benchmark_service_begin_cycle);
#endif
}

void sample_stream_manager_note_blocked_poll(uint8_t multi_blocked,
                                             uint8_t bulk_blocked,
                                             uint32_t elapsed_frames)
{
    const uint32_t blocked = ((multi_blocked != 0U) ? 1U : 0U)
                             | ((bulk_blocked != 0U) ? 2U : 0U);
    (void)sample_stream_event_trace_record(
        SAMPLE_STREAM_EVENT_SERVICE_BLOCKED,
        (sample_audio_key_t){ 0U, 0U },
        UINT32_MAX,
        0U,
        UINT8_MAX,
        0U,
        0U,
        blocked,
        elapsed_frames,
        1U);
}

static uint8_t sample_stream_manager_entry_has_critical_advance(
    const sample_stream_target_voice_registry_entry_t *entry,
    sample_stream_audio_frame_t now)
{
    if ((entry == 0) || (entry->active == 0U))
    {
        return 0U;
    }

    for (uint8_t need_index = 0U; need_index < entry->need_count; ++need_index)
    {
        const sample_stream_target_voice_need_t *const need = &entry->needs[need_index];
        if ((need->valid == 0U)
            || (sample_page_cache_get_page_state_key(need->key, need->page_index)
                == SAMPLE_PAGE_READY))
        {
            continue;
        }

        /* A missing current page is critical regardless of its deadline. */
        if (need_index == 0U)
        {
            return 1U;
        }

        if (need->consume_deadline_audio_frame == SAMPLE_STREAM_AUDIO_FRAME_NEVER)
        {
            continue;
        }
        const uint64_t remaining = (need->consume_deadline_audio_frame > now)
                                       ? (need->consume_deadline_audio_frame - now)
                                       : 0U;
        if (remaining <= SAMPLE_STREAM_CRITICAL_ADVANCE_FRAMES)
        {
            return 1U;
        }
    }
    return 0U;
}

uint8_t sample_stream_manager_has_critical_advance(void)
{
    const sample_stream_audio_frame_t now = sample_stream_time_now();
    sample_stream_target_voice_registry_entry_t entry;
    for (uint8_t voice_id = 0U;
         voice_id < SAMPLE_STREAM_SNAPSHOT_CLASSIC_CAPACITY;
         ++voice_id)
    {
        if ((sample_stream_needs_registry_read(SAMPLE_STREAM_SNAPSHOT_CLASSIC,
                                               voice_id,
                                               &entry) != 0U)
            && (sample_stream_manager_entry_has_critical_advance(&entry, now) != 0U))
        {
            return 1U;
        }
    }
    for (uint8_t voice_id = 0U;
         voice_id < SAMPLE_STREAM_SNAPSHOT_MULTI_CAPACITY;
         ++voice_id)
    {
        if ((sample_stream_needs_registry_read(SAMPLE_STREAM_SNAPSHOT_MULTI,
                                               voice_id,
                                               &entry) != 0U)
            && (sample_stream_manager_entry_has_critical_advance(&entry, now) != 0U))
        {
            return 1U;
        }
    }
    return 0U;
}

uint8_t sample_stream_manager_has_pending_sd_work(void)
{
    sample_stream_scheduler_candidate_t candidates[SAMPLE_STREAM_SCHEDULER_MAX_CANDIDATES];
    return (sample_stream_manager_collect_candidates(
                candidates, SAMPLE_STREAM_SCHEDULER_MAX_CANDIDATES,
                0, 0, 0) != 0U)
               ? 1U
               : 0U;
}

#if defined(BRICK6_MULTI_STREAM_DIAG)
void sample_stream_manager_get_debug_stats(uint32_t *out_active_needs,
                                           uint32_t *out_readers_active)
{
    if (out_active_needs != 0)
    {
        *out_active_needs = sample_stream_needs_registry_count_active();
    }
    if (out_readers_active != 0)
    {
        *out_readers_active = sample_stream_io_active_reader_count();
    }
}

static void sample_multi_stream_diag_fill_pages(
    volatile sample_multi_stream_diag_page_t *out_pages,
    sample_audio_key_t key,
    uint32_t first_page,
    uint32_t page_count,
    uint32_t *out_acquired)
{
    uint32_t acquired = 0U;
    for (uint32_t i = 0U; i < SAMPLE_MULTI_STREAM_DIAG_MAX_WINDOW_PAGES; ++i)
    {
        memset((void *)&out_pages[i], 0, sizeof(out_pages[i]));
        out_pages[i].page_index = UINT32_MAX;
        if (i >= page_count)
        {
            continue;
        }

        sample_page_window_debug_t page;
        if (sample_page_cache_get_window_page_debug(key,
                                                    first_page + i,
                                                    &page) == 0U)
        {
            out_pages[i].page_index = first_page + i;
            continue;
        }
        out_pages[i].page_index = page.page_index;
        out_pages[i].generation = page.generation;
        out_pages[i].slot_index = page.slot_index;
        out_pages[i].frame_count = page.frame_count;
        out_pages[i].use_count = page.use_count;
        out_pages[i].state = (uint8_t)page.state;
        if (page.state != SAMPLE_PAGE_FREE)
        {
            acquired++;
        }
    }
    if (out_acquired != 0)
    {
        *out_acquired = acquired;
    }
}

__attribute__((noinline, used, externally_visible))
void sample_multi_stream_diag_capture_failure(
    sample_audio_key_t key,
    uint16_t sample_id,
    uint8_t voice_index,
    uint8_t voice_active,
    uint8_t reader_active,
    uint8_t source_kind,
    uint32_t voice_generation,
    sample_audio_format_t format,
    uint32_t position_frame,
    uint32_t current_frame,
    uint32_t loop_frame,
    uint32_t failure_result,
    sample_multi_stream_diag_code_t code)
{
    if (g_sample_multi_stream_diag_frozen != 0U)
    {
        return;
    }

    memset((void *)&g_sample_multi_stream_diag, 0, sizeof(g_sample_multi_stream_diag));
    g_sample_multi_stream_diag.magic = SAMPLE_MULTI_STREAM_DIAG_MAGIC;
    g_sample_multi_stream_diag.code = (uint32_t)code;
    g_sample_multi_stream_diag.failure_result = failure_result;
    g_sample_multi_stream_diag.sample_id = sample_id;
    g_sample_multi_stream_diag.key_domain = key.domain;
    g_sample_multi_stream_diag.key_object_id = key.object_id;
    g_sample_multi_stream_diag.voice_index = voice_index;
    g_sample_multi_stream_diag.voice_active = voice_active;
    g_sample_multi_stream_diag.reader_active = reader_active;
    g_sample_multi_stream_diag.source_kind = source_kind;
    g_sample_multi_stream_diag.voice_generation = voice_generation;
    g_sample_multi_stream_diag.format = (uint32_t)sample_audio_format_or_stereo(format);
    g_sample_multi_stream_diag.position_frame = position_frame;
    g_sample_multi_stream_diag.current_page = UINT32_MAX;
    g_sample_multi_stream_diag.loop_page = UINT32_MAX;

    const sample_audio_format_t safe_format = sample_audio_format_or_stereo(format);
    const uint32_t window_pages = sample_audio_format_window_pages(safe_format);
    uint32_t acquired = 0U;
    if (current_frame != UINT32_MAX)
    {
        g_sample_multi_stream_diag.current_page =
            sample_audio_format_page_index_from_frame(safe_format, current_frame);
        g_sample_multi_stream_diag.current_expected_pages = window_pages;
        sample_multi_stream_diag_fill_pages(g_sample_multi_stream_diag.current_pages,
                                             key,
                                             g_sample_multi_stream_diag.current_page,
                                             window_pages,
                                             &acquired);
        g_sample_multi_stream_diag.current_acquired_pages = acquired;
    }
    if (loop_frame != UINT32_MAX)
    {
        g_sample_multi_stream_diag.loop_page =
            sample_audio_format_page_index_from_frame(safe_format, loop_frame);
        g_sample_multi_stream_diag.loop_expected_pages = window_pages;
        sample_multi_stream_diag_fill_pages(g_sample_multi_stream_diag.loop_pages,
                                             key,
                                             g_sample_multi_stream_diag.loop_page,
                                             window_pages,
                                             &acquired);
        g_sample_multi_stream_diag.loop_acquired_pages = acquired;
    }

    uint32_t active_needs = 0U;
    uint32_t readers_active = 0U;
    sample_stream_manager_get_debug_stats(&active_needs,
                                           &readers_active);
    g_sample_multi_stream_diag.readers_active = readers_active;
    g_sample_multi_stream_diag.active_needs = active_needs;
    g_sample_multi_stream_diag.pages_free = sample_page_cache_debug_count_free_pages();
    g_sample_multi_stream_diag.pc = (uint32_t)(uintptr_t)__builtin_return_address(0);
    uintptr_t link_register = 0U;
    __asm volatile("mov %0, lr" : "=r"(link_register));
    g_sample_multi_stream_diag.lr = (uint32_t)link_register;
    g_sample_multi_stream_diag.frozen = 1U;
    __DMB();
    g_sample_multi_stream_diag_frozen = 1U;
}

__attribute__((noinline, used, externally_visible))
void sample_multi_stream_diag_capture_fault(const uint32_t *stack_pointer,
                                            uint32_t exc_return,
                                            uint32_t fault_type)
{
    const uint32_t extended_words = ((exc_return & (1UL << 4U)) == 0U) ? 18U : 0U;
    const uintptr_t begin = (uintptr_t)stack_pointer;
    const uintptr_t end = begin + ((uintptr_t)(extended_words + 8U) * sizeof(uint32_t));

    g_sample_multi_stream_diag.fault_type = fault_type;
    g_sample_multi_stream_diag.exc_return = exc_return;
    g_sample_multi_stream_diag.cfsr = SCB->CFSR;
    g_sample_multi_stream_diag.hfsr = SCB->HFSR;
    g_sample_multi_stream_diag.bfar = SCB->BFAR;
    g_sample_multi_stream_diag.mmfar = SCB->MMFAR;
    if ((stack_pointer != 0)
        && (end >= begin)
        && (((begin >= 0x20000000UL) && (end <= 0x20020000UL))
            || ((begin >= 0x24000000UL) && (end <= 0x24080000UL))
            || ((begin >= 0x30000000UL) && (end <= 0x30048000UL))
            || ((begin >= 0x38000000UL) && (end <= 0x38010000UL))))
    {
        const uint32_t *const frame = stack_pointer + extended_words;
        g_sample_multi_stream_diag.stacked_r0 = frame[0];
        g_sample_multi_stream_diag.stacked_r1 = frame[1];
        g_sample_multi_stream_diag.stacked_r2 = frame[2];
        g_sample_multi_stream_diag.stacked_r3 = frame[3];
        g_sample_multi_stream_diag.stacked_r12 = frame[4];
        g_sample_multi_stream_diag.stacked_lr = frame[5];
        g_sample_multi_stream_diag.stacked_pc = frame[6];
        g_sample_multi_stream_diag.stacked_xpsr = frame[7];
        g_sample_multi_stream_diag.pc = frame[6];
        g_sample_multi_stream_diag.lr = frame[5];
    }

    if (g_sample_multi_stream_diag_frozen == 0U)
    {
        g_sample_multi_stream_diag.magic = SAMPLE_MULTI_STREAM_DIAG_MAGIC;
        g_sample_multi_stream_diag.code = (uint32_t)SAMPLE_MULTI_STREAM_DIAG_FAULT;
        g_sample_multi_stream_diag.failure_result = fault_type;
        g_sample_multi_stream_diag.frozen = 1U;
        __DMB();
        g_sample_multi_stream_diag_frozen = 1U;
    }
}

uint8_t sample_multi_stream_diag_breakpoint_pending(void)
{
    return ((g_sample_multi_stream_diag_frozen != 0U)
            && (g_sample_multi_stream_diag_breakpoint_seen == 0U)) ? 1U : 0U;
}

__attribute__((noinline, used, externally_visible))
void sample_multi_stream_diag_breakpoint(void)
{
    if (g_sample_multi_stream_diag_frozen != 0U)
    {
        g_sample_multi_stream_diag_breakpoint_seen = 1U;
    }
}
#endif
