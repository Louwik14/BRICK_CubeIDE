#include "Sampler/sample_stream_underrun_trace.h"

#if BRICK6_STREAM_UNDERRUN_TRACE

#include <string.h>

#include "Sampler/sample_stream_needs.h"
#include "Sampler/sample_stream_time.h"
#include "Storage/memory_layout.h"
#include "stm32h7xx.h"

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(brick6_stream_underrun_trace_event_t) == 64U,
               "underrun trace event ABI must remain fixed-width");
_Static_assert(sizeof(brick6_stream_underrun_trace_snapshot_t)
                   == (76U + (64U * BRICK6_STREAM_UNDERRUN_TRACE_CAPACITY)),
               "underrun trace snapshot ABI must remain fixed-width");
#endif

SDRAM_RECORDER volatile brick6_stream_underrun_trace_snapshot_t
    g_brick6_stream_underrun_trace;

static uint32_t g_trace_service_begin_cycle;
static uint64_t g_trace_service_begin_frame;
static uint32_t g_trace_io_begin_cycle;
static uint32_t g_trace_io_read_bytes;
static uint32_t g_trace_io_cycles;
static uint32_t g_trace_decode_cycles;
static uint32_t g_trace_manager_pages;
static uint32_t g_trace_manager_fatfs_ops;
static uint8_t g_trace_manager_reason;
static uint32_t g_trace_new_needs;

static uint32_t brick6_stream_underrun_trace_record_locked(
    brick6_stream_underrun_trace_type_t type,
    sample_audio_key_t key,
    uint32_t page_index,
    uint32_t generation,
    uint32_t registration_epoch,
    uint8_t source,
    uint8_t voice_id,
    uint8_t state,
    uint8_t reason,
    uint8_t backend,
    uint8_t result,
    uint32_t duration_cycles,
    uint32_t value0,
    uint32_t value1,
    uint32_t value2,
    uint32_t value3)
{
    if (g_brick6_stream_underrun_trace.frozen != 0U)
    {
        return 0U;
    }

    const uint8_t was_triggered =
        (g_brick6_stream_underrun_trace.triggered != 0U) ? 1U : 0U;
    const uint32_t sequence = g_brick6_stream_underrun_trace.write_index + 1U;
    const uint32_t slot = g_brick6_stream_underrun_trace.write_index
                          % BRICK6_STREAM_UNDERRUN_TRACE_CAPACITY;
    brick6_stream_underrun_trace_event_t *const event =
        (brick6_stream_underrun_trace_event_t *)&g_brick6_stream_underrun_trace.events[slot];
    const sample_stream_audio_frame_t frame = sample_stream_time_now();
    *event = (brick6_stream_underrun_trace_event_t){
        .sequence = sequence,
        .audio_frame_low = (uint32_t)frame,
        .audio_frame_high = (uint32_t)(frame >> 32U),
        .cycle = DWT->CYCCNT,
        .duration_cycles = duration_cycles,
        .key_domain = (uint32_t)key.domain,
        .key_object_id = key.object_id,
        .page_index = page_index,
        .generation = generation,
        .registration_epoch = registration_epoch,
        .value0 = value0,
        .value1 = value1,
        .value2 = value2,
        .value3 = value3,
        .type = (uint8_t)type,
        .source = source,
        .voice_id = voice_id,
        .state = state,
        .reason = reason,
        .backend = backend,
        .result = result,
    };
    __DMB();
    g_brick6_stream_underrun_trace.write_index = sequence;
    if (g_brick6_stream_underrun_trace.count < BRICK6_STREAM_UNDERRUN_TRACE_CAPACITY)
    {
        g_brick6_stream_underrun_trace.count++;
    }
    else
    {
        g_brick6_stream_underrun_trace.dropped_count++;
    }
    if (type == BRICK6_STREAM_TRACE_CONSUME_MISS)
    {
        g_brick6_stream_underrun_trace.last_miss_sequence = sequence;
    }
    if ((type == BRICK6_STREAM_TRACE_CONSUME_MISS) && (was_triggered == 0U))
    {
        g_brick6_stream_underrun_trace.triggered = 1U;
        g_brick6_stream_underrun_trace.trigger_type = (uint32_t)type;
        g_brick6_stream_underrun_trace.trigger_source = source;
        g_brick6_stream_underrun_trace.trigger_voice_id = voice_id;
        g_brick6_stream_underrun_trace.trigger_key_domain = (uint32_t)key.domain;
        g_brick6_stream_underrun_trace.trigger_key_object_id = key.object_id;
        g_brick6_stream_underrun_trace.trigger_page = page_index;
        g_brick6_stream_underrun_trace.trigger_audio_frame_low = (uint32_t)frame;
        g_brick6_stream_underrun_trace.trigger_audio_frame_high = (uint32_t)(frame >> 32U);
        g_brick6_stream_underrun_trace.post_trigger_remaining =
            BRICK6_STREAM_UNDERRUN_TRACE_POST_EVENTS;
        if (g_brick6_stream_underrun_trace.post_trigger_remaining == 0U)
        {
            g_brick6_stream_underrun_trace.frozen = 1U;
        }
    }
    else if (was_triggered != 0U)
    {
        if (g_brick6_stream_underrun_trace.post_trigger_remaining != 0U)
        {
            g_brick6_stream_underrun_trace.post_trigger_remaining--;
        }
        if (g_brick6_stream_underrun_trace.post_trigger_remaining == 0U)
        {
            g_brick6_stream_underrun_trace.frozen = 1U;
        }
    }
    return sequence;
}

