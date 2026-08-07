#include "NoteFx/note_fx_engine.h"

#include <string.h>

#include "NoteFx/note_fx_euclid.h"
#include "Seq/seq_division_catalog.h"

typedef struct {
    uint8_t active, note, destination, source_provenance;
    uint32_t source_token, source_generation;
    uint32_t token, generation;
    uint64_t off_sample;
} note_fx_owned_t;

typedef struct {
    uint8_t active, note, velocity, destination;
    uint8_t provenance, closing;
    uint32_t token, generation;
    uint64_t close_sample;
} note_fx_euclid_source_t;

typedef struct {
    note_fx_arp_t arp;
    note_fx_euclid_source_t euclid_source[NOTE_FX_EUCLID_MAX_SOURCES];
    note_fx_owned_t owned[NOTE_FX_EUCLID_MAX_OWNED];
    uint64_t next_sample;
    uint32_t generation;
    uint32_t pending_close_token, pending_close_generation;
    uint64_t euclid_mask;
    uint8_t euclid_length, euclid_pulse, euclid_division, euclid_phase;
    uint8_t euclid_source_count;
    uint8_t euclid_reconfigure_pending;
    uint8_t model, rate, style, range, destination, closing;
    uint8_t pending_source_close;
} note_fx_slot_runtime_t;

static note_fx_slot_runtime_t g_slot[NOTE_FX_TRACK_COUNT][NOTE_FX_SLOT_COUNT];
static note_fx_diag_t g_diag[NOTE_FX_TRACK_COUNT];
static uint32_t g_token;

static uint8_t slot_owned_count(const note_fx_slot_runtime_t *runtime)
{
    uint8_t count = 0U;
    for (uint8_t i = 0U; i < NOTE_FX_EUCLID_MAX_OWNED; ++i)
        count = (uint8_t)(count + (runtime->owned[i].active != 0U));
    return count;
}

static void note_fx_diag_record_saturation(uint8_t track, uint8_t slot,
                                           note_fx_diag_cause_t cause)
{
    if (track >= NOTE_FX_TRACK_COUNT)
        return;
    ++g_diag[track].saturations;
    ++g_diag[track].dropped_note_ons;
    if (slot >= NOTE_FX_SLOT_COUNT)
        return;
    ++g_diag[track].slot[slot].saturations;
    ++g_diag[track].slot[slot].dropped_note_ons;
    if (cause < NOTE_FX_DIAG_CAUSE_COUNT)
        ++g_diag[track].slot[slot].cause_count[cause];
}

static void note_fx_diag_record_emit_reject(uint8_t track, uint8_t slot,
                                            uint8_t is_note_on)
{
    if ((track >= NOTE_FX_TRACK_COUNT) || (slot >= NOTE_FX_SLOT_COUNT))
        return;
    ++g_diag[track].slot[slot].cause_count[NOTE_FX_DIAG_CAUSE_EMIT_REJECT];
    if (is_note_on != 0U)
    {
        ++g_diag[track].saturations;
        ++g_diag[track].dropped_note_ons;
        ++g_diag[track].slot[slot].saturations;
        ++g_diag[track].slot[slot].dropped_note_ons;
    }
}

static void note_fx_diag_update_usage(uint8_t track, uint8_t slot,
                                      const note_fx_slot_runtime_t *runtime)
{
    if ((track >= NOTE_FX_TRACK_COUNT) || (slot >= NOTE_FX_SLOT_COUNT))
        return;
    note_fx_slot_diag_t *const diag = &g_diag[track].slot[slot];
    const uint8_t owned_count = slot_owned_count(runtime);
    if (runtime->euclid_source_count > diag->source_high_water)
        diag->source_high_water = runtime->euclid_source_count;
    if (owned_count > diag->owned_high_water)
        diag->owned_high_water = owned_count;
}

static uint32_t next_fx_token(void)
{
    g_token = (g_token + 1U) & NOTE_EVENT_OCCURRENCE_COUNTER_MASK;
    if (g_token == 0U)
        g_token = 1U;
    return NOTE_EVENT_OCCURRENCE_NAMESPACE_FX | g_token;
}

static uint8_t slot_has_owned(const note_fx_slot_runtime_t *runtime)
{
    for (uint8_t i = 0U; i < NOTE_FX_EUCLID_MAX_OWNED; ++i)
    {
        if (runtime->owned[i].active != 0U)
            return 1U;
    }
    return 0U;
}

static int8_t euclid_source_find(const note_fx_slot_runtime_t *runtime,
                                 uint32_t token, uint32_t generation,
                                 uint8_t provenance)
{
    for (uint8_t i = 0U; i < runtime->euclid_source_count; ++i)
    {
        const note_fx_euclid_source_t *const source =
            &runtime->euclid_source[i];
        if ((source->active != 0U)
                && (source->token == token)
                && (source->generation == generation)
                && (source->provenance == provenance))
        {
            return (int8_t)i;
        }
    }
    return -1;
}

