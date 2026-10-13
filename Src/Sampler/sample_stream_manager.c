#include "Sampler/sample_stream_manager.h"
#include "Sampler/sample_page_lease_control.h"

#include <stddef.h>
#include <string.h>

#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_stream_io.h"
#include "Sampler/sample_stream_backend_physical.h"
#include "Sampler/sample_stream_scheduler.h"
#include "Platform/stream_rec_perf.h"
#include "Recorder/audio_recorder_ring.h"
#include "Platform/memory_layout.h"
#include "stm32h7xx_hal.h"

#define SAMPLE_STREAM_CANCEL_REASON_RELEASE_KEY (3U)
#define SAMPLE_STREAM_CANCEL_REASON_SUPERSEDED (6U)

#if BRICK_PERF_DIAG
static uint32_t g_perf_reset_tick;
/* DTCM is CPU-only and debugger-visible without D-cache writeback. */
AUDIO_HOT volatile brick_stream_rec_perf_t g_stream_rec_perf
    __attribute__((used, externally_visible));
void __attribute__((used, externally_visible)) brick_perf_diag_reset(void)
{
    /* Call with playback and recording stopped, then run the workload. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    g_perf_reset_tick = HAL_GetTick();
    memset((void *)&g_stream_rec_perf, 0, sizeof(g_stream_rec_perf));
    g_stream_rec_perf.magic = 0x46505242U;
    g_stream_rec_perf.version = 3U;
    g_stream_rec_perf.size = sizeof(g_stream_rec_perf);
    g_stream_rec_perf.cpu_hz = SystemCoreClock;
}
void __attribute__((used, externally_visible)) brick_perf_diag_snapshot(void)
{
    /* Call after stopping the workload; all counter writes have ceased. */
    g_stream_rec_perf.count[PERF_N_TEST_ELAPSED_MS] =
        (uint32_t)(HAL_GetTick() - g_perf_reset_tick);
    g_stream_rec_perf.count[PERF_N_REC_RING_FILL] =
        g_audio_recorder_ring_state.produced_frames
            - g_audio_recorder_ring_state.released_frames;
}
#endif

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(SAMPLE_CLASSIC_CAPACITY <= SAMPLE_PAGE_CACHE_ID_CAPACITY,
               "stream manager hot scan range must fit in page-cache ids");
_Static_assert(SAMPLE_STREAM_IO_MAX_READERS == 10U,
               "eight musical readers plus Recorder and Preview");
#endif
static uint8_t g_sample_stream_manager_initialized;
STREAM_LOCAL_D2 volatile sample_stream_fastpath_diag_t
    g_sample_stream_fastpath_diag;
static uint8_t sample_stream_manager_candidate_for_slot(
    uint8_t slot,
    sample_stream_scheduler_candidate_t *out_candidate,
    uint8_t *out_pending);
static uint8_t sample_stream_manager_probe_candidate(
    void *context,
    uint8_t slot,
    sample_stream_scheduler_candidate_t *out_candidate);
typedef struct
{
    uint32_t active_mask[2];
} sample_stream_manager_probe_context_t;
static uint8_t sample_stream_manager_finish_io(
    sample_stream_io_result_t *io_result);
static uint8_t sample_stream_manager_submit_prefill(
    sample_audio_domain_t domain, uint16_t capacity);
static void sample_stream_manager_init_storage_once(void)
{
    if (g_sample_stream_manager_initialized != 0U)
    {
        return;
    }

    sample_stream_io_init();
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
    sample_stream_scheduler_init();
    memset((void *)&g_sample_stream_fastpath_diag, 0,
           sizeof(g_sample_stream_fastpath_diag));
}

void sample_stream_manager_release_sample(uint16_t sample_id)
{
    sample_stream_manager_release_key(sample_audio_key_classic(sample_id));
}

void sample_stream_manager_release_key(sample_audio_key_t key)
{
    for (uint8_t slot = 0U; slot < SAMPLE_PAGE_LEASE_SLOT_COUNT; ++slot)
    {
        sample_page_lease_t lease;
        if ((sample_page_lease_control_read(slot, &lease) != 0U)
            && (sample_audio_key_equal(&lease.key, &key) != 0U)) return;
    }
    (void)sample_page_cache_cancel_reserved_key(key, SAMPLE_STREAM_CANCEL_REASON_RELEASE_KEY);
}