static uint32_t brick6_stream_underrun_trace_record(
    brick6_stream_underrun_trace_type_t type,
    sample_audio_key_t key,
    uint32_t page_index,
    uint32_t generation,
    uint32_t registration_epoch,
    uint8_t source,
    uint8_t voice_id,
    uint8_t state,
    uint8_t reason,
    uint8_t backend,
    uint8_t result,
    uint32_t duration_cycles,
    uint32_t value0,
    uint32_t value1,
    uint32_t value2,
    uint32_t value3)
{
    if (g_brick6_stream_underrun_trace.frozen != 0U)
    {
        return 0U;
    }
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const uint32_t sequence = brick6_stream_underrun_trace_record_locked(
        type, key, page_index, generation, registration_epoch, source, voice_id, state,
        reason, backend, result, duration_cycles, value0, value1, value2, value3);
    if (primask == 0U)
    {
        __enable_irq();
    }
    return sequence;
}

static uint32_t brick6_stream_underrun_trace_find_cause(sample_audio_key_t key,
                                                        uint32_t page_index,
                                                        uint32_t generation,
                                                        uint32_t epoch,
                                                        uint8_t source,
                                                        uint8_t voice_id)
{
    uint32_t count = g_brick6_stream_underrun_trace.count;
    if (count > BRICK6_STREAM_UNDERRUN_TRACE_CAPACITY)
    {
        count = BRICK6_STREAM_UNDERRUN_TRACE_CAPACITY;
    }
    uint32_t sequence = g_brick6_stream_underrun_trace.write_index;
    for (uint32_t i = 0U; i < count; ++i)
    {
        const uint32_t slot = (sequence - 1U) % BRICK6_STREAM_UNDERRUN_TRACE_CAPACITY;
        const brick6_stream_underrun_trace_event_t *const event =
            (const brick6_stream_underrun_trace_event_t *)&g_brick6_stream_underrun_trace.events[slot];
        if ((event->sequence == sequence)
            && (event->source == source)
            && (event->voice_id == voice_id)
            && (event->key_domain == (uint32_t)key.domain)
            && (event->key_object_id == key.object_id)
            && (event->page_index == page_index)
            && ((generation == 0U) || (event->generation == generation))
            && ((epoch == 0U) || (event->registration_epoch == epoch))
            && ((event->type == BRICK6_STREAM_TRACE_NEED_CREATED)
                || (event->type == BRICK6_STREAM_TRACE_NEED_SELECTABLE)
                || (event->type == BRICK6_STREAM_TRACE_SCHEDULER_DECISION)
                || (event->type == BRICK6_STREAM_TRACE_LOAD_BEGIN)
                || (event->type == BRICK6_STREAM_TRACE_LOAD_END)
                || (event->type == BRICK6_STREAM_TRACE_READY)))
        {
            return sequence;
        }
        sequence--;
    }
    return 0U;
}