static void euclid_source_remove(note_fx_slot_runtime_t *runtime,
                                 uint8_t index)
{
    if (index >= runtime->euclid_source_count)
        return;
    for (uint8_t i = (uint8_t)(index + 1U);
         i < runtime->euclid_source_count; ++i)
    {
        runtime->euclid_source[i - 1U] = runtime->euclid_source[i];
    }
    --runtime->euclid_source_count;
    if (runtime->euclid_source_count == 0U)
    {
        runtime->euclid_phase = 0U;
        if (slot_has_owned(runtime) == 0U)
            runtime->next_sample = 0U;
    }
}

static uint32_t owned_child_source_token(const note_fx_owned_t *owned)
{
    return owned->token;
}

uint8_t note_fx_engine_is_generated_occurrence_current(
    uint8_t track, uint32_t occurrence_id, uint32_t generation)
{
    if ((track >= NOTE_FX_TRACK_COUNT) || (occurrence_id == 0U)
            || (generation == 0U))
    {
        return 0U;
    }

    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        const note_fx_slot_runtime_t *const runtime = &g_slot[track][slot];
        if (runtime->closing != 0U)
            continue;
        for (uint8_t i = 0U; i < NOTE_FX_MAX_OUTPUTS; ++i)
        {
            const note_fx_owned_t *const owned = &runtime->owned[i];
            if ((owned->active != 0U)
                    && (owned->token == occurrence_id)
                    && (owned->generation == generation))
            {
                return 1U;
            }
        }
    }
    return 0U;
}

static uint8_t closure_is_settled(note_fx_result_t result)
{
    return (result == NOTE_EVENT_RESULT_ACCEPTED)
        || (result == NOTE_EVENT_RESULT_REJECTED_STALE);
}

static void release_slot(uint8_t track, uint8_t slot, uint64_t sample,
                         note_fx_emit_fn emit, void *context)
{
    note_fx_slot_runtime_t *const runtime = &g_slot[track][slot];
    ++runtime->generation;
    if (runtime->generation == 0U)
        runtime->generation = 1U;
    for (uint8_t i = 0U; i < NOTE_FX_MAX_OUTPUTS; ++i) {
        note_fx_owned_t *const owned = &runtime->owned[i];
        if (owned->active == 0U) continue;
        const note_fx_event_t event = {
            .sample_abs = sample,
            .track = track,
            .destination_id = owned->destination,
            .note = owned->note,
            .velocity = 0U,
            .kind = NOTE_EVENT_KIND_OFF,
            .provenance = NOTE_EVENT_SOURCE_FX,
            .stage = (uint8_t)(slot + 1U),
            .flags = NOTE_EVENT_FLAG_GENERATED | NOTE_EVENT_FLAG_CLOSURE_RESERVED,
            .source_token = owned_child_source_token(owned),
            .occurrence_id = owned->token,
            .generation = owned->generation
        };
        const note_fx_result_t result = (emit != 0)
            ? emit(&event, context) : NOTE_EVENT_RESULT_ACCEPTED;
        if (closure_is_settled(result) != 0U)
            owned->active = 0U;
        else
            note_fx_diag_record_emit_reject(track, slot, 0U);
    }
    note_fx_arp_init(&runtime->arp, 0x9E3779B9U ^ ((uint32_t)track << 8) ^ slot);
    memset(runtime->euclid_source, 0, sizeof(runtime->euclid_source));
    runtime->euclid_source_count = 0U;
    runtime->euclid_phase = 0U;
    runtime->euclid_reconfigure_pending = 0U;
    runtime->pending_source_close = 0U;
    runtime->closing = 0U;
    for (uint8_t i = 0U; i < NOTE_FX_EUCLID_MAX_OWNED; ++i)
        runtime->closing |= runtime->owned[i].active;
    runtime->next_sample = (runtime->closing != 0U) ? sample : 0U;
}

void note_fx_engine_init(void)
{
    memset(g_slot, 0, sizeof(g_slot));
    memset(g_diag, 0, sizeof(g_diag));
    g_token = 0U;
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
        for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot) {
            g_slot[track][slot].range = 1U;
            g_slot[track][slot].euclid_length = NOTE_FX_EUCLID_LENGTH_DEFAULT;
            g_slot[track][slot].euclid_pulse = NOTE_FX_EUCLID_PULSE_DEFAULT;
            g_slot[track][slot].euclid_division = SEQ_DIVISION_ARP_DEFAULT_INDEX;
            g_slot[track][slot].euclid_mask = euclid_build_mask(
                g_slot[track][slot].euclid_length,
                g_slot[track][slot].euclid_pulse);
            g_slot[track][slot].generation = 1U;
            note_fx_arp_init(&g_slot[track][slot].arp,
                             0x9E3779B9U ^ ((uint32_t)track << 8) ^ slot);
        }
}

