#include "Sampler/sample_stream_manager.h"
#include "Sampler/sample_page_lease_control.h"

#include <stddef.h>
#include <string.h>

#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_stream_io.h"
#include "Sampler/sample_stream_publish.h"
#include "Sampler/sample_stream_scheduler.h"
#include "Sampler/sample_stream_transport.h"
#include "Sampler/sample_stream_diag.h"
#include "Sampler/sample_page_cache_audio.h"
#include "SD/sd_block_device.h"
#include "Storage/sd_access_gate.h"
#include "Platform/memory_layout.h"
#include "stm32h7xx_hal.h"

#define SAMPLE_STREAM_CANCEL_REASON_RELEASE_KEY (3U)
#define SAMPLE_STREAM_CANCEL_REASON_SUPERSEDED (6U)

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(SAMPLE_CLASSIC_CAPACITY <= SAMPLE_PAGE_CACHE_ID_CAPACITY,
               "stream manager hot scan range must fit in page-cache ids");
_Static_assert(SAMPLE_STREAM_IO_MAX_READERS <= SAMPLE_CLASSIC_CAPACITY,
               "active stream readers must be bounded below hot sample capacity");
#endif
static uint8_t g_sample_stream_manager_initialized;
volatile sample_stream_diag_t g_sample_stream_diag __attribute__((used));

static uint32_t sample_stream_diag_now(void) { return DWT->CYCCNT; }
static void sample_stream_diag_max(volatile uint32_t *dst, uint32_t value)
{ if (value > *dst) *dst = value; }
static uint8_t sample_stream_diag_boundary_slot_seen(uint8_t slot)
{
    if (slot < 32U) return (g_sample_stream_diag.boundary_slot_mask_lo & (1UL << slot)) != 0U;
    if (slot < 64U) return (g_sample_stream_diag.boundary_slot_mask_hi & (1UL << (slot - 32U))) != 0U;
    return 0U;
}

void sample_stream_diag_lease_audio(uint8_t reader, uint8_t lease_slot,
    sample_audio_key_t key, uint32_t audio_page, uint32_t watch_page,
    const sample_page_lease_range_t ranges[2], uint32_t result, uint32_t seq)
{
    if (g_sample_stream_diag.frozen || reader >= SAMPLE_STREAM_TARGET_MAX_VOICES || !ranges) return;
    volatile sample_stream_diag_boundary_t *const b = &g_sample_stream_diag.boundary[reader][0];
    if (b->audio_cycles && (b->watch_page != watch_page ||
        sample_audio_key_equal((const sample_audio_key_t *)&b->key, &key) == 0U)) {
        g_sample_stream_diag.boundary[reader][1] = *b;
        memset((void *)b, 0, sizeof(*b));
    }
    b->watch_page = watch_page; b->audio_page = audio_page;
    b->lease_slot = lease_slot; b->key = key;
    if (lease_slot < 32U) g_sample_stream_diag.boundary_slot_mask_lo |= 1UL << lease_slot;
    else if (lease_slot < 64U) g_sample_stream_diag.boundary_slot_mask_hi |= 1UL << (lease_slot - 32U);
    b->audio_cycles = sample_stream_diag_now(); b->audio_seq = seq;
    b->audio_result = result;
    b->audio_r0_first = ranges[0].first_page; b->audio_r0_count = ranges[0].page_count;
    b->audio_r1_first = ranges[1].first_page; b->audio_r1_count = ranges[1].page_count;
    b->event = STREAM_BOUNDARY_AUDIO_PUBLISH;
}

