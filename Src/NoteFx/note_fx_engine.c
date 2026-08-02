#include "NoteFx/note_fx_engine.h"

#include <string.h>

#include "Seq/seq_division_catalog.h"

typedef struct {
    uint8_t active, note, destination;
    uint32_t source_token, source_generation;
    uint32_t token, generation;
} note_fx_owned_t;

typedef struct {
    note_fx_arp_t arp;
    note_fx_owned_t owned[NOTE_FX_MAX_OUTPUTS];
    uint64_t next_sample;
    uint32_t generation;
    uint8_t model, rate, style, range, destination;
} note_fx_slot_runtime_t;

static note_fx_slot_runtime_t g_slot[NOTE_FX_TRACK_COUNT][NOTE_FX_SLOT_COUNT];
static note_fx_diag_t g_diag[NOTE_FX_TRACK_COUNT];
static uint32_t g_token;

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
            .flags = NOTE_EVENT_FLAG_GENERATED,
            .source_token = owned->source_token,
            .occurrence_id = owned->token,
            .generation = owned->generation
        };
        if (emit != 0) (void)emit(&event, context);
        owned->active = 0U;
    }
    note_fx_arp_init(&runtime->arp, 0x9E3779B9U ^ ((uint32_t)track << 8) ^ slot);
    runtime->next_sample = 0U;
}

void note_fx_engine_init(void)
{
    memset(g_slot, 0, sizeof(g_slot));
    memset(g_diag, 0, sizeof(g_diag));
    g_token = 0U;
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
        for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot) {
            g_slot[track][slot].range = 1U;
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
    runtime->model = (model < NOTE_FX_MODEL_COUNT) ? model : NOTE_FX_MODEL_OFF;
    runtime->rate = (rate < 8U) ? rate : 2U;
    runtime->style = (style <= NOTE_FX_ARP_RANDOM) ? style : NOTE_FX_ARP_ORDER;
    runtime->range = (range >= 1U && range <= 4U) ? range : 1U;
}

note_fx_result_t note_fx_engine_stage_source(const note_fx_event_t *event, uint8_t slot,
                                             note_fx_emit_fn emit, void *context)
{
    if (!note_event_is_valid(event) || event->track >= NOTE_FX_TRACK_COUNT
            || slot >= NOTE_FX_SLOT_COUNT || event->stage != slot)
        return NOTE_EVENT_RESULT_DROPPED_POLICY;

    note_fx_slot_runtime_t *const runtime = &g_slot[event->track][slot];
    if (runtime->model != NOTE_FX_MODEL_ARP)
    {
        note_fx_event_t forwarded = *event;
        forwarded.stage = (uint8_t)(slot + 1U);
        if (emit != 0) (void)emit(&forwarded, context);
        return NOTE_EVENT_RESULT_ACCEPTED;
    }
    if (event->kind == NOTE_EVENT_KIND_OFF) {
        if (note_fx_arp_note_off(&runtime->arp, event->source_token,
                                 event->generation) == 0U)
        {
            return NOTE_EVENT_RESULT_REJECTED_STALE;
        }
        for (uint8_t i = 0U; i < NOTE_FX_MAX_OUTPUTS; ++i) {
            note_fx_owned_t *const owned = &runtime->owned[i];
            if ((owned->active == 0U)
                    || (owned->source_token != event->source_token)
                    || (owned->source_generation != event->generation)) continue;
            note_fx_event_t off = *event;
            off.note = owned->note;
            off.velocity = 0U;
            off.kind = NOTE_EVENT_KIND_OFF;
            off.provenance = NOTE_EVENT_SOURCE_FX;
            off.stage = (uint8_t)(slot + 1U);
            off.flags |= NOTE_EVENT_FLAG_GENERATED;
            off.source_token = owned->source_token;
            off.occurrence_id = owned->token;
            off.generation = owned->generation;
            off.destination_id = owned->destination;
            if (emit != 0) (void)emit(&off, context);
            owned->active = 0U;
        }
        if (runtime->arp.count == 0U) runtime->next_sample = 0U;
        return 1U;
    }

    const uint8_t was_empty = runtime->arp.count == 0U;
    if (note_fx_arp_note_on(&runtime->arp, event->note, event->velocity,
                            event->source_token, event->generation) == 0U) {
        ++g_diag[event->track].saturations; ++g_diag[event->track].dropped_note_ons;
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

void note_fx_engine_process(uint64_t start, uint16_t frames, uint32_t step_q16,
                            note_fx_emit_fn emit, void *context)
{
    const uint64_t end = start + frames;
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track) {
        for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot) {
            note_fx_slot_runtime_t *const runtime = &g_slot[track][slot];
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
                    .flags = NOTE_EVENT_FLAG_GENERATED,
                    .source_token = owned->source_token,
                    .occurrence_id = owned->token,
                    .generation = owned->generation
                };
                if (emit != 0) (void)emit(&off, context);
                owned->active = 0U;
            }
            uint8_t note, velocity;
            if (note_fx_arp_next(&runtime->arp,
                (note_fx_arp_style_t)runtime->style, runtime->range,
                &note, &velocity) == 0U) {
                ++g_diag[track].saturations; ++g_diag[track].dropped_note_ons;
                release_slot(track, slot, runtime->next_sample, emit, context);
                continue;
            }
            note_fx_owned_t *const owned = &runtime->owned[0];
            owned->active = 1U; owned->note = note;
            owned->source_token = runtime->arp.last_source_token;
            owned->source_generation = runtime->arp.last_source_generation;
            owned->destination = runtime->destination;
            owned->token = ++g_token;
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
                .source_token = owned->source_token,
                .occurrence_id = owned->token,
                .generation = owned->generation
            };
            const note_fx_result_t result = (emit != 0)
                ? emit(&on, context) : NOTE_EVENT_RESULT_ACCEPTED;
            if (result != NOTE_EVENT_RESULT_ACCEPTED)
                owned->active = 0U;
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
    const note_fx_diag_t zero = {0U,0U};
    return track < NOTE_FX_TRACK_COUNT ? g_diag[track] : zero;
}

uint64_t note_fx_engine_next_deadline(void)
{
    uint64_t next = UINT64_MAX;
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
        for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot) {
            const note_fx_slot_runtime_t *const runtime = &g_slot[track][slot];
            if (runtime->model == NOTE_FX_MODEL_ARP &&
                runtime->arp.count != 0U && runtime->next_sample < next)
                next = runtime->next_sample;
        }
    return next;
}