void note_fx_engine_configure(uint8_t track, uint8_t slot, uint8_t model,
                              uint8_t rate, uint8_t style, uint8_t range)
{
    if (track >= NOTE_FX_TRACK_COUNT || slot >= NOTE_FX_SLOT_COUNT) return;
    note_fx_slot_runtime_t *const runtime = &g_slot[track][slot];
    const uint8_t previous_model = runtime->model;
    const uint8_t target_model = (model < NOTE_FX_MODEL_COUNT)
        ? model : NOTE_FX_MODEL_OFF;

    if (target_model == NOTE_FX_MODEL_EUCLID)
    {
        const uint8_t length = (rate >= NOTE_FX_EUCLID_LENGTH_MIN)
            ? ((rate <= NOTE_FX_EUCLID_LENGTH_MAX)
                ? rate : NOTE_FX_EUCLID_LENGTH_MAX)
            : NOTE_FX_EUCLID_LENGTH_DEFAULT;
        const uint8_t pulse = (style <= length) ? style : length;
        const uint8_t division = (range < SEQ_DIVISION_ARP_COUNT)
            ? range : SEQ_DIVISION_ARP_DEFAULT_INDEX;
        const uint8_t changed = (previous_model != NOTE_FX_MODEL_EUCLID)
            || (runtime->euclid_length != length)
            || (runtime->euclid_pulse != pulse)
            || (runtime->euclid_division != division);

        if (previous_model != NOTE_FX_MODEL_EUCLID)
        {
            note_fx_arp_init(&runtime->arp,
                             0x9E3779B9U ^ ((uint32_t)track << 8) ^ slot);
            memset(runtime->euclid_source, 0, sizeof(runtime->euclid_source));
            runtime->euclid_source_count = 0U;
            if (slot_has_owned(runtime) != 0U)
            {
                runtime->closing = 1U;
                runtime->next_sample = 0U;
            }
        }
        runtime->model = target_model;
        runtime->euclid_length = length;
        runtime->euclid_pulse = pulse;
        runtime->euclid_division = division;
        if (changed != 0U)
        {
            runtime->euclid_mask = euclid_build_mask(length, pulse);
            runtime->euclid_phase = 0U;
            if (previous_model == NOTE_FX_MODEL_EUCLID)
            {
                runtime->euclid_reconfigure_pending = 1U;
                runtime->next_sample = 0U;
            }
        }
        return;
    }

    if (previous_model != target_model)
    {
        if (previous_model == NOTE_FX_MODEL_EUCLID)
        {
            memset(runtime->euclid_source, 0, sizeof(runtime->euclid_source));
            runtime->euclid_source_count = 0U;
        }
        if (target_model == NOTE_FX_MODEL_ARP)
        {
            note_fx_arp_init(&runtime->arp,
                             0x9E3779B9U ^ ((uint32_t)track << 8) ^ slot);
        }
        if (slot_has_owned(runtime) != 0U)
        {
            runtime->closing = 1U;
            runtime->next_sample = 0U;
        }
    }
    runtime->model = target_model;
    runtime->rate = (rate < 8U) ? rate : 2U;
    runtime->style = (style <= NOTE_FX_ARP_RANDOM) ? style : NOTE_FX_ARP_ORDER;
    runtime->range = (range >= 1U && range <= 4U) ? range : 1U;
    runtime->euclid_reconfigure_pending = 0U;
}

static note_fx_result_t euclid_emit_source_off(
    note_fx_slot_runtime_t *runtime, uint8_t track, uint8_t slot,
    note_fx_euclid_source_t *source, uint64_t sample,
    note_fx_emit_fn emit, void *context)
{
    note_fx_result_t close_result = NOTE_EVENT_RESULT_ACCEPTED;
    for (uint8_t i = 0U; i < NOTE_FX_MAX_OUTPUTS; ++i)
    {
        note_fx_owned_t *const owned = &runtime->owned[i];
        if ((owned->active == 0U)
                || (owned->source_token != source->token)
                || (owned->source_generation != source->generation)
                || (owned->source_provenance != source->provenance))
        {
            continue;
        }
        const note_fx_event_t off = {
            .sample_abs = sample,
            .track = track,
            .destination_id = owned->destination,
            .note = owned->note,
            .velocity = 0U,
            .kind = NOTE_EVENT_KIND_OFF,
            .provenance = NOTE_EVENT_SOURCE_FX,
            .stage = (uint8_t)(slot + 1U),
            .flags = NOTE_EVENT_FLAG_GENERATED | NOTE_EVENT_FLAG_CLOSURE_RESERVED,
            .source_token = owned_child_source_token(owned),
            .occurrence_id = owned->token,
            .generation = owned->generation
        };
        const note_fx_result_t result = (emit != 0)
            ? emit(&off, context) : NOTE_EVENT_RESULT_ACCEPTED;
        if (closure_is_settled(result) != 0U)
            owned->active = 0U;
        else
        {
            note_fx_diag_record_emit_reject(track, slot, 0U);
            close_result = result;
        }
    }
    return close_result;
}