void brick6_stream_underrun_trace_reset(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    memset((void *)&g_brick6_stream_underrun_trace, 0,
           sizeof(g_brick6_stream_underrun_trace));
    g_brick6_stream_underrun_trace.magic = BRICK6_STREAM_UNDERRUN_TRACE_MAGIC;
    g_brick6_stream_underrun_trace.abi_version = BRICK6_STREAM_UNDERRUN_TRACE_ABI_VERSION;
    g_brick6_stream_underrun_trace.event_size = sizeof(brick6_stream_underrun_trace_event_t);
    g_brick6_stream_underrun_trace.capacity = BRICK6_STREAM_UNDERRUN_TRACE_CAPACITY;
    g_trace_service_begin_cycle = 0U;
    g_trace_service_begin_frame = 0U;
    g_trace_io_begin_cycle = 0U;
    g_trace_io_read_bytes = 0U;
    g_trace_io_cycles = 0U;
    g_trace_decode_cycles = 0U;
    g_trace_manager_pages = 0U;
    g_trace_manager_fatfs_ops = 0U;
    g_trace_manager_reason = BRICK6_STREAM_TRACE_REASON_NONE;
    g_trace_new_needs = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }
}

void brick6_stream_underrun_trace_voice_state(
    sample_stream_snapshot_source_t source,
    uint8_t voice_id,
    const sample_stream_snapshot_t *snapshot,
    const sample_stream_target_voice_registry_entry_t *entry)
{
    if ((snapshot == 0) || (entry == 0))
    {
        return;
    }

    const uint32_t frames_per_page = (snapshot->frames_per_page != 0U)
                                         ? snapshot->frames_per_page : 1U;
    uint32_t ready_end = snapshot->current_frame;
    uint32_t first_missing = UINT32_MAX;
    for (uint8_t i = 0U; i < entry->need_count; ++i)
    {
        const sample_stream_target_voice_need_t *const need = &entry->needs[i];
        if (need->valid == 0U)
        {
            continue;
        }
        const uint8_t page_exists = sample_page_cache_page_exists_key(
            need->key, need->page_index);
        const sample_page_state_t page_state = sample_page_cache_get_page_state_key(
            need->key, need->page_index);
        const uint8_t state = (page_exists != 0U)
                                  ? (uint8_t)page_state
                                  : BRICK6_STREAM_TRACE_STATE_ABSENT;
        const uint32_t frames_ahead =
            (need->consume_deadline_audio_frame > sample_stream_time_now())
                ? (uint32_t)((need->consume_deadline_audio_frame
                              - sample_stream_time_now()) > UINT32_MAX
                                 ? UINT32_MAX
                                 : (need->consume_deadline_audio_frame
                                    - sample_stream_time_now()))
                : 0U;
        brick6_stream_underrun_trace_record(
            BRICK6_STREAM_TRACE_VOICE_STATE,
            need->key,
            need->page_index,
            entry->generation,
            need->registration_epoch,
            (uint8_t)source,
            voice_id,
            state,
            BRICK6_STREAM_TRACE_REASON_NONE,
            0U,
            0U,
            0U,
            snapshot->current_frame,
            snapshot->step_q16,
            frames_ahead,
            ((uint32_t)need->role << 24U) | entry->need_count);

        if ((first_missing == UINT32_MAX) && (state != SAMPLE_PAGE_READY))
        {
            first_missing = need->page_index;
        }
        else if (first_missing == UINT32_MAX)
        {
            const uint32_t page_start = need->page_index * frames_per_page;
            ready_end = page_start + frames_per_page;
        }
    }
    const uint32_t ready_frames = (ready_end > snapshot->current_frame)
                                      ? (ready_end - snapshot->current_frame)
                                      : 0U;

    brick6_stream_underrun_trace_record(
        BRICK6_STREAM_TRACE_VOICE_STATE,
        snapshot->key,
        first_missing,
        snapshot->generation,
        snapshot->registration_epoch,
        (uint8_t)source,
        voice_id,
        snapshot->active,
        BRICK6_STREAM_TRACE_REASON_NONE,
        0U,
        0U,
        0U,
        snapshot->current_frame,
        snapshot->step_q16,
        ready_frames,
        ((uint32_t)entry->need_count << 24U)
            | ((uint32_t)(snapshot->loop_enabled != 0U) << 16U));
}

