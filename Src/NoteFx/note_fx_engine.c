#include "NoteFx/note_fx_engine.h"

#include <string.h>

#include "NoteFx/note_fx_euclid.h"
#include "Seq/seq_division_catalog.h"

typedef struct {
    uint8_t active, note, destination, source_provenance;
    uint32_t source_token, source_generation;
    uint32_t causal_source_token;
    uint32_t token, generation;
    uint64_t off_sample;
} note_fx_owned_t;

typedef struct {
    uint64_t close_sample;
    uint32_t token, generation, causal_source_token;
    uint8_t note, velocity, destination, flags;
} note_fx_euclid_source_t;

#define NOTE_FX_EUCLID_SOURCE_ACTIVE  (1U << 0)
#define NOTE_FX_EUCLID_SOURCE_CLOSING (1U << 1)
#define NOTE_FX_EUCLID_SOURCE_PROVENANCE_SHIFT 2U
#define NOTE_FX_EUCLID_SOURCE_PROVENANCE_MASK  (3U << 2)

static uint8_t euclid_source_is_active(
    const note_fx_euclid_source_t *source)
{
    return (uint8_t)((source->flags & NOTE_FX_EUCLID_SOURCE_ACTIVE) != 0U);
}

static uint8_t euclid_source_is_closing(
    const note_fx_euclid_source_t *source)
{
    return (uint8_t)((source->flags & NOTE_FX_EUCLID_SOURCE_CLOSING) != 0U);
}

static uint8_t euclid_source_provenance(
    const note_fx_euclid_source_t *source)
{
    return (uint8_t)((source->flags
        & NOTE_FX_EUCLID_SOURCE_PROVENANCE_MASK)
        >> NOTE_FX_EUCLID_SOURCE_PROVENANCE_SHIFT);
}

typedef struct {
    note_fx_owned_t owned[NOTE_FX_EUCLID_MAX_OWNED];
    uint64_t next_sample;
    uint32_t generation;
    uint8_t model;
} note_fx_slot_common_t;

typedef struct {
    note_fx_arp_t arp;
    uint8_t rate, style, range, destination;
} note_fx_arp_state_t;

typedef struct {
    note_fx_euclid_source_t source[NOTE_FX_EUCLID_MAX_SOURCES];
    uint64_t mask;
    uint8_t length, pulse, division, phase;
    uint8_t source_count;
} note_fx_euclid_state_t;

typedef union {
    note_fx_arp_state_t arp;
    note_fx_euclid_state_t euclid;
} note_fx_state_union_t;

typedef struct {
    note_fx_slot_common_t common;
    note_fx_state_union_t fx;
} note_fx_slot_runtime_t;

_Static_assert(sizeof(note_fx_slot_runtime_t) == 928U,
               "NoteFx slot layout changed unexpectedly");

static note_fx_slot_runtime_t g_slot[NOTE_FX_TRACK_COUNT][NOTE_FX_SLOT_COUNT];
static uint32_t g_token;
static uint64_t g_work_slot_mask;

static uint8_t slot_has_owned(const note_fx_slot_runtime_t *runtime);

static uint64_t slot_work_bit(uint8_t track, uint8_t slot)
{
    return UINT64_C(1) << ((uint32_t)track * NOTE_FX_SLOT_COUNT + slot);
}

static uint8_t slot_has_work(const note_fx_slot_runtime_t *runtime)
{
    if (slot_has_owned(runtime) != 0U)
        return 1U;
    if (runtime->common.model == NOTE_FX_MODEL_ARP)
        return runtime->fx.arp.arp.count != 0U;
    if (runtime->common.model == NOTE_FX_MODEL_EUCLID)
        return runtime->fx.euclid.source_count != 0U;
    return 0U;
}

static void refresh_slot_work(uint8_t track, uint8_t slot)
{
    const uint64_t bit = slot_work_bit(track, slot);
    if (slot_has_work(&g_slot[track][slot]) != 0U)
        g_work_slot_mask |= bit;
    else
        g_work_slot_mask &= ~bit;
}

static uint32_t next_fx_token(uint32_t source_token)
{
    g_token = (g_token + 1U) & NOTE_EVENT_OCCURRENCE_COUNTER_MASK;
    if (g_token == 0U)
        g_token = 1U;
    return (source_token & (uint32_t)~NOTE_EVENT_OCCURRENCE_COUNTER_MASK)
         | g_token;
}