static note_fx_result_t euclid_stage_source(
    note_fx_slot_runtime_t *runtime, const note_fx_event_t *event,
    uint8_t slot, note_fx_emit_fn emit, void *context)
{
    const int8_t source_index = euclid_source_find(
        runtime, event->source_token, event->generation, event->provenance);
    if (event->kind == NOTE_EVENT_KIND_OFF)
    {
        if (source_index < 0)
            return NOTE_EVENT_RESULT_REJECTED_STALE;
        note_fx_euclid_source_t *const source =
            &runtime->euclid_source[(uint8_t)source_index];
        source->closing = 1U;
        source->close_sample = event->sample_abs;
        if ((runtime->next_sample == 0U)
                || (event->sample_abs < runtime->next_sample))
        {
            runtime->next_sample = event->sample_abs;
        }
        const note_fx_result_t result = euclid_emit_source_off(
            runtime, event->track, slot, source, event->sample_abs, emit, context);
        uint8_t pending_owned = 0U;
        for (uint8_t i = 0U; i < NOTE_FX_EUCLID_MAX_OWNED; ++i)
        {
            const note_fx_owned_t *const owned = &runtime->owned[i];
            if ((owned->active != 0U)
                    && (owned->source_token == source->token)
                    && (owned->source_generation == source->generation)
                    && (owned->source_provenance == source->provenance))
            {
                pending_owned = 1U;
                break;
            }
        }
        if (pending_owned == 0U)
            euclid_source_remove(runtime, (uint8_t)source_index);
        return result;
    }

    if (source_index >= 0)
    {
        note_fx_euclid_source_t *const source =
            &runtime->euclid_source[(uint8_t)source_index];
        source->note = event->note;
        source->velocity = event->velocity;
        source->destination = event->destination_id;
        source->closing = 0U;
        return NOTE_EVENT_RESULT_ACCEPTED;
    }
    if (runtime->euclid_source_count >= NOTE_FX_EUCLID_MAX_SOURCES)
    {
        note_fx_diag_record_saturation(event->track, slot,
                                        NOTE_FX_DIAG_CAUSE_SOURCE_CAPACITY);
        return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
    }

    const uint8_t was_empty = runtime->euclid_source_count == 0U;
    note_fx_euclid_source_t *const source =
        &runtime->euclid_source[runtime->euclid_source_count++];
    *source = (note_fx_euclid_source_t){
        .active = 1U,
        .note = event->note,
        .velocity = event->velocity,
        .destination = event->destination_id,
        .provenance = event->provenance,
        .token = event->source_token,
        .generation = event->generation
    };
    if (was_empty != 0U)
    {
        runtime->euclid_phase = 0U;
        runtime->next_sample = event->sample_abs;
    }
    note_fx_diag_update_usage(event->track, slot, runtime);
    return NOTE_EVENT_RESULT_ACCEPTED;
}