void sample_stream_diag_lease_storage(uint8_t lease_slot, uint32_t ok,
    uint32_t seq, const sample_page_lease_t *lease)
{
    if (g_sample_stream_diag.frozen || !sample_stream_diag_boundary_slot_seen(lease_slot)) return;
    for (uint32_t i = 0U; i < SAMPLE_STREAM_TARGET_MAX_VOICES; ++i)
        for (uint32_t j = 0U; j < 2U; ++j) {
            volatile sample_stream_diag_boundary_t *const b = &g_sample_stream_diag.boundary[i][j];
            if (!b->audio_cycles || b->lease_slot != lease_slot ||
                !g_sample_stream_diag.reader[i].active) continue;
            b->storage_cycles = sample_stream_diag_now();
            b->storage_seq = ok ? lease->seq : seq;
            b->storage_ok = ok; ++b->storage_reads;
            if (!ok) {
                ++b->storage_rejects; b->event = STREAM_BOUNDARY_READ_REJECT;
                b->reason = (seq == 0U) ? 1U : ((seq & 1U) ? 2U :
                    ((lease && lease->seq == seq && lease->ranges[0].page_count == 0U)
                     ? 6U : 3U));
                continue;
            }
            b->storage_key = lease->key;
            b->storage_r0_first = lease->ranges[0].first_page;
            b->storage_r0_count = lease->ranges[0].page_count;
            b->storage_r1_first = lease->ranges[1].first_page;
            b->storage_r1_count = lease->ranges[1].page_count;
            b->storage_contains = 0U;
            if (sample_audio_key_equal((const sample_audio_key_t *)&b->key, &lease->key))
                for (uint32_t k = 0U; k < 2U; ++k)
                    if (lease->ranges[k].page_count && b->watch_page >= lease->ranges[k].first_page
                        && b->watch_page - lease->ranges[k].first_page < lease->ranges[k].page_count)
                        b->storage_contains = 1U;
            if (b->storage_contains) ++b->storage_contains_reads;
            else ++b->storage_missing_reads;
            b->event = STREAM_BOUNDARY_STORAGE_READ;
            b->reason = b->storage_contains ? 0U :
                (sample_audio_key_equal((const sample_audio_key_t *)&b->key, &lease->key) ? 4U : 5U);
        }
}

void sample_stream_diag_boundary_candidate(uint8_t lease_slot,
    sample_audio_key_t key, uint32_t page, uint32_t state, uint32_t event)
{
    if (g_sample_stream_diag.frozen || !sample_stream_diag_boundary_slot_seen(lease_slot)) return;
    for (uint32_t i = 0U; i < SAMPLE_STREAM_TARGET_MAX_VOICES; ++i)
        for (uint32_t j = 0U; j < 2U; ++j) {
            volatile sample_stream_diag_boundary_t *const b = &g_sample_stream_diag.boundary[i][j];
            if (!b->audio_cycles || b->lease_slot != lease_slot ||
                !g_sample_stream_diag.reader[i].active ||
                sample_audio_key_equal((const sample_audio_key_t *)&b->key, &key) == 0U) continue;
            if (b->watch_page != page && event != STREAM_BOUNDARY_NO_WORK &&
                event != STREAM_BOUNDARY_OTHER_CANDIDATE &&
                event != STREAM_BOUNDARY_EARLIER_LOADING) continue;
            if (b->watch_page == page && (event == STREAM_BOUNDARY_OTHER_CANDIDATE ||
                event == STREAM_BOUNDARY_EARLIER_LOADING)) continue;
            b->event = event; b->candidate_cycles = sample_stream_diag_now();
            b->examined_page = page; b->examined_state = state;
            b->reason = event;
            if (event == STREAM_BOUNDARY_CANDIDATE_FOUND) ++b->candidate_found;
            if (event == STREAM_BOUNDARY_CANDIDATE_NONE || event == STREAM_BOUNDARY_NO_WORK)
                ++b->candidate_none;
            if (event == STREAM_BOUNDARY_PENDING_ONLY) ++b->pending_seen;
            if (event == STREAM_BOUNDARY_RESERVE_ATTEMPT) {
                b->reserve_cycles = b->candidate_cycles; ++b->reserve_attempts;
            }
            if (event == STREAM_BOUNDARY_RESERVE_FAILED || event == STREAM_BOUNDARY_RESERVED)
                b->reserve_result = (event == STREAM_BOUNDARY_RESERVED);
        }
}

static void sample_stream_diag_trace(uint32_t event, uint32_t slot,
    sample_audio_key_t key, uint32_t page, uint32_t frame, uint32_t state, uint32_t extra)
{
    if (g_sample_stream_diag.frozen != 0U) return;
    const uint32_t index = g_sample_stream_diag.trace_next++ & 63U;
    volatile sample_stream_diag_trace_t *const t = &g_sample_stream_diag.trace[index];
    t->sequence = g_sample_stream_diag.trace_next;
    t->cycles = sample_stream_diag_now(); t->event = event; t->reader_slot = slot;
    t->key = key; t->page = page; t->frame = frame; t->page_state = state;
    t->extra = extra; t->active_readers = g_sample_stream_diag.active_readers;
    t->scheduler_pending = g_sample_stream_diag.scheduler_pending;
    t->sd_pending = sd_block_device_async_pending_count();
    t->sd_state = sd_block_device_async_hardware_state();
    if (g_sample_stream_diag.trace_count < 64U) ++g_sample_stream_diag.trace_count;
}