uint8_t sample_stream_manager_key_busy(sample_audio_key_t key)
{
    return sample_stream_io_key_busy(key);
}

static uint8_t sample_stream_manager_finish_io(
    sample_stream_io_result_t *io_result)
{
    PERF_START(perf_start);
    if (io_result == 0)
    {
        return 0U;
    }
    const sample_page_finish_result_t finish =
        (io_result->load_result == SAMPLE_PAGE_LOAD_OK)
            ? SAMPLE_PAGE_FINISH_READY : SAMPLE_PAGE_FINISH_ERROR;
    uint8_t completed = 1U;
    for (uint8_t page = 0U; page < io_result->page_count; ++page)
    {
        if (sample_page_cache_finish_loading(
                &io_result->token[page], finish) == 0U)
            completed = 0U;
    }
    if ((completed == 0U)
        || (io_result->load_result != SAMPLE_PAGE_LOAD_OK)) return 0U;
    if (io_result->request_class
        == SAMPLE_STREAM_REQUEST_COALESCED_SEQUENTIAL)
        g_sample_stream_fastpath_diag.fastpath_completed++;
    if (io_result->request_cycles != 0U)
        brick_perf_wall(PERF_WALL_STREAM_REQUEST_READY,
                        brick_perf_now() - io_result->request_cycles);
    PERF_END(PERF_CPU_MANAGER_FINISH, perf_start);
    return 1U;
}