note_fx_result_t note_fx_engine_stage_source(const note_fx_event_t *event, uint8_t slot,
                                             note_fx_emit_fn emit, void *context)
{
    if (!note_event_is_valid(event) || event->track >= NOTE_FX_TRACK_COUNT
            || slot >= NOTE_FX_SLOT_COUNT || event->stage != slot)
        return NOTE_EVENT_RESULT_DROPPED_POLICY;

    note_fx_slot_runtime_t *const runtime = &g_slot[event->track][slot];
    if (runtime->model == NOTE_FX_MODEL_EUCLID)
    {
        return euclid_stage_source(runtime, event, slot, emit, context);
    }
    if (runtime->model != NOTE_FX_MODEL_ARP)
    {
        note_fx_event_t forwarded = *event;
        forwarded.stage = (uint8_t)(slot + 1U);
        return (emit != 0) ? emit(&forwarded, context)
                           : NOTE_EVENT_RESULT_ACCEPTED;
    }
    if (event->kind == NOTE_EVENT_KIND_OFF) {
        note_fx_result_t close_result = NOTE_EVENT_RESULT_ACCEPTED;
        uint8_t matched_owned = 0U;
        for (uint8_t i = 0U; i < NOTE_FX_MAX_OUTPUTS; ++i) {
            note_fx_owned_t *const owned = &runtime->owned[i];
            if ((owned->active == 0U)
                    || (owned->source_token != event->source_token)
                    || (owned->source_generation != event->generation)) continue;
            matched_owned = 1U;
            note_fx_event_t off = *event;
            off.note = owned->note;
            off.velocity = 0U;
            off.kind = NOTE_EVENT_KIND_OFF;
            off.provenance = NOTE_EVENT_SOURCE_FX;
            off.stage = (uint8_t)(slot + 1U);
            off.flags |= NOTE_EVENT_FLAG_GENERATED | NOTE_EVENT_FLAG_CLOSURE_RESERVED;
            off.source_token = owned_child_source_token(owned);
            off.occurrence_id = owned->token;
            off.generation = owned->generation;
            off.destination_id = owned->destination;
            const note_fx_result_t result = (emit != 0)
                ? emit(&off, context) : NOTE_EVENT_RESULT_ACCEPTED;
            if (closure_is_settled(result) != 0U)
                owned->active = 0U;
            else
            {
                note_fx_diag_record_emit_reject(event->track, slot, 0U);
                close_result = result;
            }
        }
        if (close_result != NOTE_EVENT_RESULT_ACCEPTED)
        {
            runtime->pending_source_close = 1U;
            runtime->pending_close_token = event->source_token;
            runtime->pending_close_generation = event->generation;
            runtime->next_sample = event->sample_abs;
            return close_result;
        }
        if (note_fx_arp_note_off(&runtime->arp, event->source_token,
                                 event->generation) == 0U)
        {
            return (matched_owned != 0U) ? NOTE_EVENT_RESULT_ACCEPTED
                                         : NOTE_EVENT_RESULT_REJECTED_STALE;
        }
        if (runtime->arp.count == 0U) {
            runtime->closing = 0U;
            for (uint8_t i = 0U; i < NOTE_FX_MAX_OUTPUTS; ++i)
                runtime->closing |= runtime->owned[i].active;
            runtime->next_sample = (runtime->closing != 0U)
                ? event->sample_abs : 0U;
        }
        return 1U;
    }

    const uint8_t was_empty = runtime->arp.count == 0U;
    if (note_fx_arp_note_on(&runtime->arp, event->note, event->velocity,
                            event->source_token, event->generation) == 0U) {
        note_fx_diag_record_saturation(event->track, slot,
                                        NOTE_FX_DIAG_CAUSE_MODEL_CAPACITY);
        return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
    }
    runtime->destination = event->destination_id;
    if (was_empty != 0U) runtime->next_sample = event->sample_abs;
    return 1U;
}

note_fx_result_t note_fx_engine_source(const note_fx_event_t *event,
                                       note_fx_emit_fn emit, void *context)
{
    return note_fx_engine_stage_source(event, 0U, emit, context);
}

static uint64_t rate_period(uint8_t rate, uint32_t samples_per_step_q16)
{
    return seq_division_period_samples(rate, samples_per_step_q16);
}

static note_fx_result_t euclid_emit_owned_off(
    note_fx_slot_runtime_t *runtime, uint8_t track, uint8_t slot,
    note_fx_owned_t *owned, uint64_t sample,
    note_fx_emit_fn emit, void *context)
{
    const note_fx_event_t off = {
        .sample_abs = sample,
        .track = track,
        .destination_id = owned->destination,
        .note = owned->note,
        .velocity = 0U,
        .kind = NOTE_EVENT_KIND_OFF,
        .provenance = NOTE_EVENT_SOURCE_FX,
        .stage = (uint8_t)(slot + 1U),
        .flags = NOTE_EVENT_FLAG_GENERATED | NOTE_EVENT_FLAG_CLOSURE_RESERVED,
        .source_token = owned_child_source_token(owned),
        .occurrence_id = owned->token,
        .generation = owned->generation
    };
    const note_fx_result_t result = (emit != 0)
        ? emit(&off, context) : NOTE_EVENT_RESULT_ACCEPTED;
    if (closure_is_settled(result) != 0U)
        owned->active = 0U;
    else
        note_fx_diag_record_emit_reject(track, slot, 0U);
    (void)runtime;
    return result;
}

static uint8_t euclid_owned_source_is_closing(
    const note_fx_slot_runtime_t *runtime, const note_fx_owned_t *owned)
{
    for (uint8_t i = 0U; i < runtime->euclid_source_count; ++i)
    {
        const note_fx_euclid_source_t *const source =
            &runtime->euclid_source[i];
        if ((source->active != 0U)
                && (source->token == owned->source_token)
                && (source->generation == owned->source_generation)
                && (source->provenance == owned->source_provenance))
        {
            return source->closing;
        }
    }
    return 0U;
}