void sample_stream_diag_init(void)
{
    memset((void *)&g_sample_stream_diag, 0, sizeof(g_sample_stream_diag));
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    g_sample_stream_diag.magic = 0x53444731U;
    g_sample_stream_diag.version = 1U;
    g_sample_stream_diag.cycle_hz = SystemCoreClock;
    for (uint32_t i = 0; i < SAMPLE_STREAM_TARGET_MAX_VOICES; ++i)
        g_sample_stream_diag.reader[i].min_ready_distance = UINT32_MAX;
}

void sample_stream_diag_bind(uint8_t slot, sample_audio_key_t key, uint32_t epoch, uint32_t frame)
{
    if (slot >= SAMPLE_STREAM_TARGET_MAX_VOICES ||
        (key.domain != SAMPLE_AUDIO_DOMAIN_MULTI && key.domain != SAMPLE_AUDIO_DOMAIN_CLASSIC
         && key.domain != SAMPLE_AUDIO_DOMAIN_REC)) return;
    volatile sample_stream_diag_reader_t *const r = &g_sample_stream_diag.reader[slot];
    if (r->active == 0U) ++g_sample_stream_diag.active_readers;
    memset((void *)r, 0, sizeof(*r));
    if (!g_sample_stream_diag.frozen)
        memset((void *)&g_sample_stream_diag.boundary[slot], 0,
               sizeof(g_sample_stream_diag.boundary[slot]));
    r->active = 1U; r->slot = slot; r->key = key; r->registration_epoch = epoch;
    r->frame = frame; r->min_ready_distance = UINT32_MAX;
}

void sample_stream_diag_unbind(uint8_t slot)
{
    if (slot >= SAMPLE_STREAM_TARGET_MAX_VOICES) return;
    volatile sample_stream_diag_reader_t *const r = &g_sample_stream_diag.reader[slot];
    if (r->active != 0U && g_sample_stream_diag.active_readers != 0U)
        --g_sample_stream_diag.active_readers;
    r->active = 0U;
}

void sample_stream_diag_need(uint8_t slot, sample_audio_key_t key, uint32_t frame, uint32_t page)
{
    if (slot >= SAMPLE_STREAM_TARGET_MAX_VOICES ||
        (key.domain != SAMPLE_AUDIO_DOMAIN_MULTI && key.domain != SAMPLE_AUDIO_DOMAIN_CLASSIC
         && key.domain != SAMPLE_AUDIO_DOMAIN_REC)) return;
    volatile sample_stream_diag_reader_t *const r = &g_sample_stream_diag.reader[slot];
    r->frame = frame; r->next_page = page;
    if (r->need_page == page && r->t_need != 0U) return;
    r->prev_need_page = r->need_page;
    r->prev_t_need = r->t_need; r->prev_t_reserved = r->t_reserved;
    r->prev_t_dma_start = r->t_dma_start; r->prev_t_dma_complete = r->t_dma_complete;
    r->prev_t_ready = r->t_ready; r->prev_t_first_use = r->t_first_use;
    r->prev_dma_owner = r->dma_owner;
    r->need_page = page; r->t_need = sample_stream_diag_now();
    r->t_reserved = 0U; r->t_dma_start = 0U; r->t_dma_complete = 0U;
    r->t_ready = 0U; r->t_first_use = 0U;
    r->dma_owner = 0U;
    ++r->refills; ++g_sample_stream_diag.requests;
}

void sample_stream_diag_use(uint8_t slot, uint32_t frame, uint32_t page,
                            uint32_t ready_distance, uint32_t generation)
{
    if (slot >= SAMPLE_STREAM_TARGET_MAX_VOICES) return;
    volatile sample_stream_diag_reader_t *const r = &g_sample_stream_diag.reader[slot];
    r->frame = frame;
    if (r->pages_used == 0U || r->current_page != page) ++r->pages_used;
    r->current_page = page; r->page_generation = generation;
    if (ready_distance < r->min_ready_distance) r->min_ready_distance = ready_distance;
    if (r->need_page == page && r->t_first_use == 0U) r->t_first_use = sample_stream_diag_now();
    if (r->prev_need_page == page && r->prev_t_first_use == 0U) r->prev_t_first_use = sample_stream_diag_now();
}