static uint8_t slot_has_owned(const note_fx_slot_runtime_t *runtime)
{
    for (uint8_t i = 0U; i < NOTE_FX_EUCLID_MAX_OWNED; ++i)
    {
        if (runtime->common.owned[i].active != 0U)
            return 1U;
    }
    return 0U;
}

static int8_t euclid_source_find(const note_fx_slot_runtime_t *runtime,
                                 uint32_t token, uint32_t generation,
                                 uint8_t provenance)
{
    for (uint8_t i = 0U; i < runtime->fx.euclid.source_count; ++i)
    {
        const note_fx_euclid_source_t *const source =
            &runtime->fx.euclid.source[i];
        if ((euclid_source_is_active(source) != 0U)
                && (source->token == token)
                && (source->generation == generation)
                && (euclid_source_provenance(source) == provenance))
        {
            return (int8_t)i;
        }
    }
    return -1;
}

static void euclid_source_remove(note_fx_slot_runtime_t *runtime,
                                 uint8_t index)
{
    if (index >= runtime->fx.euclid.source_count)
        return;
    for (uint8_t i = (uint8_t)(index + 1U);
         i < runtime->fx.euclid.source_count; ++i)
    {
        runtime->fx.euclid.source[i - 1U] = runtime->fx.euclid.source[i];
    }
    --runtime->fx.euclid.source_count;
    if (runtime->fx.euclid.source_count == 0U)
    {
        runtime->fx.euclid.phase = 0U;
        if (slot_has_owned(runtime) == 0U)
            runtime->common.next_sample = 0U;
    }
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
        for (uint8_t i = 0U; i < NOTE_FX_MAX_OUTPUTS; ++i)
        {
            const note_fx_owned_t *const owned = &runtime->common.owned[i];
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
    return (result == NOTE_EVENT_RESULT_ACCEPTED) ? 1U : 0U;
}

static void note_fx_slot_reset_fx_state(note_fx_slot_runtime_t *runtime,
                                        uint8_t track, uint8_t slot,
                                        uint8_t model)
{
    memset(&runtime->fx, 0, sizeof(runtime->fx));
    if (model == NOTE_FX_MODEL_ARP)
    {
        note_fx_arp_init(&runtime->fx.arp.arp,
                         0x9E3779B9U ^ ((uint32_t)track << 8) ^ slot);
    }
    else if (model == NOTE_FX_MODEL_EUCLID)
    {
        runtime->fx.euclid.length = NOTE_FX_EUCLID_LENGTH_DEFAULT;
        runtime->fx.euclid.pulse = NOTE_FX_EUCLID_PULSE_DEFAULT;
        runtime->fx.euclid.division = SEQ_DIVISION_ARP_DEFAULT_INDEX;
        runtime->fx.euclid.mask = euclid_build_mask(
            runtime->fx.euclid.length, runtime->fx.euclid.pulse);
    }
}

/* Reset only source ownership and timing.  The rate/style/range (or Euclid
 * tuple) are the current projection of note_fx_state and must survive a
 * transport cleanup. */
static void note_fx_slot_reset_temporal_state(note_fx_slot_runtime_t *runtime,
                                               uint8_t track, uint8_t slot)
{
    if (runtime->common.model == NOTE_FX_MODEL_ARP)
    {
        note_fx_arp_init(&runtime->fx.arp.arp,
                         0x9E3779B9U ^ ((uint32_t)track << 8) ^ slot);
        runtime->fx.arp.destination = 0U;
    }
    else if (runtime->common.model == NOTE_FX_MODEL_EUCLID)
    {
        memset(runtime->fx.euclid.source, 0,
               sizeof(runtime->fx.euclid.source));
        runtime->fx.euclid.phase = 0U;
        runtime->fx.euclid.source_count = 0U;
    }
}

static note_fx_result_t close_owned(uint8_t track, uint8_t slot,
                                    uint64_t sample,
                                    note_fx_emit_fn emit, void *context)
{
    note_fx_slot_runtime_t *const runtime = &g_slot[track][slot];
    ++runtime->common.generation;
    if (runtime->common.generation == 0U)
        runtime->common.generation = 1U;
    for (uint8_t i = 0U; i < NOTE_FX_MAX_OUTPUTS; ++i) {
        note_fx_owned_t *const owned = &runtime->common.owned[i];
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
            .flags = NOTE_EVENT_FLAG_GENERATED,
            .source_token = owned->causal_source_token,
            .occurrence_id = owned->token,
            .generation = owned->generation
        };
        const note_fx_result_t result = (emit != 0)
            ? emit(&event, context) : NOTE_EVENT_RESULT_ACCEPTED;
        if (closure_is_settled(result) == 0U)
            return result;
        owned->active = 0U;
    }
    return NOTE_EVENT_RESULT_ACCEPTED;
}