void brick6_stream_underrun_trace_need_created(
    sample_stream_snapshot_source_t source,
    uint8_t voice_id,
    uint32_t generation,
    const sample_stream_target_voice_need_t *need,
    uint32_t frames_ahead)
{
    if ((need == 0) || (need->valid == 0U))
    {
        return;
    }
    g_trace_new_needs++;
    brick6_stream_underrun_trace_record(
        BRICK6_STREAM_TRACE_NEED_CREATED,
        need->key,
        need->page_index,
        generation,
        need->registration_epoch,
        (uint8_t)source,
        voice_id,
        0U,
        BRICK6_STREAM_TRACE_REASON_NONE,
        0U,
        0U,
        0U,
        (uint32_t)need->role,
        frames_ahead,
        0U,
        0U);
}

void brick6_stream_underrun_trace_need_selectable(
    const sample_stream_scheduler_candidate_t *candidate,
    sample_page_state_t state,
    uint32_t candidate_count)
{
    if (candidate == 0)
    {
        return;
    }
    const uint64_t now = sample_stream_time_now();
    const uint32_t frames_ahead =
        (candidate->consume_deadline_audio_frame > now)
            ? (uint32_t)((candidate->consume_deadline_audio_frame - now) > UINT32_MAX
                             ? UINT32_MAX
                             : (candidate->consume_deadline_audio_frame - now))
            : 0U;
    brick6_stream_underrun_trace_record(
        BRICK6_STREAM_TRACE_NEED_SELECTABLE,
        candidate->key,
        candidate->page_index,
        candidate->voice_generation,
        candidate->registration_epoch,
        candidate->source,
        candidate->voice_id,
        (uint8_t)state,
        BRICK6_STREAM_TRACE_REASON_NONE,
        0U,
        0U,
        0U,
        candidate->advance,
        frames_ahead,
        candidate_count,
        candidate->round_robin_slot);
}

void brick6_stream_underrun_trace_scheduler(
    const sample_stream_scheduler_candidate_t *candidate,
    const sample_stream_scheduler_decision_t *decision,
    uint32_t candidate_count,
    uint32_t critical_voices,
    uint32_t loadable_needs,
    uint8_t reason)
{
    sample_audio_key_t key = { 0U, 0U };
    uint32_t page = UINT32_MAX;
    uint32_t generation = 0U;
    uint32_t epoch = 0U;
    uint8_t source = UINT8_MAX;
    uint8_t voice = UINT8_MAX;
    uint32_t advance = UINT32_MAX;
    uint32_t remaining = 0U;
    if (candidate != 0)
    {
        key = candidate->key;
        page = candidate->page_index;
        generation = candidate->voice_generation;
        epoch = candidate->registration_epoch;
        source = candidate->source;
        voice = candidate->voice_id;
        advance = candidate->advance;
    }
    if (decision != 0)
    {
        advance = decision->advance;
        const uint64_t now = sample_stream_time_now();
        remaining = (decision->consume_deadline_audio_frame > now)
                        ? (uint32_t)((decision->consume_deadline_audio_frame - now)
                                     > UINT32_MAX
                                         ? UINT32_MAX
                                         : (decision->consume_deadline_audio_frame - now))
                        : 0U;
    }
    brick6_stream_underrun_trace_record(
        BRICK6_STREAM_TRACE_SCHEDULER_DECISION,
        key,
        page,
        generation,
        epoch,
        source,
        voice,
        0U,
        reason,
        0U,
        (decision != 0) ? 1U : 0U,
        0U,
        candidate_count,
        critical_voices,
        loadable_needs,
        (advance == UINT32_MAX) ? remaining : ((advance & 0xFFFFU) | (remaining << 16U)));
}