void sample_stream_diag_fault(uint8_t slot, sample_audio_key_t key, uint32_t frame,
                              uint32_t page, sample_page_state_t state, uint32_t event)
{
    volatile sample_stream_diag_t *const d = &g_sample_stream_diag;
    if (event == STREAM_DIAG_AUDIO_MISS) ++d->audio_miss;
    else if (event == STREAM_DIAG_AUDIO_NOT_READY) ++d->audio_not_ready;
    else if (event == STREAM_DIAG_AUDIO_BAD_KEY) ++d->audio_bad_key;
    else if (event == STREAM_DIAG_AUDIO_BAD_EPOCH) ++d->audio_bad_epoch;
    if (event == STREAM_DIAG_AUDIO_MISS || event == STREAM_DIAG_AUDIO_NOT_READY ||
        event == STREAM_DIAG_AUDIO_UNDERRUN) ++d->underruns;
    if (slot < SAMPLE_STREAM_TARGET_MAX_VOICES) {
        volatile sample_stream_diag_reader_t *const r = &d->reader[slot];
        r->frame = frame;
        if (event == STREAM_DIAG_AUDIO_MISS) ++r->misses;
        else if (event == STREAM_DIAG_AUDIO_NOT_READY) ++r->not_ready;
        if (event == STREAM_DIAG_AUDIO_MISS || event == STREAM_DIAG_AUDIO_NOT_READY ||
            event == STREAM_DIAG_AUDIO_UNDERRUN) ++r->underruns;
    }
    if (d->frozen != 0U) return;
    sample_stream_diag_trace(event, slot, key, page, frame, state, 0U);
    d->frozen = 1U;
    if (slot < SAMPLE_STREAM_TARGET_MAX_VOICES)
        for (uint32_t j = 0U; j < 2U; ++j) {
            volatile sample_stream_diag_boundary_t *const b = &d->boundary[slot][j];
            if (!b->audio_cycles || b->watch_page != page ||
                b->lease_slot >= SAMPLE_PAGE_LEASE_SLOT_COUNT) continue;
            const sample_page_lease_t *const raw = &g_sample_page_leases[b->lease_slot];
            b->fault_cycles = sample_stream_diag_now(); b->fault_seq = raw->seq;
            b->fault_key = raw->key;
            b->fault_r0_first = raw->ranges[0].first_page;
            b->fault_r0_count = raw->ranges[0].page_count;
            b->fault_r1_first = raw->ranges[1].first_page;
            b->fault_r1_count = raw->ranges[1].page_count;
        }
    d->first_gate_polls = d->gate_polls;
    d->first_service_calls = d->service_calls;
    d->first_gate_pending = d->gate_pending;
    d->first_gate_deferred_load = d->gate_deferred_load;
    d->first_gate_acquire_fail = d->gate_acquire_fail;
    d->first_gate_owner = sd_access_gate_current_owner();
    volatile sample_stream_diag_snapshot_t *const s = &d->first;
    s->cycles = sample_stream_diag_now(); s->event = event; s->reader_slot = slot;
    s->key = key; s->page = page; s->frame = frame; s->page_state = state;
    s->prev_state = page ? sample_page_cache_audio_get_page_state_key(key, page - 1U) : SAMPLE_PAGE_FREE;
    s->next_state = sample_page_cache_audio_get_page_state_key(key, page + 1U);
    s->active_readers = d->active_readers; s->scheduler_pending = d->scheduler_pending;
    s->sd_pending = sd_block_device_async_pending_count();
    s->sd_state = sd_block_device_async_hardware_state();
    sd_block_device_debug_snapshot_t sd; sd_block_device_debug_snapshot(&sd);
    s->sd_owner = sd.owner_client; s->sd_operation = sd.operation;
    s->sd_fault = sd.fault_latched; s->sd_irq_error = sd.irq_error;
    if (slot < SAMPLE_STREAM_TARGET_MAX_VOICES) s->refill = d->reader[slot];
    if (slot < SAMPLE_STREAM_TARGET_MAX_VOICES) {
        const volatile sample_stream_diag_reader_t *const r = &d->reader[slot];
        if (r->need_page == page) {
            s->t_need = r->t_need; s->t_reserved = r->t_reserved;
            s->t_dma_start = r->t_dma_start; s->t_dma_complete = r->t_dma_complete;
            s->t_ready = r->t_ready; s->t_first_use = r->t_first_use;
        } else if (r->prev_need_page == page) {
            s->t_need = r->prev_t_need; s->t_reserved = r->prev_t_reserved;
            s->t_dma_start = r->prev_t_dma_start; s->t_dma_complete = r->prev_t_dma_complete;
            s->t_ready = r->prev_t_ready; s->t_first_use = r->prev_t_first_use;
        }
    }
    s->reserved = d->reserved; s->loading = d->loading; s->ready = d->ready;
    s->failed = d->failed; s->dma_starts = d->dma_starts;
    s->dma_completions = d->dma_completions; s->dma_errors = d->dma_errors;
    s->busy = d->busy; s->queue_full = d->queue_full;
    __DMB(); s->valid = 1U;
}