static note_fx_result_t release_slot(uint8_t track, uint8_t slot,
                                     uint64_t sample,
                                     note_fx_emit_fn emit, void *context)
{
    note_fx_slot_runtime_t *const runtime = &g_slot[track][slot];
    const note_fx_result_t result = close_owned(
        track, slot, sample, emit, context);
    if (result != NOTE_EVENT_RESULT_ACCEPTED)
        return result;
    note_fx_slot_reset_temporal_state(runtime, track, slot);
    runtime->common.next_sample = 0U;
    refresh_slot_work(track, slot);
    return NOTE_EVENT_RESULT_ACCEPTED;
}

void note_fx_engine_init(void)
{
    memset(g_slot, 0, sizeof(g_slot));
    g_token = 0U;
    g_work_slot_mask = 0U;
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
        for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot) {
            g_slot[track][slot].common.model = NOTE_FX_MODEL_OFF;
            g_slot[track][slot].common.generation = 1U;
            note_fx_slot_reset_fx_state(&g_slot[track][slot], track, slot,
                                        NOTE_FX_MODEL_OFF);
        }
}

void note_fx_engine_forget_output(uint8_t track, uint32_t output_id)
{
    if ((track >= NOTE_FX_TRACK_COUNT) || (output_id == 0U))
        return;
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        note_fx_slot_runtime_t *const runtime = &g_slot[track][slot];
        for (uint8_t i = 0U; i < NOTE_FX_EUCLID_MAX_OWNED; ++i)
            if ((runtime->common.owned[i].active != 0U)
                    && (runtime->common.owned[i].token == output_id))
                runtime->common.owned[i].active = 0U;
        refresh_slot_work(track, slot);
    }
}

void note_fx_engine_forget_causal_source(uint8_t track,
                                         uint32_t causal_source_id)
{
    if ((track >= NOTE_FX_TRACK_COUNT) || (causal_source_id == 0U))
        return;
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        note_fx_slot_runtime_t *const runtime = &g_slot[track][slot];
        for (uint8_t i = 0U; i < NOTE_FX_MAX_OUTPUTS; ++i)
            if ((runtime->common.owned[i].active != 0U)
                    && (runtime->common.owned[i].causal_source_token
                        == causal_source_id))
                runtime->common.owned[i].active = 0U;
        if (runtime->common.model == NOTE_FX_MODEL_ARP)
        {
            uint8_t index = 0U;
            while (index < runtime->fx.arp.arp.count)
            {
                if (runtime->fx.arp.arp.causal_source_token[index]
                        != causal_source_id)
                {
                    ++index;
                    continue;
                }
                const uint32_t token =
                    runtime->fx.arp.arp.source_token[index];
                const uint32_t generation =
                    runtime->fx.arp.arp.source_generation[index];
                (void)note_fx_arp_note_off(&runtime->fx.arp.arp,
                                           token, generation);
            }
        }
        else if (runtime->common.model == NOTE_FX_MODEL_EUCLID)
        {
            uint8_t index = 0U;
            while (index < runtime->fx.euclid.source_count)
            {
                if (runtime->fx.euclid.source[index].causal_source_token
                        == causal_source_id)
                    euclid_source_remove(runtime, index);
                else
                    ++index;
            }
        }
        if ((slot_has_owned(runtime) == 0U)
                && ((runtime->common.model != NOTE_FX_MODEL_ARP)
                    || (runtime->fx.arp.arp.count == 0U))
                && ((runtime->common.model != NOTE_FX_MODEL_EUCLID)
                    || (runtime->fx.euclid.source_count == 0U)))
            runtime->common.next_sample = 0U;
        refresh_slot_work(track, slot);
    }
}

