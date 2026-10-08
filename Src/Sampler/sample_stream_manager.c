#include "Sampler/sample_stream_manager.h"
#include "Sampler/sample_page_lease_control.h"
#include "SD/stream_end_to_end_bench.h"

#include <stddef.h>
#include <string.h>

#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_stream_io.h"
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
    if (sample_page_cache_finish_loading(&io_result->token, finish) == 0U)
    {
        stream_end_to_end_bench_probe_io_complete(io_result, 0U);
        return 0U;
    }
    const uint32_t page_ready_cycles = DWT->CYCCNT;
    stream_end_to_end_bench_probe_io_complete(io_result,
                                              page_ready_cycles);
    if (io_result->load_result != SAMPLE_PAGE_LOAD_OK) return 0U;
    if (io_result->request_cycles != 0U)
        brick_perf_wall(PERF_WALL_STREAM_REQUEST_READY,
                        brick_perf_now() - io_result->request_cycles);
    PERF_END(PERF_CPU_MANAGER_FINISH, perf_start);
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
        const uint32_t lookup_start = DWT->CYCCNT;
        const sample_page_state_t state =
            sample_page_cache_get_page_state_key(lease.key, page_index);
        const uint32_t lookup_cycles = DWT->CYCCNT - lookup_start;
        stream_end_to_end_bench_probe_storage_seen(
            lease.key, page_index, lookup_start, lookup_cycles,
            (uint8_t)(state == SAMPLE_PAGE_READY));
        if (state == SAMPLE_PAGE_READY) { PERF_COUNT(PERF_N_CACHE_READY); ++page_rank; continue; }

        if (out_pending != 0) *out_pending = 1U;
        if (state == SAMPLE_PAGE_LOADING) { PERF_COUNT(PERF_N_CACHE_LOADING); return 0U; }
        if (out_candidate == 0) return 0U;

        memset(out_candidate, 0, sizeof(*out_candidate));
        out_candidate->key = lease.key;
        out_candidate->page_index = page_index;
        out_candidate->registration_epoch = lease.registration_epoch;
        out_candidate->voice_id = slot;
        out_candidate->page_rank = page_rank;
        out_candidate->round_robin_slot = slot;
        out_candidate->active = 1U;
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
    sample_page_load_target_t *out_target)
{
    const uint32_t bench_pick_start = DWT->CYCCNT;
    PERF_START(perf_start);
    if (out_target == 0)
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
    const uint32_t bench_pick_end = DWT->CYCCNT;
    stream_end_to_end_bench_probe_manager_pick(
        candidate.key, candidate.page_index,
        bench_pick_start, bench_pick_end);
    PERF_END(PERF_CPU_MANAGER_PICK, perf_start);
    sample_page_load_target_t target;
    PERF_COUNT(PERF_N_CACHE_MISS);
    PERF_START(reserve_start);
    const uint32_t bench_reserve_start = DWT->CYCCNT;
    const uint8_t reserve_ok = sample_page_cache_reserve_page_target_key_alloc(
            candidate.key,
            candidate.page_index,
            SAMPLE_PAGE_ALLOC_VOICE_WINDOW,
            &target);
    const uint32_t bench_reserve_end = DWT->CYCCNT;
    stream_end_to_end_bench_probe_reserve(
        candidate.key, candidate.page_index,
        bench_reserve_start, bench_reserve_end, reserve_ok);
    if (reserve_ok == 0U)
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

    sample_stream_scheduler_begin_round();

    for (;;)
    {
        sample_page_load_target_t target;
        if (sample_stream_manager_pick_next(&target) == 0U)
        {
            break;
        }

        sample_page_load_token_t load_token;
        if (sample_page_cache_begin_loading(&target, &load_token) == 0U)
        {
            continue;
        }
        /* One counted request is one reader-slot need that has won cache
         * ownership and entered LOADING. Candidate scans and retries are not
         * requests. Static presocle prefill is deliberately excluded. */
        PERF_COUNT(PERF_N_PAGES_REQUESTED);
        sample_stream_io_result_t io_result;
        memset(&io_result, 0, sizeof(io_result));
        io_result.token = load_token;
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
    if (sample_stream_manager_submit_prefill(
            SAMPLE_AUDIO_DOMAIN_REC, SAMPLE_PAGE_CACHE_REC_ID_CAPACITY) == 0U)
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
    return sample_page_cache_get_reserved_load_target_domain_range(
        SAMPLE_AUDIO_DOMAIN_CLASSIC, 0U, SAMPLE_CLASSIC_CAPACITY,
        &prefill_target);
}

uint8_t sample_stream_manager_io_in_flight(void)
{
    return (sample_stream_io_active_job_count() != 0U) ? 1U : 0U;
}