void sample_stream_diag_page(sample_audio_key_t key, uint32_t page,
    sample_page_state_t old_state, sample_page_state_t new_state, uint32_t generation)
{
    volatile sample_stream_diag_t *const d = &g_sample_stream_diag;
    if (old_state == new_state) return;
    if (old_state == SAMPLE_PAGE_RESERVED && d->reserved) --d->reserved;
    if (old_state == SAMPLE_PAGE_LOADING && d->loading) --d->loading;
    if (old_state == SAMPLE_PAGE_READY && d->ready) --d->ready;
    if (old_state == SAMPLE_PAGE_FAILED && d->failed) --d->failed;
    if (new_state == SAMPLE_PAGE_RESERVED) ++d->reserved;
    if (new_state == SAMPLE_PAGE_LOADING) ++d->loading;
    if (new_state == SAMPLE_PAGE_READY) ++d->ready;
    if (new_state == SAMPLE_PAGE_FAILED) ++d->failed;
    uint32_t event = 0U;
    if (new_state == SAMPLE_PAGE_RESERVED) event = STREAM_DIAG_RESERVED;
    if (new_state == SAMPLE_PAGE_LOADING) event = STREAM_DIAG_LOADING;
    if (new_state == SAMPLE_PAGE_READY) event = STREAM_DIAG_READY;
    if (new_state == SAMPLE_PAGE_FAILED) event = STREAM_DIAG_FAILED;
    for (uint32_t i = 0U; i < SAMPLE_STREAM_TARGET_MAX_VOICES; ++i) {
        volatile sample_stream_diag_reader_t *const r = &d->reader[i];
        if (r->active == 0U || (r->need_page != page && r->prev_need_page != page) ||
            sample_audio_key_equal((const sample_audio_key_t *)&r->key, &key) == 0U) continue;
        const uint32_t now = sample_stream_diag_now();
        const uint8_t prior = (r->prev_need_page == page && r->need_page != page);
        if (new_state == SAMPLE_PAGE_RESERVED) {
            if (prior) r->prev_t_reserved = now; else r->t_reserved = now;
        }
        if (new_state == SAMPLE_PAGE_READY) {
            if (prior) r->prev_t_ready = now; else r->t_ready = now;
            r->page_generation = generation;
            const uint32_t need = prior ? r->prev_t_need : r->t_need;
            const uint32_t complete = prior ? r->prev_t_dma_complete : r->t_dma_complete;
            if (need) sample_stream_diag_max(&d->max_need_ready, now - need);
            if (complete) sample_stream_diag_max(&d->max_complete_ready, now - complete);
        }
    }
    if (event == STREAM_DIAG_FAILED || event == STREAM_DIAG_READY)
        sample_stream_diag_trace(event, UINT32_MAX, key, page, 0U, new_state, generation);
}

void sample_stream_diag_scheduler(uint32_t event, uint8_t slot, sample_audio_key_t key,
                                  uint32_t page, uint32_t extra)
{
    volatile sample_stream_diag_t *const d = &g_sample_stream_diag;
    d->scheduler_owner = slot;
    if (event == STREAM_DIAG_RESERVE_FAIL) ++d->reserve_fail;
    if (event == STREAM_DIAG_DELAYED) ++d->delayed;
    if (event == STREAM_DIAG_QUEUE_FULL) ++d->queue_full;
    sample_stream_diag_trace(event, slot, key, page, 0U, 0U, extra);
}