note_fx_result_t note_fx_engine_configure(
    uint8_t track, uint8_t slot, uint8_t model, uint8_t rate,
    uint8_t style, uint8_t range, uint64_t sample,
    note_fx_emit_fn emit, void *context)
{
    if (track >= NOTE_FX_TRACK_COUNT || slot >= NOTE_FX_SLOT_COUNT)
        return NOTE_EVENT_RESULT_DROPPED_POLICY;
    note_fx_slot_runtime_t *const runtime = &g_slot[track][slot];
    const uint8_t previous_model = runtime->common.model;
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
            ? 1U
            : (uint8_t)((runtime->fx.euclid.length != length)
                || (runtime->fx.euclid.pulse != pulse)
                || (runtime->fx.euclid.division != division));

        if (previous_model != NOTE_FX_MODEL_EUCLID)
        {
            const note_fx_result_t result = release_slot(
                track, slot, sample, emit, context);
            if (result != NOTE_EVENT_RESULT_ACCEPTED)
                return result;
            note_fx_slot_reset_fx_state(runtime, track, slot,
                                        NOTE_FX_MODEL_EUCLID);
        }
        else if (changed != 0U)
        {
            const note_fx_result_t result = close_owned(
                track, slot, sample, emit, context);
            if (result != NOTE_EVENT_RESULT_ACCEPTED)
                return result;
        }
        runtime->common.model = target_model;
        runtime->fx.euclid.length = length;
        runtime->fx.euclid.pulse = pulse;
        runtime->fx.euclid.division = division;
        if (changed != 0U)
        {
            runtime->fx.euclid.mask = euclid_build_mask(length, pulse);
            runtime->fx.euclid.phase = 0U;
            runtime->common.next_sample = sample;
        }
        refresh_slot_work(track, slot);
        return NOTE_EVENT_RESULT_ACCEPTED;
    }

    if (previous_model != target_model)
    {
        const note_fx_result_t result = release_slot(
            track, slot, sample, emit, context);
        if (result != NOTE_EVENT_RESULT_ACCEPTED)
            return result;
        note_fx_slot_reset_fx_state(runtime, track, slot, target_model);
    }
    runtime->common.model = target_model;
    if (target_model == NOTE_FX_MODEL_ARP)
    {
        runtime->fx.arp.rate = (rate < 8U) ? rate : 2U;
        runtime->fx.arp.style = (style <= NOTE_FX_ARP_RANDOM) ? style : NOTE_FX_ARP_ORDER;
        runtime->fx.arp.range = (range >= 1U && range <= 4U) ? range : 1U;
    }
    refresh_slot_work(track, slot);
    return NOTE_EVENT_RESULT_ACCEPTED;
}

static note_fx_result_t euclid_emit_source_off(
    note_fx_slot_runtime_t *runtime, uint8_t track, uint8_t slot,
    note_fx_euclid_source_t *source, uint64_t sample,
    note_fx_emit_fn emit, void *context)
{
    note_fx_result_t close_result = NOTE_EVENT_RESULT_ACCEPTED;
    for (uint8_t i = 0U; i < NOTE_FX_MAX_OUTPUTS; ++i)
    {
        note_fx_owned_t *const owned = &runtime->common.owned[i];
        if ((owned->active == 0U)
                || (owned->source_token != source->token)
                || (owned->source_generation != source->generation)
                || (owned->source_provenance
                    != euclid_source_provenance(source)))
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
            .flags = NOTE_EVENT_FLAG_GENERATED,
            .source_token = owned->causal_source_token,
            .occurrence_id = owned->token,
            .generation = owned->generation
        };
        const note_fx_result_t result = (emit != 0)
            ? emit(&off, context) : NOTE_EVENT_RESULT_ACCEPTED;
        if (closure_is_settled(result) != 0U)
            owned->active = 0U;
        else
            close_result = result;
    }
    return close_result;
}