static uint8_t euclid_process_closures(
    note_fx_slot_runtime_t *runtime, uint8_t track, uint8_t slot,
    uint64_t start, uint64_t end, note_fx_emit_fn emit, void *context)
{
    uint8_t blocked = 0U;
    if (runtime->euclid_reconfigure_pending != 0U)
    {
        for (uint8_t i = 0U; i < NOTE_FX_MAX_OUTPUTS; ++i)
        {
            note_fx_owned_t *const owned = &runtime->owned[i];
            if (owned->active != 0U)
                (void)euclid_emit_owned_off(runtime, track, slot, owned,
                                            start, emit, context);
        }
        if (slot_has_owned(runtime) != 0U)
            blocked = 1U;
        else
        {
            runtime->euclid_reconfigure_pending = 0U;
            runtime->next_sample = start;
        }
    }

    for (uint8_t i = 0U; i < runtime->euclid_source_count; )
    {
        note_fx_euclid_source_t *const source = &runtime->euclid_source[i];
        if ((source->active == 0U) || (source->closing == 0U)
                || (source->close_sample >= end))
        {
            ++i;
            continue;
        }
        const uint64_t close_sample = (source->close_sample < start)
            ? start : source->close_sample;
        (void)euclid_emit_source_off(runtime, track, slot, source,
                                     close_sample, emit, context);
        uint8_t pending_owned = 0U;
        for (uint8_t owned_index = 0U;
             owned_index < NOTE_FX_MAX_OUTPUTS; ++owned_index)
        {
            const note_fx_owned_t *const owned = &runtime->owned[owned_index];
            if ((owned->active != 0U)
                    && (owned->source_token == source->token)
                    && (owned->source_generation == source->generation)
                    && (owned->source_provenance == source->provenance))
            {
                pending_owned = 1U;
                break;
            }
        }
        if (pending_owned != 0U)
        {
            blocked = 1U;
            ++i;
        }
        else
        {
            euclid_source_remove(runtime, i);
        }
    }

    for (uint8_t i = 0U; i < NOTE_FX_MAX_OUTPUTS; ++i)
    {
        note_fx_owned_t *const owned = &runtime->owned[i];
        if ((owned->active == 0U)
                || (owned->off_sample >= end)
                || (euclid_owned_source_is_closing(runtime, owned) != 0U))
        {
            continue;
        }
        const uint64_t off_sample = (owned->off_sample < start)
            ? start : owned->off_sample;
        (void)euclid_emit_owned_off(runtime, track, slot, owned,
                                    off_sample, emit, context);
        if (owned->active != 0U)
            blocked = 1U;
    }
    return blocked;
}

static void euclid_generate_at_deadline(
    note_fx_slot_runtime_t *runtime, uint8_t track, uint8_t slot,
    uint64_t start, uint64_t end, uint32_t samples_per_step_q16,
    note_fx_emit_fn emit, void *context)
{
    if ((runtime->model != NOTE_FX_MODEL_EUCLID)
            || (runtime->euclid_source_count == 0U)
            || (runtime->euclid_reconfigure_pending != 0U)
            || (runtime->closing != 0U)
            || (runtime->next_sample >= end))
    {
        return;
    }
    if (runtime->next_sample < start)
        runtime->next_sample = start;

    const uint64_t sample = runtime->next_sample;
    const uint64_t period = rate_period(runtime->euclid_division,
                                        samples_per_step_q16);
    if (((runtime->euclid_mask >> runtime->euclid_phase) & 1U) != 0U)
    {
        for (uint8_t source_index = 0U;
             source_index < runtime->euclid_source_count; ++source_index)
        {
            const note_fx_euclid_source_t *const source =
                &runtime->euclid_source[source_index];
            if ((source->active == 0U) || (source->closing != 0U))
                continue;
            note_fx_owned_t *owned = 0;
            for (uint8_t owned_index = 0U;
                 owned_index < NOTE_FX_MAX_OUTPUTS; ++owned_index)
            {
                if (runtime->owned[owned_index].active == 0U)
                {
                    owned = &runtime->owned[owned_index];
                    break;
                }
            }
            if (owned == 0)
            {
                note_fx_diag_record_saturation(track, slot,
                                                NOTE_FX_DIAG_CAUSE_OWNED_CAPACITY);
                continue;
            }

            const uint32_t token = next_fx_token();
            *owned = (note_fx_owned_t){
                .active = 1U,
                .note = source->note,
                .destination = source->destination,
                .source_provenance = source->provenance,
                .source_token = source->token,
                .source_generation = source->generation,
                .token = token,
                .generation = runtime->generation,
                .off_sample = sample + period
            };
            note_fx_diag_update_usage(track, slot, runtime);
            const note_fx_event_t on = {
                .sample_abs = sample,
                .track = track,
                .destination_id = source->destination,
                .note = source->note,
                .velocity = source->velocity,
                .kind = NOTE_EVENT_KIND_ON,
                .provenance = NOTE_EVENT_SOURCE_FX,
                .stage = (uint8_t)(slot + 1U),
                .flags = NOTE_EVENT_FLAG_GENERATED,
                .source_token = token,
                .occurrence_id = token,
                .generation = runtime->generation
            };
            const note_fx_result_t result = (emit != 0)
                ? emit(&on, context) : NOTE_EVENT_RESULT_ACCEPTED;
            if (result != NOTE_EVENT_RESULT_ACCEPTED)
            {
                note_fx_diag_record_emit_reject(track, slot, 1U);
                owned->active = 0U;
            }
        }
    }

    if ((uint8_t)(runtime->euclid_phase + 1U) >= runtime->euclid_length)
        runtime->euclid_phase = 0U;
    else
        ++runtime->euclid_phase;
    runtime->next_sample = sample + period;
}