static uint8_t sample_stream_manager_submit_fastpath(
    const sample_page_load_target_t *first_target,
    const sample_stream_scheduler_candidate_t *candidate,
    const sample_page_lease_t *lease)
{
    if ((first_target == 0) || (candidate == 0) || (lease == 0)
        || (candidate->page_role != SAMPLE_PAGE_LEASE_NEXT)
        || (lease->sequential_direction == SAMPLE_PAGE_SEQUENCE_NONE)
        || (lease->jump_pending != 0U)
        || ((lease->valid_mask
             & SAMPLE_PAGE_LEASE_VALID(SAMPLE_PAGE_LEASE_CURRENT)) == 0U)
        || ((lease->sequential_direction == SAMPLE_PAGE_SEQUENCE_FORWARD)
            && ((lease->pages[SAMPLE_PAGE_LEASE_CURRENT] + 1U
                    != candidate->page_index)
                || (lease->sequential_page_end <= candidate->page_index + 1U)))
        || ((lease->sequential_direction == SAMPLE_PAGE_SEQUENCE_REVERSE)
            && ((candidate->page_index + 1U
                    != lease->pages[SAMPLE_PAGE_LEASE_CURRENT])
                || (candidate->page_index <= lease->sequential_page_begin)))
        || (first_target->frame_count != first_target->frames_per_page)
        || (sample_page_cache_get_page_state_key(
                lease->key,
                lease->pages[SAMPLE_PAGE_LEASE_CURRENT])
            != SAMPLE_PAGE_READY))
        return 0U;

    sample_page_stream_load_info_t stream;
    if ((sample_page_cache_get_stream_load_info_key(
            candidate->key, &stream) == 0U)
        || (stream.canonical_float == 0U)
        || (stream.format != SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED)
        || (stream.stride_floats != SAMPLE_PAGE_FRAME_STRIDE_FLOATS)
        || (stream.stream_safe.block_align != SAMPLE_PAGE_BYTES_PER_FRAME)
        || (sample_stream_safe_metadata_backend(&stream.stream_safe)
            != SAMPLE_STREAM_BACKEND_PHYSICAL))
        return 0U;

    const uint8_t reverse = (lease->sequential_direction
        == SAMPLE_PAGE_SEQUENCE_REVERSE) ? 1U : 0U;
    uint32_t remaining_pages = (reverse != 0U)
        ? (candidate->page_index - lease->sequential_page_begin + 1U)
        : (lease->sequential_page_end - candidate->page_index);
    uint8_t desired = (remaining_pages > SAMPLE_STREAM_FASTPATH_MAX_PAGES)
        ? SAMPLE_STREAM_FASTPATH_MAX_PAGES : (uint8_t)remaining_pages;
    if (reverse != 0U)
    {
        uint8_t contiguous = 0U;
        for (uint8_t pages = desired; pages >= 2U; --pages)
        {
            const uint32_t physical_start_frame =
                (candidate->page_index - (uint32_t)(pages - 1U))
                * first_target->frames_per_page;
            if (sample_stream_backend_physical_contiguous_page_count(
                    &stream.stream_safe, physical_start_frame, pages) == pages)
            {
                contiguous = pages;
                break;
            }
        }
        desired = contiguous;
    }
    else
    {
        desired = sample_stream_backend_physical_contiguous_page_count(
            &stream.stream_safe, first_target->start_frame, desired);
    }
    if (desired < 2U)
    {
        g_sample_stream_fastpath_diag.fastpath_fallback_noncontiguous++;
        return 0U;
    }

    sample_page_load_target_t target[SAMPLE_STREAM_FASTPATH_MAX_PAGES];
    sample_page_load_target_t reserved_target[SAMPLE_STREAM_FASTPATH_MAX_PAGES];
    sample_page_load_token_t token[SAMPLE_STREAM_FASTPATH_MAX_PAGES];
    reserved_target[0] = *first_target;
    uint8_t reserved = 1U;
    while (reserved < desired)
    {
        const uint32_t page_index = (reverse != 0U)
            ? (candidate->page_index - reserved)
            : (candidate->page_index + reserved);
        const sample_page_state_t state =
            sample_page_cache_get_page_state_key(candidate->key, page_index);
        if ((state == SAMPLE_PAGE_READY) || (state == SAMPLE_PAGE_LOADING)
            || (state == SAMPLE_PAGE_RESERVED)) break;
        if (sample_page_cache_reserve_page_target_key_alloc(
                candidate->key, page_index,
                SAMPLE_PAGE_ALLOC_VOICE_WINDOW,
                &reserved_target[reserved]) == 0U)
        {
            g_sample_stream_fastpath_diag.fastpath_fallback_cache_reserve++;
            break;
        }
        if (reserved_target[reserved].frame_count
            != reserved_target[reserved].frames_per_page)
        {
            (void)sample_page_cache_cancel_reserved_page_key(
                candidate->key, page_index,
                SAMPLE_STREAM_CANCEL_REASON_SUPERSEDED);
            break;
        }
        ++reserved;
    }
    if (reserved < 2U) return 0U;

    for (uint8_t page = 0U; page < reserved; ++page)
        target[page] = (reverse != 0U)
            ? reserved_target[reserved - 1U - page] : reserved_target[page];

    uint8_t loading = 0U;
    while ((loading < reserved)
        && (sample_page_cache_begin_loading(
                &target[loading], &token[loading]) != 0U))
        ++loading;
    if (loading != reserved)
    {
        for (uint8_t page = 0U; page < loading; ++page)
            (void)sample_page_cache_finish_loading(
                &token[page], SAMPLE_PAGE_FINISH_ERROR);
        for (uint8_t page = loading; page < reserved; ++page)
            (void)sample_page_cache_cancel_reserved_page_key(
                candidate->key, target[page].page_index,
                SAMPLE_STREAM_CANCEL_REASON_SUPERSEDED);
        return 1U;
    }

    PERF_ACCUM(PERF_N_PAGES_REQUESTED, reserved);
    if (sample_stream_io_begin_coalesced(
            token, reserved, candidate->voice_id, lease->seq,
            UINT32_MAX) == 0U)
    {
        for (uint8_t page = 0U; page < reserved; ++page)
            (void)sample_page_cache_finish_loading(
                &token[page], SAMPLE_PAGE_FINISH_ERROR);
        return 1U;
    }
    g_sample_stream_fastpath_diag.fastpath_requests++;
    g_sample_stream_fastpath_diag.fastpath_pages_requested += reserved;
    if (reserved == 2U)
        g_sample_stream_fastpath_diag.fastpath_2page_count++;
    else if (reserved == 3U)
        g_sample_stream_fastpath_diag.fastpath_3page_count++;
    else if (reserved == SAMPLE_STREAM_FASTPATH_MAX_PAGES)
        g_sample_stream_fastpath_diag.fastpath_4page_count++;
    return 1U;
}