static note_fx_result_t euclid_stage_source(
    note_fx_slot_runtime_t *runtime, const note_fx_event_t *event,
    uint8_t slot, note_fx_emit_fn emit, void *context)
{
    const int8_t source_index = euclid_source_find(
        runtime, event->occurrence_id, event->generation, event->provenance);
    if (event->kind == NOTE_EVENT_KIND_OFF)
    {
        if (source_index < 0)
            return NOTE_EVENT_RESULT_REJECTED_STALE;
        note_fx_euclid_source_t *const source =
            &runtime->fx.euclid.source[(uint8_t)source_index];
        source->flags |= NOTE_FX_EUCLID_SOURCE_CLOSING;
        source->close_sample = event->sample_abs;
        if ((runtime->common.next_sample == 0U)
                || (event->sample_abs < runtime->common.next_sample))
        {
            runtime->common.next_sample = event->sample_abs;
        }
        const note_fx_result_t result = euclid_emit_source_off(
            runtime, event->track, slot, source, event->sample_abs, emit, context);
        uint8_t pending_owned = 0U;
        for (uint8_t i = 0U; i < NOTE_FX_EUCLID_MAX_OWNED; ++i)
        {
            const note_fx_owned_t *const owned = &runtime->common.owned[i];
            if ((owned->active != 0U)
                    && (owned->source_token == source->token)
                    && (owned->source_generation == source->generation)
                    && (owned->source_provenance
                        == euclid_source_provenance(source)))
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
            &runtime->fx.euclid.source[(uint8_t)source_index];
        source->note = event->note;
        source->velocity = event->velocity;
        source->destination = event->destination_id;
        source->causal_source_token = event->source_token;
        source->flags &= (uint8_t)~NOTE_FX_EUCLID_SOURCE_CLOSING;
        return NOTE_EVENT_RESULT_ACCEPTED;
    }
    if (runtime->fx.euclid.source_count >= NOTE_FX_EUCLID_MAX_SOURCES)
    {
        return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
    }

    const uint8_t was_empty = runtime->fx.euclid.source_count == 0U;
    note_fx_euclid_source_t *const source =
        &runtime->fx.euclid.source[runtime->fx.euclid.source_count++];
    *source = (note_fx_euclid_source_t){
        .note = event->note,
        .velocity = event->velocity,
        .destination = event->destination_id,
        .flags = (uint8_t)(NOTE_FX_EUCLID_SOURCE_ACTIVE
            | (uint8_t)(event->provenance
                << NOTE_FX_EUCLID_SOURCE_PROVENANCE_SHIFT)),
        .token = event->occurrence_id,
        .generation = event->generation,
        .causal_source_token = event->source_token
    };
    if (was_empty != 0U)
    {
        runtime->fx.euclid.phase = 0U;
        runtime->common.next_sample = event->sample_abs;
    }
    return NOTE_EVENT_RESULT_ACCEPTED;
}

static note_fx_result_t note_fx_engine_stage_source_impl(
    const note_fx_event_t *event, uint8_t slot,
    note_fx_emit_fn emit, void *context)
{
    if (!note_event_is_valid(event) || event->track >= NOTE_FX_TRACK_COUNT
            || slot >= NOTE_FX_SLOT_COUNT || event->stage != slot)
        return NOTE_EVENT_RESULT_DROPPED_POLICY;

    note_fx_slot_runtime_t *const runtime = &g_slot[event->track][slot];
    if (runtime->common.model == NOTE_FX_MODEL_EUCLID)
    {
        return euclid_stage_source(runtime, event, slot, emit, context);
    }
    if (runtime->common.model != NOTE_FX_MODEL_ARP)
    {
        note_fx_event_t forwarded = *event;
        forwarded.stage = (uint8_t)(slot + 1U);
        return (emit != 0) ? emit(&forwarded, context)
                           : NOTE_EVENT_RESULT_ACCEPTED;
    }
    if (event->kind == NOTE_EVENT_KIND_OFF) {
        uint8_t matched_owned = 0U;
        for (uint8_t i = 0U; i < NOTE_FX_MAX_OUTPUTS; ++i) {
            note_fx_owned_t *const owned = &runtime->common.owned[i];
            if ((owned->active == 0U)
                    || (owned->source_token != event->occurrence_id)
                    || (owned->source_generation != event->generation)) continue;
            matched_owned = 1U;
            note_fx_event_t off = *event;
            off.note = owned->note;
            off.velocity = 0U;
            off.kind = NOTE_EVENT_KIND_OFF;
            off.provenance = NOTE_EVENT_SOURCE_FX;
            off.stage = (uint8_t)(slot + 1U);
            off.flags |= NOTE_EVENT_FLAG_GENERATED;
            off.source_token = owned->causal_source_token;
            off.occurrence_id = owned->token;
            off.generation = owned->generation;
            off.destination_id = owned->destination;
            const note_fx_result_t result = (emit != 0)
                ? emit(&off, context) : NOTE_EVENT_RESULT_ACCEPTED;
            if (closure_is_settled(result) == 0U)
                return result;
            owned->active = 0U;
        }
        const uint8_t source_removed = note_fx_arp_note_off(
            &runtime->fx.arp.arp, event->occurrence_id,
            event->generation) == 0U
            ? 0U : 1U;
        if (source_removed == 0U)
            return (matched_owned != 0U) ? NOTE_EVENT_RESULT_ACCEPTED
                                         : NOTE_EVENT_RESULT_REJECTED_STALE;
        if (runtime->fx.arp.arp.count == 0U)
            runtime->common.next_sample = 0U;
        return 1U;
    }

    const uint8_t was_empty = runtime->fx.arp.arp.count == 0U;
    if (note_fx_arp_note_on(&runtime->fx.arp.arp, event->note, event->velocity,
                            event->occurrence_id, event->generation,
                            event->source_token) == 0U) {
        return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
    }
    runtime->fx.arp.destination = event->destination_id;
    if (was_empty != 0U) runtime->common.next_sample = event->sample_abs;
    return 1U;
}

note_fx_result_t note_fx_engine_stage_source(const note_fx_event_t *event,
                                             uint8_t slot,
                                             note_fx_emit_fn emit,
                                             void *context)
{
    const note_fx_result_t result = note_fx_engine_stage_source_impl(
        event, slot, emit, context);
    if ((event != 0) && (event->track < NOTE_FX_TRACK_COUNT)
            && (slot < NOTE_FX_SLOT_COUNT))
    {
        refresh_slot_work(event->track, slot);
    }
    return result;
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
        .flags = NOTE_EVENT_FLAG_GENERATED,
        .source_token = owned->causal_source_token,
        .occurrence_id = owned->token,
        .generation = owned->generation
    };
    const note_fx_result_t result = (emit != 0)
        ? emit(&off, context) : NOTE_EVENT_RESULT_ACCEPTED;
    if (closure_is_settled(result) != 0U)
        owned->active = 0U;
    (void)runtime;
    return result;
}