void brick6_stream_underrun_trace_load_begin(
    const sample_stream_scheduler_candidate_t *candidate,
    const sample_page_load_target_t *target,
    const sample_page_load_token_t *token)
{
    if ((candidate == 0) || (target == 0) || (token == 0))
    {
        return;
    }
    brick6_stream_underrun_trace_record(
        BRICK6_STREAM_TRACE_LOAD_BEGIN,
        target->key,
        target->page_index,
        token->page_generation,
        token->registration_epoch,
        candidate->source,
        candidate->voice_id,
        SAMPLE_PAGE_LOADING,
        BRICK6_STREAM_TRACE_REASON_NONE,
        0U,
        0U,
        0U,
        target->frame_count,
        target->slot_index,
        candidate->advance,
        0U);
}

void brick6_stream_underrun_trace_io_begin(
    const sample_stream_scheduler_candidate_t *candidate,
    const sample_stream_io_command_t *command)
{
    if ((candidate == 0) || (command == 0))
    {
        return;
    }
    g_trace_io_begin_cycle = DWT->CYCCNT;
    brick6_stream_underrun_trace_record(
        BRICK6_STREAM_TRACE_IO_BEGIN,
        command->target.key,
        command->target.page_index,
        command->token.page_generation,
        command->token.registration_epoch,
        candidate->source,
        candidate->voice_id,
        SAMPLE_PAGE_LOADING,
        BRICK6_STREAM_TRACE_REASON_NONE,
        command->stream_info.stream_safe.backend_kind,
        0U,
        0U,
        command->target.frame_count * command->stream_info.info.block_align,
        command->target.start_frame,
        command->token.slot_index,
        0U);
}

void brick6_stream_underrun_trace_io_end(
    const sample_stream_scheduler_candidate_t *candidate,
    const sample_stream_io_command_t *command,
    const sample_stream_io_result_t *result,
    uint32_t duration_cycles)
{
    if ((candidate == 0) || (command == 0) || (result == 0))
    {
        return;
    }
    const uint32_t io_cycles = (duration_cycles != 0U)
                                   ? duration_cycles
                                   : (DWT->CYCCNT - g_trace_io_begin_cycle);
    g_trace_io_read_bytes += result->read_bytes;
    g_trace_io_cycles += io_cycles;
    g_trace_decode_cycles += result->decode_cycles;
    brick6_stream_underrun_trace_record(
        BRICK6_STREAM_TRACE_IO_END,
        command->target.key,
        command->target.page_index,
        result->token.page_generation,
        result->token.registration_epoch,
        candidate->source,
        candidate->voice_id,
        SAMPLE_PAGE_LOADING,
        (result->load_result == SAMPLE_PAGE_LOAD_OK)
            ? BRICK6_STREAM_TRACE_REASON_NONE : BRICK6_STREAM_TRACE_REASON_LOAD_ERROR,
        result->backend,
        (uint8_t)result->load_result,
        io_cycles,
        result->read_bytes,
        result->physical_reads,
        result->seeks,
        ((uint32_t)result->fatfs_ops << 16U) | result->file_opens);
}

void brick6_stream_underrun_trace_load_end(
    const sample_stream_scheduler_candidate_t *candidate,
    const sample_stream_io_result_t *result,
    uint8_t reason)
{
    if ((candidate == 0) || (result == 0))
    {
        return;
    }
    brick6_stream_underrun_trace_record(
        BRICK6_STREAM_TRACE_LOAD_END,
        result->token.key,
        result->token.page_index,
        result->token.page_generation,
        result->token.registration_epoch,
        candidate->source,
        candidate->voice_id,
        SAMPLE_PAGE_LOADING,
        reason,
        result->backend,
        (uint8_t)result->load_result,
        0U,
        result->read_bytes,
        result->physical_reads,
        result->seeks,
        result->decode_cycles);
}