void note_fx_engine_process(uint64_t start, uint16_t frames, uint32_t step_q16,
                            note_fx_emit_fn emit, void *context)
{
    const uint64_t end = start + frames;
    /* First pass: every due owned closure precedes every newly generated On. */
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track) {
        for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot) {
            note_fx_slot_runtime_t *const runtime = &g_slot[track][slot];
            if ((runtime->closing != 0U) && (runtime->next_sample < end)) {
                if (runtime->next_sample < start) runtime->next_sample = start;
                release_slot(track, slot, runtime->next_sample, emit, context);
                continue;
            }
            if (runtime->model == NOTE_FX_MODEL_EUCLID)
            {
                (void)euclid_process_closures(runtime, track, slot, start, end,
                                               emit, context);
                continue;
            }
            if (runtime->model != NOTE_FX_MODEL_ARP ||
                runtime->arp.count == 0U || runtime->next_sample >= end) continue;
            if (runtime->next_sample < start) runtime->next_sample = start;
            for (uint8_t i = 0U; i < NOTE_FX_MAX_OUTPUTS; ++i) {
                note_fx_owned_t *const owned = &runtime->owned[i];
                if (owned->active == 0U) continue;
                const note_fx_event_t off = {
                    .sample_abs = runtime->next_sample,
                    .track = track,
                    .destination_id = owned->destination,
                    .note = owned->note,
                    .velocity = 0U,
                    .kind = NOTE_EVENT_KIND_OFF,
                    .provenance = NOTE_EVENT_SOURCE_FX,
                    .stage = (uint8_t)(slot + 1U),
                    .flags = NOTE_EVENT_FLAG_GENERATED | NOTE_EVENT_FLAG_CLOSURE_RESERVED,
                    .source_token = owned_child_source_token(owned),
                    .occurrence_id = owned->token,
                    .generation = owned->generation
                };
                const note_fx_result_t result = (emit != 0)
                    ? emit(&off, context) : NOTE_EVENT_RESULT_ACCEPTED;
                if (closure_is_settled(result) != 0U)
                    owned->active = 0U;
                else
                    note_fx_diag_record_emit_reject(track, slot, 0U);
            }
            if (runtime->pending_source_close != 0U) {
                uint8_t pending_owned = 0U;
                for (uint8_t i = 0U; i < NOTE_FX_MAX_OUTPUTS; ++i) {
                    const note_fx_owned_t *const owned = &runtime->owned[i];
                    if ((owned->active != 0U)
                            && (owned->source_token == runtime->pending_close_token)
                            && (owned->source_generation
                                == runtime->pending_close_generation)) {
                        pending_owned = 1U;
                        break;
                    }
                }
                if (pending_owned == 0U) {
                    (void)note_fx_arp_note_off(&runtime->arp,
                                              runtime->pending_close_token,
                                              runtime->pending_close_generation);
                    runtime->pending_source_close = 0U;
                    if (runtime->arp.count == 0U)
                        runtime->next_sample = 0U;
                }
            }
        }
    }

    /* Second pass: generate only after the global closure pass completed. */
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track) {
        for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot) {
            note_fx_slot_runtime_t *const runtime = &g_slot[track][slot];
            if (runtime->model == NOTE_FX_MODEL_EUCLID)
            {
                if (slot_has_owned(runtime) == 0U)
                {
                    euclid_generate_at_deadline(runtime, track, slot, start, end,
                                                step_q16, emit, context);
                }
                continue;
            }
            if ((runtime->closing != 0U)
                    || (runtime->model != NOTE_FX_MODEL_ARP)
                    || (runtime->arp.count == 0U)
                    || (runtime->next_sample >= end)) continue;
            uint8_t release_pending = 0U;
            for (uint8_t i = 0U; i < NOTE_FX_MAX_OUTPUTS; ++i)
                release_pending |= runtime->owned[i].active;
            if (release_pending != 0U) {
                runtime->next_sample += rate_period(runtime->rate, step_q16);
                continue;
            }
            uint8_t note, velocity;
            if (note_fx_arp_next(&runtime->arp,
                (note_fx_arp_style_t)runtime->style, runtime->range,
                &note, &velocity) == 0U) {
                note_fx_diag_record_saturation(track, slot,
                                                NOTE_FX_DIAG_CAUSE_MODEL_CAPACITY);
                release_slot(track, slot, runtime->next_sample, emit, context);
                continue;
            }
            note_fx_owned_t *const owned = &runtime->owned[0];
            owned->active = 1U; owned->note = note;
            owned->source_token = runtime->arp.last_source_token;
            owned->source_generation = runtime->arp.last_source_generation;
            owned->source_provenance = NOTE_EVENT_SOURCE_FX;
            owned->destination = runtime->destination;
            owned->token = next_fx_token();
            owned->generation = runtime->generation;
            const note_fx_event_t on = {
                .sample_abs = runtime->next_sample,
                .track = track,
                .destination_id = owned->destination,
                .note = note,
                .velocity = velocity,
                .kind = NOTE_EVENT_KIND_ON,
                .provenance = NOTE_EVENT_SOURCE_FX,
                .stage = (uint8_t)(slot + 1U),
                .flags = NOTE_EVENT_FLAG_GENERATED,
                .source_token = owned->token,
                .occurrence_id = owned->token,
                .generation = owned->generation
            };
            const note_fx_result_t result = (emit != 0)
                ? emit(&on, context) : NOTE_EVENT_RESULT_ACCEPTED;
            if (result != NOTE_EVENT_RESULT_ACCEPTED)
            {
                note_fx_diag_record_emit_reject(track, slot, 1U);
                owned->active = 0U;
            }
            runtime->next_sample += rate_period(runtime->rate, step_q16);
        }
    }
}