static uint8_t euclid_owned_source_is_closing(
    const note_fx_slot_runtime_t *runtime, const note_fx_owned_t *owned)
{
    for (uint8_t i = 0U; i < runtime->fx.euclid.source_count; ++i)
    {
        const note_fx_euclid_source_t *const source =
            &runtime->fx.euclid.source[i];
        if ((euclid_source_is_active(source) != 0U)
                && (source->token == owned->source_token)
                && (source->generation == owned->source_generation)
                && (euclid_source_provenance(source)
                    == owned->source_provenance))
        {
            return euclid_source_is_closing(source);
        }
    }
    return 0U;
}

static note_fx_result_t euclid_process_closures(
    note_fx_slot_runtime_t *runtime, uint8_t track, uint8_t slot,
    uint64_t start, uint64_t end, note_fx_emit_fn emit, void *context)
{
    for (uint8_t i = 0U; i < runtime->fx.euclid.source_count; )
    {
        note_fx_euclid_source_t *const source = &runtime->fx.euclid.source[i];
        if ((euclid_source_is_active(source) == 0U)
                || (euclid_source_is_closing(source) == 0U)
                || (source->close_sample >= end))
        {
            ++i;
            continue;
        }
        const uint64_t close_sample = (source->close_sample < start)
            ? start : source->close_sample;
        const note_fx_result_t result = euclid_emit_source_off(
            runtime, track, slot, source, close_sample, emit, context);
        if (closure_is_settled(result) == 0U)
            return result;
        uint8_t pending_owned = 0U;
        for (uint8_t owned_index = 0U;
             owned_index < NOTE_FX_MAX_OUTPUTS; ++owned_index)
        {
            const note_fx_owned_t *const owned =
                &runtime->common.owned[owned_index];
            if ((owned->active != 0U)
                    && (owned->source_token == source->token)
                    && (owned->source_generation == source->generation)
                    && (owned->source_provenance
                        == euclid_source_provenance(source)))
            {
                pending_owned = 1U;
                break;
            }
        }
        if (pending_owned != 0U)
        {
            ++i;
        }
        else
        {
            euclid_source_remove(runtime, i);
        }
    }

    for (uint8_t i = 0U; i < NOTE_FX_MAX_OUTPUTS; ++i)
    {
        note_fx_owned_t *const owned = &runtime->common.owned[i];
        if ((owned->active == 0U)
                || (owned->off_sample >= end)
                || (euclid_owned_source_is_closing(runtime, owned) != 0U))
        {
            continue;
        }
        const uint64_t off_sample = (owned->off_sample < start)
            ? start : owned->off_sample;
        const note_fx_result_t result = euclid_emit_owned_off(
            runtime, track, slot, owned, off_sample, emit, context);
        if (closure_is_settled(result) == 0U)
            return result;
    }
    return NOTE_EVENT_RESULT_ACCEPTED;
}