void sample_stream_diag_dma(uint32_t event, uint32_t owner, uint32_t lba, uint32_t extra)
{
    volatile sample_stream_diag_t *const d = &g_sample_stream_diag;
    if (event == STREAM_DIAG_DMA_START) ++d->dma_starts;
    if (event == STREAM_DIAG_DMA_COMPLETE) ++d->dma_completions;
    if (event == STREAM_DIAG_DMA_ERROR) ++d->dma_errors;
    if (event == STREAM_DIAG_QUEUE_FULL) ++d->queue_full;
    if (event == STREAM_DIAG_DELAYED) ++d->busy;
    const uint32_t now = sample_stream_diag_now();
    for (uint32_t i = 0U; i < SAMPLE_STREAM_TARGET_MAX_VOICES; ++i) {
        volatile sample_stream_diag_reader_t *const r = &d->reader[i];
        if (owner == 0U || (r->dma_owner != owner && r->prev_dma_owner != owner)) continue;
        const uint8_t prior = (r->prev_dma_owner == owner && r->dma_owner != owner);
        volatile uint32_t *const start = prior ? &r->prev_t_dma_start : &r->t_dma_start;
        volatile uint32_t *const complete = prior ? &r->prev_t_dma_complete : &r->t_dma_complete;
        const uint32_t need = prior ? r->prev_t_need : r->t_need;
        if (event == STREAM_DIAG_DMA_START && *start == 0U) {
            *start = now;
            if (need) sample_stream_diag_max(&d->max_need_dma, now - need);
        }
        if (event == STREAM_DIAG_DMA_COMPLETE) {
            *complete = now;
            if (*start) sample_stream_diag_max(&d->max_dma_complete, now - *start);
        }
    }
    sample_stream_diag_trace(event, owner, sample_audio_key_classic(0U), lba,
                             0U, sd_block_device_async_hardware_state(), extra);
    (void)now;
}

void sample_stream_diag_dma_owner(uint32_t owner, sample_audio_key_t key, uint32_t page)
{
    for (uint32_t i = 0U; i < SAMPLE_STREAM_TARGET_MAX_VOICES; ++i) {
        volatile sample_stream_diag_reader_t *const r = &g_sample_stream_diag.reader[i];
        if (r->active && sample_audio_key_equal((const sample_audio_key_t *)&r->key, &key)) {
            if (r->need_page == page) r->dma_owner = owner;
            if (r->prev_need_page == page) r->prev_dma_owner = owner;
        }
    }
}
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
static uint8_t sample_stream_manager_candidate_for_slot(
    uint8_t slot,
    sample_stream_scheduler_candidate_t *out_candidate,
    uint8_t *out_pending);
static uint8_t sample_stream_manager_probe_candidate(
    void *context,
    uint8_t slot,
    sample_stream_scheduler_candidate_t *out_candidate);
static uint8_t sample_stream_manager_finish_io(
    sample_stream_manager_pending_io_t *pending,
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
    sample_stream_transport_init();
    sample_stream_diag_init();
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
    g_sample_stream_diag.scheduler_pending = 0U;
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
    (void)sample_stream_transport_request_release(key);
    (void)sample_page_cache_cancel_reserved_key(key, SAMPLE_STREAM_CANCEL_REASON_RELEASE_KEY);
}