void brick6_stream_underrun_trace_ready(
    const sample_stream_scheduler_candidate_t *candidate,
    const sample_page_load_target_t *target,
    const sample_stream_io_result_t *result)
{
    if ((candidate == 0) || (target == 0) || (result == 0))
    {
        return;
    }
    brick6_stream_underrun_trace_record(
        BRICK6_STREAM_TRACE_READY,
        target->key,
        target->page_index,
        target->page_generation,
        target->registration_epoch,
        candidate->source,
        candidate->voice_id,
        SAMPLE_PAGE_READY,
        BRICK6_STREAM_TRACE_REASON_NONE,
        result->backend,
        0U,
        0U,
        target->frame_count,
        result->read_bytes,
        result->decode_cycles,
        target->slot_index);
}

void brick6_stream_underrun_trace_consume_miss(sample_audio_key_t key,
                                               uint32_t page_index,
                                               uint32_t reader_position,
                                               uint32_t frames_remaining)
{
    const uint8_t state = (sample_page_cache_page_exists_key(key, page_index) != 0U)
                              ? (uint8_t)sample_page_cache_get_page_state_key(key, page_index)
                              : BRICK6_STREAM_TRACE_STATE_ABSENT;
    uint8_t found = 0U;
    for (uint8_t source = (uint8_t)SAMPLE_STREAM_SNAPSHOT_CLASSIC;
         source <= (uint8_t)SAMPLE_STREAM_SNAPSHOT_MULTI;
         ++source)
    {
        const uint8_t capacity = (source == (uint8_t)SAMPLE_STREAM_SNAPSHOT_CLASSIC)
                                     ? SAMPLE_STREAM_SNAPSHOT_CLASSIC_CAPACITY
                                     : SAMPLE_STREAM_SNAPSHOT_MULTI_CAPACITY;
        for (uint8_t voice_id = 0U; voice_id < capacity; ++voice_id)
        {
            sample_stream_target_voice_registry_entry_t entry;
            if (sample_stream_needs_registry_read(
                    (sample_stream_snapshot_source_t)source, voice_id, &entry) == 0U)
            {
                continue;
            }
            for (uint8_t i = 0U; i < entry.need_count; ++i)
            {
                const sample_stream_target_voice_need_t *const need = &entry.needs[i];
                if ((need->valid == 0U)
                    || (need->page_index != page_index)
                    || (sample_audio_key_equal(&need->key, &key) == 0U))
                {
                    continue;
                }
                found = 1U;
                const uint32_t cause = brick6_stream_underrun_trace_find_cause(
                    key, page_index, entry.generation, need->registration_epoch,
                    source, voice_id);
                brick6_stream_underrun_trace_record(
                    BRICK6_STREAM_TRACE_CONSUME_MISS,
                    key,
                    page_index,
                    entry.generation,
                    need->registration_epoch,
                    source,
                    voice_id,
                    state,
                    BRICK6_STREAM_TRACE_REASON_NONE,
                    0U,
                    1U,
                    0U,
                    reader_position,
                    frames_remaining,
                    (uint32_t)need->role,
                    cause);
            }
        }
    }
    if (found == 0U)
    {
        brick6_stream_underrun_trace_record(
            BRICK6_STREAM_TRACE_CONSUME_MISS,
            key,
            page_index,
            0U,
            0U,
            UINT8_MAX,
            UINT8_MAX,
            state,
            BRICK6_STREAM_TRACE_REASON_NO_ACTIVE_NEED,
            0U,
            1U,
            0U,
            reader_position,
            frames_remaining,
            0U,
            0U);
    }
}

void brick6_stream_underrun_trace_page_state(const sample_page_desc_t *page,
                                             sample_page_state_t old_state,
                                             sample_page_state_t new_state)
{
    if ((page == 0) || (old_state == new_state))
    {
        return;
    }
    brick6_stream_underrun_trace_record(
        BRICK6_STREAM_TRACE_PAGE_STATE,
        page->key,
        page->page_index,
        page->generation,
        page->registration_epoch,
        UINT8_MAX,
        UINT8_MAX,
        (uint8_t)new_state,
        BRICK6_STREAM_TRACE_REASON_NONE,
        0U,
        0U,
        0U,
        old_state,
        new_state,
        0U,
        0U);
}