static void euclid_generate_at_deadline(
    note_fx_slot_runtime_t *runtime, uint8_t track, uint8_t slot,
    uint64_t start, uint64_t end, uint32_t samples_per_step_q16,
    note_fx_emit_fn emit, void *context)
{
    if ((runtime->common.model != NOTE_FX_MODEL_EUCLID)
            || (runtime->fx.euclid.source_count == 0U)
            || (runtime->common.next_sample >= end))
    {
        return;
    }
    if (runtime->common.next_sample < start)
        runtime->common.next_sample = start;

    const uint64_t sample = runtime->common.next_sample;
    const uint64_t period = rate_period(runtime->fx.euclid.division,
                                        samples_per_step_q16);
    if (((runtime->fx.euclid.mask >> runtime->fx.euclid.phase) & 1U) != 0U)
    {
        for (uint8_t source_index = 0U;
             source_index < runtime->fx.euclid.source_count; ++source_index)
        {
            const note_fx_euclid_source_t *const source =
                &runtime->fx.euclid.source[source_index];
            if ((euclid_source_is_active(source) == 0U)
                    || (euclid_source_is_closing(source) != 0U))
                continue;
            note_fx_owned_t *owned = 0;
            for (uint8_t owned_index = 0U;
                 owned_index < NOTE_FX_MAX_OUTPUTS; ++owned_index)
            {
                if (runtime->common.owned[owned_index].active == 0U)
                {
                    owned = &runtime->common.owned[owned_index];
                    break;
                }
            }
            if (owned == 0)
            {
                continue;
            }

            const uint32_t token = next_fx_token(source->token);
            *owned = (note_fx_owned_t){
                .active = 1U,
                .note = source->note,
                .destination = source->destination,
                .source_provenance = euclid_source_provenance(source),
                .source_token = source->token,
                .source_generation = source->generation,
                .causal_source_token = source->causal_source_token,
                .token = token,
                .generation = runtime->common.generation,
                .off_sample = sample + period
            };
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
                .source_token = source->causal_source_token,
                .occurrence_id = token,
                .generation = runtime->common.generation
            };
            const note_fx_result_t result = (emit != 0)
                ? emit(&on, context) : NOTE_EVENT_RESULT_ACCEPTED;
            if (result != NOTE_EVENT_RESULT_ACCEPTED)
            {
                owned->active = 0U;
            }
        }
    }

    if ((uint8_t)(runtime->fx.euclid.phase + 1U) >= runtime->fx.euclid.length)
        runtime->fx.euclid.phase = 0U;
    else
        ++runtime->fx.euclid.phase;
    runtime->common.next_sample = sample + period;
}