uint8_t sample_stream_manager_key_busy(sample_audio_key_t key)
{
    for (uint8_t i = 0U; i < g_sample_stream_manager_pending_count; ++i)
        if ((g_sample_stream_manager_pending_io[i].active != 0U)
                && (sample_audio_key_equal(
                    &g_sample_stream_manager_pending_io[i].target.key,
                    &key) != 0U)) return 1U;
    return 0U;
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

    sample_page_lease_t lease = {0};
    const uint8_t lease_ok = sample_page_lease_control_read(slot, &lease);
    sample_stream_diag_lease_storage(slot, lease_ok,
        g_sample_page_leases[slot].seq, &lease);
    if (lease_ok == 0U) return 0U;
    sample_page_lease_range_t derived = {0};
    const sample_page_lease_range_t *const tail =
        (lease.ranges[1].page_count != 0U)
            ? &lease.ranges[1] : &lease.ranges[0];
    const uint32_t published_pages = (uint32_t)lease.ranges[0].page_count
                                   + lease.ranges[1].page_count;
    sample_page_stream_info_t info;
    if ((published_pages <= 2U)
            && (sample_page_cache_get_stream_info_key(lease.key, &info) != 0U)
            && (info.frames_per_page != 0U))
    {
        const uint32_t total_pages =
            (info.total_frames + info.frames_per_page - 1U)
            / info.frames_per_page;
        derived.first_page = tail->first_page + tail->page_count;
        if (derived.first_page < total_pages)
        {
            derived.page_count =
                (lease.key.domain == SAMPLE_AUDIO_DOMAIN_MULTI)
                    ? SAMPLE_PAGE_MULTI_LOOKAHEAD_PAGES
                    : SAMPLE_PAGE_CLASSIC_FORWARD_LOOKAHEAD_PAGES;
        }
    }

    uint8_t page_rank = 0U;
    for (uint8_t range_index = 0U; range_index < 3U; ++range_index)
    {
        const sample_page_lease_range_t *range =
            (range_index < 2U) ? &lease.ranges[range_index] : &derived;
        for (uint8_t offset = 0U; offset < range->page_count; ++offset, ++page_rank)
        {
            const uint32_t page_index = range->first_page + offset;
            const sample_page_state_t state =
                sample_page_cache_get_page_state_key(lease.key, page_index);
            if (state == SAMPLE_PAGE_READY) {
                sample_stream_diag_boundary_candidate(slot, lease.key, page_index,
                    state, STREAM_BOUNDARY_READY_SKIP);
                continue;
            }

            if (out_pending != 0) *out_pending = 1U;
            if (state == SAMPLE_PAGE_LOADING) {
                sample_stream_diag_boundary_candidate(slot, lease.key, page_index,
                    state, STREAM_BOUNDARY_LOADING_BLOCK);
                sample_stream_diag_boundary_candidate(slot, lease.key, page_index,
                    state, STREAM_BOUNDARY_EARLIER_LOADING);
                return 0U;
            }
            if (out_candidate == 0) {
                sample_stream_diag_boundary_candidate(slot, lease.key, page_index,
                    state, STREAM_BOUNDARY_PENDING_ONLY);
                return 0U;
            }

            memset(out_candidate, 0, sizeof(*out_candidate));
            out_candidate->key = lease.key;
            out_candidate->page_index = page_index;
            out_candidate->registration_epoch = lease.registration_epoch;
            out_candidate->voice_id = slot;
            out_candidate->page_rank = page_rank;
            out_candidate->round_robin_slot = slot;
            out_candidate->active = 1U;
            sample_stream_diag_boundary_candidate(slot, lease.key, page_index,
                state, STREAM_BOUNDARY_CANDIDATE_FOUND);
            sample_stream_diag_boundary_candidate(slot, lease.key, page_index,
                state, STREAM_BOUNDARY_OTHER_CANDIDATE);
            return 1U;
        }
    }
    sample_stream_diag_boundary_candidate(slot, lease.key, UINT32_MAX,
        SAMPLE_PAGE_FREE, STREAM_BOUNDARY_NO_WORK);
    return 0U;
}

static uint8_t sample_stream_manager_probe_candidate(
    void *context,
    uint8_t slot,
    sample_stream_scheduler_candidate_t *out_candidate)
{
    (void)context;
    return sample_stream_manager_candidate_for_slot(slot, out_candidate, 0);
}

static uint8_t sample_stream_manager_pick_next(
    sample_page_load_target_t *out_target,
    sample_stream_scheduler_candidate_t *out_candidate)
{
    if ((out_target == 0) || (out_candidate == 0))
    {
        return 0U;
    }

    sample_stream_scheduler_candidate_t candidate;
    if (sample_stream_scheduler_pick(
            sample_stream_manager_probe_candidate, 0, &candidate) == 0U)
    {
        return 0U;
    }
    g_sample_stream_diag.scheduler_owner = candidate.voice_id;
    const sample_page_state_t state = sample_page_cache_get_page_state_key(
        candidate.key, candidate.page_index);
    uint8_t reserved_here = 0U;
    if ((state == SAMPLE_PAGE_FREE) || (state == SAMPLE_PAGE_FAILED))
    {
        sample_stream_diag_boundary_candidate(candidate.voice_id, candidate.key,
            candidate.page_index, state, STREAM_BOUNDARY_RESERVE_ATTEMPT);
        if (sample_page_cache_reserve_page_key_alloc(
                candidate.key,
                candidate.page_index,
                SAMPLE_PAGE_ALLOC_VOICE_WINDOW) == 0U)
        {
            sample_stream_diag_boundary_candidate(candidate.voice_id, candidate.key,
                candidate.page_index, state, STREAM_BOUNDARY_RESERVE_FAILED);
            sample_stream_diag_scheduler(STREAM_DIAG_RESERVE_FAIL,
                candidate.voice_id, candidate.key, candidate.page_index, state);
            return 0U;
        }
        reserved_here = 1U;
        sample_stream_diag_boundary_candidate(candidate.voice_id, candidate.key,
            candidate.page_index, SAMPLE_PAGE_RESERVED, STREAM_BOUNDARY_RESERVED);
    }

    sample_page_load_target_t target;
    if (sample_page_cache_get_load_target_key(candidate.key,
                                              candidate.page_index,
                                              &target) == 0U)
    {
        if (reserved_here != 0U)
        {
            (void)sample_page_cache_cancel_reserved_page_key(
                candidate.key,
                candidate.page_index,
                SAMPLE_STREAM_CANCEL_REASON_SUPERSEDED);
        }
        return 0U;
    }
    if ((candidate.registration_epoch != 0U)
        && (target.registration_epoch != candidate.registration_epoch))
    {
        (void)sample_page_cache_cancel_reserved_page_key(
            candidate.key,
            candidate.page_index,
            SAMPLE_STREAM_CANCEL_REASON_SUPERSEDED);
        return 0U;
    }
    *out_candidate = candidate;
    *out_target = target;
    return 1U;
}