static uint8_t sample_stream_manager_candidate_for_slot(
    uint8_t slot,
    sample_stream_scheduler_candidate_t *out_candidate,
    uint8_t *out_pending)
{
    if (out_pending != 0)
    {
        *out_pending = 0U;
    }
    if (slot >= SAMPLE_PAGE_LEASE_SLOT_COUNT)
    {
        return 0U;
    }

    sample_page_lease_t lease;
    if (sample_page_lease_control_read(slot, &lease) == 0U) return 0U;
    uint8_t page_rank = 0U;
    uint8_t found = 0U;
    sample_stream_scheduler_candidate_t best;
    for (uint8_t role = 0U; role < SAMPLE_PAGE_LEASE_PAGE_COUNT; ++role)
    {
        if ((lease.valid_mask & SAMPLE_PAGE_LEASE_VALID(role)) == 0U) continue;
        const uint32_t page_index = lease.pages[role];
        uint8_t duplicate = 0U;
        for (uint8_t previous = 0U; previous < role; ++previous)
        {
            if (((lease.valid_mask & SAMPLE_PAGE_LEASE_VALID(previous)) != 0U)
                && (lease.pages[previous] == page_index)) duplicate = 1U;
        }
        if (duplicate != 0U) continue;
        const sample_page_state_t state =
            sample_page_cache_get_page_state_key(lease.key, page_index);
        if (state == SAMPLE_PAGE_READY) { PERF_COUNT(PERF_N_CACHE_READY); ++page_rank; continue; }

        if (out_pending != 0) *out_pending = 1U;
        if (state == SAMPLE_PAGE_LOADING)
        {
            PERF_COUNT(PERF_N_CACHE_LOADING);
            continue;
        }
        if (out_candidate == 0) continue;

        sample_stream_scheduler_candidate_t candidate;
        memset(&candidate, 0, sizeof(candidate));
        candidate.key = lease.key;
        candidate.page_index = page_index;
        candidate.registration_epoch = lease.registration_epoch;
        candidate.voice_id = slot;
        candidate.page_rank = page_rank;
        candidate.page_role = role;
        candidate.priority = ((lease.jump_pending != 0U)
                && (role == SAMPLE_PAGE_LEASE_LOOP_START_NEXT))
            ? 0U : ((role == SAMPLE_PAGE_LEASE_CURRENT)
                ? 0U : ((role == SAMPLE_PAGE_LEASE_NEXT) ? 1U : 2U));
        candidate.round_robin_slot = slot;
        candidate.active = 1U;
        if ((found == 0U) || (candidate.priority < best.priority))
        {
            best = candidate;
            found = 1U;
        }
    }
    if ((found != 0U) && (out_candidate != 0))
    {
        *out_candidate = best;
        return 1U;
    }
    return 0U;
}

static uint8_t sample_stream_manager_probe_candidate(
    void *context,
    uint8_t slot,
    sample_stream_scheduler_candidate_t *out_candidate)
{
    const sample_stream_manager_probe_context_t *const probe_context = context;
    if ((probe_context == NULL)
        || ((probe_context->active_mask[slot >> 5U]
             & (UINT32_C(1) << (slot & 31U))) == 0U))
        return 0U;
    return sample_stream_manager_candidate_for_slot(slot, out_candidate, 0);
}