void note_fx_engine_cleanup(uint8_t track, uint64_t sample,
                            note_fx_emit_fn emit, void *context)
{
    if (track >= NOTE_FX_TRACK_COUNT) return;
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
        release_slot(track, slot, sample, emit, context);
}

note_fx_diag_t note_fx_engine_diag(uint8_t track)
{
    const note_fx_diag_t zero = {0};
    return track < NOTE_FX_TRACK_COUNT ? g_diag[track] : zero;
}

note_fx_slot_diag_t note_fx_engine_slot_diag(uint8_t track, uint8_t slot)
{
    const note_fx_slot_diag_t zero = {0};
    if ((track >= NOTE_FX_TRACK_COUNT) || (slot >= NOTE_FX_SLOT_COUNT))
        return zero;
    return g_diag[track].slot[slot];
}

uint64_t note_fx_engine_next_deadline(void)
{
    uint64_t next = UINT64_MAX;
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
        for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot) {
            const note_fx_slot_runtime_t *const runtime = &g_slot[track][slot];
            if ((runtime->closing != 0U)
                    || (runtime->pending_source_close != 0U)
                    || (runtime->euclid_reconfigure_pending != 0U))
            {
                if (runtime->next_sample < next)
                    next = runtime->next_sample;
            }
            if ((runtime->model == NOTE_FX_MODEL_ARP)
                    && (runtime->arp.count != 0U)
                    && (runtime->next_sample < next))
            {
                next = runtime->next_sample;
            }
            if (runtime->model == NOTE_FX_MODEL_EUCLID)
            {
                if ((runtime->euclid_source_count != 0U)
                        && (runtime->next_sample < next))
                    next = runtime->next_sample;
                for (uint8_t i = 0U; i < runtime->euclid_source_count; ++i)
                {
                    const note_fx_euclid_source_t *const source =
                        &runtime->euclid_source[i];
                    if ((source->active != 0U) && (source->closing != 0U)
                            && (source->close_sample < next))
                        next = source->close_sample;
                }
                for (uint8_t i = 0U; i < NOTE_FX_MAX_OUTPUTS; ++i)
                {
                    const note_fx_owned_t *const owned = &runtime->owned[i];
                    if ((owned->active != 0U) && (owned->off_sample < next))
                        next = owned->off_sample;
                }
            }
        }
    return next;
}