static uint8_t sample_stream_manager_submit_prefill(
    sample_audio_domain_t domain, uint16_t capacity)
{
    sample_page_load_target_t target;
    if ((g_sample_stream_manager_pending_count >= 2U)
        || (sample_page_cache_get_reserved_load_target_domain_range(
                domain, 0U, capacity, &target) == 0U))
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

static void sample_stream_manager_service_impl(uint32_t byte_budget)
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
            g_sample_stream_diag.scheduler_pending = g_sample_stream_manager_pending_count;
            pages_this_call = (finished != 0U) ? 1U : 0U;
            return;
        }
        if (g_sample_stream_manager_pending_count >= 2U)
        {
            ++g_sample_stream_diag.delayed;
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
        io_command.deadline_margin_us = UINT32_MAX;
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
            sample_stream_diag_scheduler(STREAM_DIAG_QUEUE_FULL,
                candidate.voice_id, candidate.key, candidate.page_index, 0U);
            (void)sample_stream_manager_finish_io(pending, &io_result);
            return;
        }
        pending->active = 1U;
        ++g_sample_stream_manager_pending_count;
        g_sample_stream_diag.scheduler_pending = g_sample_stream_manager_pending_count;
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
        g_sample_stream_diag.scheduler_pending = g_sample_stream_manager_pending_count;
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
    if (sample_stream_manager_submit_prefill(
            SAMPLE_AUDIO_DOMAIN_REC, SAMPLE_PAGE_CACHE_REC_ID_CAPACITY) == 0U)
        (void)sample_stream_manager_submit_prefill(
            SAMPLE_AUDIO_DOMAIN_CLASSIC, SAMPLE_CLASSIC_CAPACITY);
}

void sample_stream_manager_service(uint32_t byte_budget)
{
    ++g_sample_stream_diag.service_calls;
    g_sample_stream_diag.scheduler_pending = g_sample_stream_manager_pending_count;
    g_sample_stream_diag.sd_pending = sd_block_device_async_pending_count();
    g_sample_stream_diag.sd_state = sd_block_device_async_hardware_state();
    sample_stream_manager_service_impl(byte_budget);
}

uint8_t sample_stream_manager_has_pending_sd_work(void)
{
    ++g_sample_stream_diag.gate_polls;
    if (g_sample_stream_manager_pending_count != 0U)
    {
        ++g_sample_stream_diag.gate_pending;
        return 1U;
    }
    for (uint8_t slot = 0U; slot < SAMPLE_PAGE_LEASE_SLOT_COUNT; ++slot)
    {
        uint8_t pending = 0U;
        (void)sample_stream_manager_candidate_for_slot(slot, 0, &pending);
        if (pending != 0U) {
            ++g_sample_stream_diag.gate_pending;
            return 1U;
        }
    }
    sample_page_load_target_t prefill_target;
    if (sample_page_cache_get_reserved_load_target_domain_range(
        SAMPLE_AUDIO_DOMAIN_REC, 0U, SAMPLE_PAGE_CACHE_REC_ID_CAPACITY,
        &prefill_target) != 0U) {
        ++g_sample_stream_diag.gate_pending;
        return 1U;
    }
    const uint8_t prefill = sample_page_cache_get_reserved_load_target_domain_range(
        SAMPLE_AUDIO_DOMAIN_CLASSIC, 0U, SAMPLE_CLASSIC_CAPACITY,
        &prefill_target);
    if (prefill) ++g_sample_stream_diag.gate_pending;
    return prefill;
}

uint8_t sample_stream_manager_io_in_flight(void)
{
    return (g_sample_stream_manager_pending_count != 0U) ? 1U : 0U;
}