static uint8_t sample_stream_manager_pick_next(
    sample_page_load_target_t *out_target,
    sample_stream_scheduler_candidate_t *out_candidate,
    sample_page_lease_t *out_lease)
{
    PERF_START(perf_start);
    if ((out_target == 0) || (out_candidate == 0) || (out_lease == 0))
    {
        return 0U;
    }

    sample_stream_scheduler_candidate_t candidate;
    sample_stream_manager_probe_context_t probe_context;
    sample_page_lease_control_active_slots(probe_context.active_mask);
    if (sample_stream_scheduler_pick(
            sample_stream_manager_probe_candidate, &probe_context, &candidate) == 0U)
    {
        return 0U;
    }
    PERF_END(PERF_CPU_MANAGER_PICK, perf_start);
    sample_page_load_target_t target;
    PERF_COUNT(PERF_N_CACHE_MISS);
    PERF_START(reserve_start);
    if (sample_page_cache_reserve_page_target_key_alloc(
            candidate.key,
            candidate.page_index,
            SAMPLE_PAGE_ALLOC_VOICE_WINDOW,
            &target) == 0U)
    {
        PERF_COUNT(PERF_N_CACHE_ALLOC_FAIL);
        return 0U;
    }
    PERF_END(PERF_CPU_CACHE_RESERVE, reserve_start);
    PERF_COUNT(PERF_N_CACHE_ALLOC);
    if ((candidate.registration_epoch != 0U)
        && (target.registration_epoch != candidate.registration_epoch))
    {
        (void)sample_page_cache_cancel_reserved_page_key(
            candidate.key,
            candidate.page_index,
            SAMPLE_STREAM_CANCEL_REASON_SUPERSEDED);
        return 0U;
    }
    *out_target = target;
    *out_candidate = candidate;
    if (sample_page_lease_control_read(
            candidate.voice_id, out_lease) == 0U)
    {
        (void)sample_page_cache_cancel_reserved_page_key(
            candidate.key, candidate.page_index,
            SAMPLE_STREAM_CANCEL_REASON_SUPERSEDED);
        return 0U;
    }
    if ((out_lease->registration_epoch != candidate.registration_epoch)
        || (sample_audio_key_equal(
                &out_lease->key, &candidate.key) == 0U))
    {
        (void)sample_page_cache_cancel_reserved_page_key(
            candidate.key, candidate.page_index,
            SAMPLE_STREAM_CANCEL_REASON_SUPERSEDED);
        return 0U;
    }
    return 1U;
}

static uint8_t sample_stream_manager_submit_prefill(
    sample_audio_domain_t domain, uint16_t capacity)
{
    sample_page_load_target_t target;
    if ((sample_stream_io_active_job_count() >= SAMPLE_STREAM_IO_JOB_CAPACITY)
        || (sample_page_cache_get_reserved_load_target_domain_range(
                domain, 0U, capacity, &target) == 0U))
    {
        return 0U;
    }
    sample_page_load_token_t token;
    if (sample_page_cache_begin_loading(&target, &token) == 0U)
    {
        return 0U;
    }
    if (sample_stream_io_begin(&token, UINT32_MAX) == 0U)
    {
        (void)sample_page_cache_finish_loading(
            &token, SAMPLE_PAGE_FINISH_ERROR);
        return 0U;
    }
    return 1U;
}