void brick6_stream_underrun_trace_service_begin(uint32_t byte_budget,
                                                uint32_t pending_needs,
                                                uint32_t wake_sequence,
                                                uint32_t interval_frames,
                                                uint8_t gate_owner)
{
    g_trace_service_begin_cycle = DWT->CYCCNT;
    g_trace_service_begin_frame = sample_stream_time_now();
    g_trace_io_read_bytes = 0U;
    g_trace_io_cycles = 0U;
    g_trace_decode_cycles = 0U;
    g_trace_manager_pages = 0U;
    g_trace_manager_fatfs_ops = 0U;
    g_trace_manager_reason = BRICK6_STREAM_TRACE_REASON_NONE;
    g_trace_new_needs = 0U;
    brick6_stream_underrun_trace_record(
        BRICK6_STREAM_TRACE_SERVICE_BEGIN,
        (sample_audio_key_t){ 0U, 0U },
        UINT32_MAX,
        0U,
        0U,
        UINT8_MAX,
        UINT8_MAX,
        0U,
        BRICK6_STREAM_TRACE_REASON_NONE,
        gate_owner,
        0U,
        0U,
        byte_budget,
        pending_needs,
        wake_sequence,
        interval_frames);
}

void brick6_stream_underrun_trace_service_blocked(uint8_t reason,
                                                  uint8_t gate_owner,
                                                  uint32_t poll_delay_frames)
{
    g_trace_manager_reason = reason;
    brick6_stream_underrun_trace_record(
        BRICK6_STREAM_TRACE_SERVICE_BLOCKED,
        (sample_audio_key_t){ 0U, 0U },
        UINT32_MAX,
        0U,
        0U,
        UINT8_MAX,
        UINT8_MAX,
        0U,
        reason,
        gate_owner,
        1U,
        0U,
        poll_delay_frames,
        0U,
        0U,
        0U);
}

void brick6_stream_underrun_trace_manager_end(uint32_t pages,
                                              uint32_t fatfs_ops,
                                              uint8_t reason)
{
    g_trace_manager_pages = pages;
    g_trace_manager_fatfs_ops = fatfs_ops;
    g_trace_manager_reason = reason;
    brick6_stream_underrun_trace_record(
        BRICK6_STREAM_TRACE_MANAGER_END,
        (sample_audio_key_t){ 0U, 0U },
        UINT32_MAX,
        0U,
        0U,
        UINT8_MAX,
        UINT8_MAX,
        0U,
        reason,
        0U,
        0U,
        DWT->CYCCNT - g_trace_service_begin_cycle,
        pages,
        fatfs_ops,
        g_trace_io_read_bytes,
        g_trace_decode_cycles);
}

void brick6_stream_underrun_trace_service_end(uint8_t reason,
                                              uint32_t pending_needs,
                                              uint8_t critical_active)
{
    if (reason == BRICK6_STREAM_TRACE_REASON_NONE)
    {
        reason = g_trace_manager_reason;
    }
    const uint32_t packed_pages_fatfs =
        (g_trace_manager_pages & 0xFFFFU)
        | ((g_trace_manager_fatfs_ops & 0xFFFFU) << 16U);
    const uint64_t now_frame = sample_stream_time_now();
    const uint32_t elapsed_frames =
        (now_frame >= g_trace_service_begin_frame)
            ? (uint32_t)((now_frame - g_trace_service_begin_frame) > UINT32_MAX
                             ? UINT32_MAX : (now_frame - g_trace_service_begin_frame))
            : 0U;
    brick6_stream_underrun_trace_record(
        BRICK6_STREAM_TRACE_SERVICE_END,
        (sample_audio_key_t){ 0U, 0U },
        elapsed_frames,
        0U,
        0U,
        g_trace_new_needs > UINT8_MAX ? UINT8_MAX : (uint8_t)g_trace_new_needs,
        critical_active,
        0U,
        reason,
        0U,
        0U,
        DWT->CYCCNT - g_trace_service_begin_cycle,
        packed_pages_fatfs,
        g_trace_io_read_bytes,
        g_trace_io_cycles,
        g_trace_decode_cycles);
}

#endif