note_fx_result_t note_fx_engine_process(uint64_t start, uint16_t frames,
                                        uint32_t step_q16,
                                        note_fx_emit_fn emit, void *context)
{
    if (g_work_slot_mask == 0U)
        return NOTE_EVENT_RESULT_ACCEPTED;
    const uint64_t end = start + frames;
    /* First pass: every due owned closure precedes every newly generated On. */
    uint64_t work_mask = g_work_slot_mask;
    while (work_mask != 0U) {
            const uint32_t work_index = (uint32_t)__builtin_ctzll(work_mask);
            work_mask &= work_mask - 1U;
            const uint8_t track = (uint8_t)(work_index / NOTE_FX_SLOT_COUNT);
            const uint8_t slot = (uint8_t)(work_index % NOTE_FX_SLOT_COUNT);
            note_fx_slot_runtime_t *const runtime = &g_slot[track][slot];
            if (runtime->common.model == NOTE_FX_MODEL_EUCLID)
            {
                const note_fx_result_t result = euclid_process_closures(
                    runtime, track, slot, start, end, emit, context);
                if (closure_is_settled(result) == 0U)
                    return result;
                refresh_slot_work(track, slot);
                continue;
            }
            if (runtime->common.model != NOTE_FX_MODEL_ARP ||
                runtime->fx.arp.arp.count == 0U
                || runtime->common.next_sample >= end) continue;
            if (runtime->common.next_sample < start)
                runtime->common.next_sample = start;
            for (uint8_t i = 0U; i < NOTE_FX_MAX_OUTPUTS; ++i) {
                note_fx_owned_t *const owned = &runtime->common.owned[i];
                if (owned->active == 0U) continue;
                const note_fx_event_t off = {
                    .sample_abs = runtime->common.next_sample,
                    .track = track,
                    .destination_id = owned->destination,
                    .note = owned->note,
                    .velocity = 0U,
                    .kind = NOTE_EVENT_KIND_OFF,
                    .provenance = NOTE_EVENT_SOURCE_FX,
                    .stage = (uint8_t)(slot + 1U),
                    .flags = NOTE_EVENT_FLAG_GENERATED,
                    .source_token = owned->causal_source_token,
                    .occurrence_id = owned->token,
                    .generation = owned->generation
                };
                const note_fx_result_t result = (emit != 0)
                    ? emit(&off, context) : NOTE_EVENT_RESULT_ACCEPTED;
                if (closure_is_settled(result) == 0U)
                    return result;
                owned->active = 0U;
            }
            refresh_slot_work(track, slot);
    }

    /* Second pass: generate only after the global closure pass completed. */
    uint64_t processed_mask = 0U;
    while ((work_mask = (g_work_slot_mask & ~processed_mask)) != 0U) {
            const uint32_t work_index = (uint32_t)__builtin_ctzll(work_mask);
            processed_mask |= UINT64_C(1) << work_index;
            const uint8_t track = (uint8_t)(work_index / NOTE_FX_SLOT_COUNT);
            const uint8_t slot = (uint8_t)(work_index % NOTE_FX_SLOT_COUNT);
            note_fx_slot_runtime_t *const runtime = &g_slot[track][slot];
            if (runtime->common.model == NOTE_FX_MODEL_EUCLID)
            {
                if (slot_has_owned(runtime) == 0U)
                {
                    euclid_generate_at_deadline(runtime, track, slot, start, end,
                                                step_q16, emit, context);
                }
                refresh_slot_work(track, slot);
                continue;
            }
            if ((runtime->common.model != NOTE_FX_MODEL_ARP)
                    || (runtime->fx.arp.arp.count == 0U)
                    || (runtime->common.next_sample >= end)) continue;
            uint8_t release_pending = 0U;
            for (uint8_t i = 0U; i < NOTE_FX_MAX_OUTPUTS; ++i)
                release_pending |= runtime->common.owned[i].active;
            if (release_pending != 0U) {
                runtime->common.next_sample += rate_period(
                    runtime->fx.arp.rate, step_q16);
                refresh_slot_work(track, slot);
                continue;
            }
            uint8_t note, velocity;
            if (note_fx_arp_next(&runtime->fx.arp.arp,
                (note_fx_arp_style_t)runtime->fx.arp.style,
                runtime->fx.arp.range,
                &note, &velocity) == 0U) {
                const note_fx_result_t result = release_slot(
                    track, slot, runtime->common.next_sample, emit, context);
                if (closure_is_settled(result) == 0U)
                    return result;
                continue;
            }
            note_fx_owned_t *const owned = &runtime->common.owned[0];
            owned->active = 1U; owned->note = note;
            owned->source_token = runtime->fx.arp.arp.last_source_token;
            owned->source_generation = runtime->fx.arp.arp.last_source_generation;
            owned->source_provenance = NOTE_EVENT_SOURCE_FX;
            owned->causal_source_token =
                runtime->fx.arp.arp.last_causal_source_token;
            owned->destination = runtime->fx.arp.destination;
            owned->token = next_fx_token(
                runtime->fx.arp.arp.last_source_token);
            owned->generation = runtime->common.generation;
            const note_fx_event_t on = {
                .sample_abs = runtime->common.next_sample,
                .track = track,
                .destination_id = owned->destination,
                .note = note,
                .velocity = velocity,
                .kind = NOTE_EVENT_KIND_ON,
                .provenance = NOTE_EVENT_SOURCE_FX,
                .stage = (uint8_t)(slot + 1U),
                .flags = NOTE_EVENT_FLAG_GENERATED,
                .source_token = owned->causal_source_token,
                .occurrence_id = owned->token,
                .generation = owned->generation
            };
            const note_fx_result_t result = (emit != 0)
                ? emit(&on, context) : NOTE_EVENT_RESULT_ACCEPTED;
            if (result != NOTE_EVENT_RESULT_ACCEPTED)
            {
                owned->active = 0U;
            }
            runtime->common.next_sample += rate_period(
                runtime->fx.arp.rate, step_q16);
            refresh_slot_work(track, slot);
    }
    return NOTE_EVENT_RESULT_ACCEPTED;
}

note_fx_result_t note_fx_engine_cleanup(uint8_t track, uint64_t sample,
                                        note_fx_emit_fn emit, void *context)
{
    if (track >= NOTE_FX_TRACK_COUNT)
        return NOTE_EVENT_RESULT_DROPPED_POLICY;
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        const note_fx_result_t result = release_slot(
            track, slot, sample, emit, context);
        if (closure_is_settled(result) == 0U)
            return result;
    }
    return NOTE_EVENT_RESULT_ACCEPTED;
}