static void sample_stream_manager_service_impl(uint32_t byte_budget)
{
    if (byte_budget == 0U)
    {
        return;
    }

    if (sample_stream_io_active_job_count() != 0U)
    {
        for (uint8_t slot = 0U; slot < SAMPLE_PAGE_LEASE_SLOT_COUNT; ++slot)
        {
            sample_page_lease_t lease;
            const uint32_t seq = (sample_page_lease_control_read(
                    slot, &lease) != 0U) ? lease.seq : 0U;
            if (sample_stream_io_cancel_stale_coalesced_before_submit(
                    slot, seq) != 0U)
                g_sample_stream_fastpath_diag.fastpath_cancel_before_submit++;
        }
        sample_stream_io_result_t pending_result;
        if (sample_stream_io_poll(&pending_result) != 0U)
        {
            (void)sample_stream_manager_finish_io(&pending_result);
            return;
        }
        if (sample_stream_io_active_job_count()
                >= SAMPLE_STREAM_IO_JOB_CAPACITY)
        {
            return;
        }
    }

    /* REC_SOURCE cannot own a voice lease before its startup page is READY.
     * Admit that reserved page while the bounded I/O queue still has room;
     * otherwise a permanent stream workload can keep selecting lease-backed
     * pages and prevent the finalized take from ever becoming playable. */
    (void)sample_stream_manager_submit_prefill(
        SAMPLE_AUDIO_DOMAIN_REC, SAMPLE_PAGE_CACHE_REC_ID_CAPACITY);
    if (sample_stream_io_active_job_count()
            >= SAMPLE_STREAM_IO_JOB_CAPACITY)
    {
        return;
    }

    sample_stream_scheduler_begin_round();

    for (;;)
    {
        sample_page_load_target_t target;
        sample_stream_scheduler_candidate_t candidate;
        sample_page_lease_t lease;
        if (sample_stream_manager_pick_next(
                &target, &candidate, &lease) == 0U)
        {
            break;
        }

        if (sample_stream_manager_submit_fastpath(
                &target, &candidate, &lease) != 0U)
            return;

        sample_page_load_token_t load_token;
        if (sample_page_cache_begin_loading(&target, &load_token) == 0U)
        {
            continue;
        }
        /* One counted request is one reader-slot need that has won cache
         * ownership and entered LOADING. Candidate scans and retries are not
         * requests. Static presocle prefill is deliberately excluded. */
        PERF_COUNT(PERF_N_PAGES_REQUESTED);
        g_sample_stream_fastpath_diag.normal_16k_requests++;
        sample_stream_io_result_t io_result;
        memset(&io_result, 0, sizeof(io_result));
        io_result.token[0] = load_token;
        io_result.page_count = 1U;
        io_result.request_class = SAMPLE_STREAM_REQUEST_NORMAL_16K;
        io_result.load_result = SAMPLE_PAGE_LOAD_INVALID_ARG;
        PERF_START(submit_start);
        const uint8_t submitted = sample_stream_io_begin(
            &load_token, UINT32_MAX);
        PERF_END(PERF_CPU_STREAM_SUBMIT, submit_start);
        if (submitted == 0U)
        {
            (void)sample_stream_manager_finish_io(&io_result);
            return;
        }
        return;

    }
    /* Classic and Multi startup pages use the same registration, reservation
     * and asynchronous I/O lifecycle.  Multi keeps instrument-level READY
     * publication, but no longer owns a parallel synchronous page loader. */
    (void)sample_stream_manager_submit_prefill(
        SAMPLE_AUDIO_DOMAIN_MULTI, SAMPLE_PAGE_CACHE_MULTI_ID_CAPACITY);
    if (sample_stream_io_active_job_count()
            >= SAMPLE_STREAM_IO_JOB_CAPACITY)
    {
        return;
    }
    (void)sample_stream_manager_submit_prefill(
        SAMPLE_AUDIO_DOMAIN_CLASSIC, SAMPLE_CLASSIC_CAPACITY);
}

void sample_stream_manager_service(uint32_t byte_budget)
{
    sample_stream_manager_service_impl(byte_budget);
}

uint8_t sample_stream_manager_has_pending_sd_work(void)
{
    if (sample_stream_io_active_job_count() != 0U)
    {
        return 1U;
    }
    uint32_t active_mask[2];
    sample_page_lease_control_active_slots(active_mask);
    for (uint8_t slot = 0U; slot < SAMPLE_PAGE_LEASE_SLOT_COUNT; ++slot)
    {
        if ((active_mask[slot >> 5U]
             & (UINT32_C(1) << (slot & 31U))) == 0U) continue;
        uint8_t pending = 0U;
        (void)sample_stream_manager_candidate_for_slot(slot, 0, &pending);
        if (pending != 0U) return 1U;
    }
    sample_page_load_target_t prefill_target;
    if (sample_page_cache_get_reserved_load_target_domain_range(
        SAMPLE_AUDIO_DOMAIN_REC, 0U, SAMPLE_PAGE_CACHE_REC_ID_CAPACITY,
        &prefill_target) != 0U) return 1U;
    if (sample_page_cache_get_reserved_load_target_domain_range(
        SAMPLE_AUDIO_DOMAIN_MULTI, 0U, SAMPLE_PAGE_CACHE_MULTI_ID_CAPACITY,
        &prefill_target) != 0U) return 1U;
    return sample_page_cache_get_reserved_load_target_domain_range(
        SAMPLE_AUDIO_DOMAIN_CLASSIC, 0U, SAMPLE_CLASSIC_CAPACITY,
        &prefill_target);
}
