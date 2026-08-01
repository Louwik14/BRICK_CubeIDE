#include "NoteFx/note_fx_engine.h"

#include <string.h>

typedef struct {
    uint8_t active, note, source_note, destination;
    uint32_t token, generation;
} note_fx_owned_t;

typedef struct {
    note_fx_arp_t arp;
    note_fx_owned_t owned[NOTE_FX_MAX_OUTPUTS];
    uint64_t next_sample;
    uint32_t generation;
    uint8_t model, rate, style, range, suspended, destination;
} note_fx_slot_runtime_t;

static note_fx_slot_runtime_t g_slot[NOTE_FX_TRACK_COUNT][NOTE_FX_SLOT_COUNT];
static note_fx_diag_t g_diag[NOTE_FX_TRACK_COUNT];
static uint32_t g_token;

static void release_slot(uint8_t track, uint8_t slot, uint64_t sample,
                         note_fx_emit_fn emit, void *context)
{
    note_fx_slot_runtime_t *const runtime = &g_slot[track][slot];
    ++runtime->generation;
    for (uint8_t i = 0U; i < NOTE_FX_MAX_OUTPUTS; ++i) {
        note_fx_owned_t *const owned = &runtime->owned[i];
        if (owned->active == 0U) continue;
        const note_fx_event_t event = { sample, owned->token, owned->generation,
            track, owned->note, 0U, owned->destination, NOTE_FX_EVENT_OFF };
        if (emit != 0) emit(&event, context);
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

uint8_t note_fx_engine_source(const note_fx_event_t *event,
                              note_fx_emit_fn emit, void *context)
{
    if (event == 0 || event->track >= NOTE_FX_TRACK_COUNT) return 0U;
    note_fx_slot_runtime_t *runtime = 0;
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
        if (g_slot[event->track][slot].model == NOTE_FX_MODEL_ARP) {
            runtime = &g_slot[event->track][slot]; break;
        }
    if (runtime == 0) { if (emit != 0) emit(event, context); return 1U; }
    if (runtime->suspended != 0U) return 1U;

    if (event->type == NOTE_FX_EVENT_OFF) {
        (void)note_fx_arp_note_off(&runtime->arp, event->note);
        for (uint8_t i = 0U; i < NOTE_FX_MAX_OUTPUTS; ++i) {
            note_fx_owned_t *const owned = &runtime->owned[i];
            if (owned->active == 0U || owned->source_note != event->note) continue;
            note_fx_event_t off = *event;
            off.note = owned->note; off.token = owned->token;
            off.generation = owned->generation; off.destination = owned->destination;
            if (emit != 0) emit(&off, context);
            owned->active = 0U;
        }
        if (runtime->arp.count == 0U) runtime->next_sample = 0U;
        return 1U;
    }

    const uint8_t was_empty = runtime->arp.count == 0U;
    if (note_fx_arp_note_on(&runtime->arp, event->note, event->velocity) == 0U) {
        ++g_diag[event->track].saturations; ++g_diag[event->track].dropped_note_ons;
        return 0U;
    }
    runtime->destination = event->destination;
    if (was_empty != 0U) runtime->next_sample = event->sample;
    return 1U;
}

static uint64_t rate_period(uint8_t rate, uint32_t samples_per_step_q16)
{
    static const uint8_t numerator[8] = {4,2,1,1,8,4,2,1};
    static const uint8_t denominator[8] = {1,1,1,2,3,3,3,3};
    const uint64_t value = ((uint64_t)samples_per_step_q16 * numerator[rate]) /
                           ((uint64_t)denominator[rate] << 16);
    return value != 0U ? value : 1U;
}

void note_fx_engine_process(uint64_t start, uint16_t frames, uint32_t step_q16,
                            note_fx_emit_fn emit, void *context)
{
    const uint64_t end = start + frames;
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track) {
        uint8_t budget = NOTE_FX_MAX_EMISSIONS_PER_BLOCK;
        for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot) {
            note_fx_slot_runtime_t *const runtime = &g_slot[track][slot];
            if (runtime->model != NOTE_FX_MODEL_ARP || runtime->suspended != 0U ||
                runtime->arp.count == 0U || runtime->next_sample >= end) continue;
            if (runtime->next_sample < start) runtime->next_sample = start;
            for (uint8_t i = 0U; i < NOTE_FX_MAX_OUTPUTS; ++i) {
                note_fx_owned_t *const owned = &runtime->owned[i];
                if (owned->active == 0U || budget == 0U) continue;
                const note_fx_event_t off = { runtime->next_sample, owned->token,
                    owned->generation, track, owned->note, 0U, owned->destination,
                    NOTE_FX_EVENT_OFF };
                if (emit != 0) emit(&off, context);
                owned->active = 0U; --budget;
            }
            uint8_t note, velocity;
            if (budget == 0U || note_fx_arp_next(&runtime->arp,
                (note_fx_arp_style_t)runtime->style, runtime->range,
                &note, &velocity) == 0U) {
                ++g_diag[track].saturations; ++g_diag[track].dropped_note_ons;
                release_slot(track, slot, runtime->next_sample, emit, context);
                continue;
            }
            note_fx_owned_t *const owned = &runtime->owned[0];
            owned->active = 1U; owned->note = note;
            owned->source_note = runtime->arp.last_source_note;
            owned->destination = runtime->destination;
            owned->token = ++g_token; owned->generation = runtime->generation;
            const note_fx_event_t on = { runtime->next_sample, owned->token,
                owned->generation, track, note, velocity, owned->destination,
                NOTE_FX_EVENT_ON };
            if (emit != 0) emit(&on, context);
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

void note_fx_engine_suspend(uint8_t track, uint8_t suspended, uint64_t sample,
                            note_fx_emit_fn emit, void *context)
{
    if (track >= NOTE_FX_TRACK_COUNT) return;
    if (suspended != 0U) note_fx_engine_cleanup(track, sample, emit, context);
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
        g_slot[track][slot].suspended = suspended != 0U;
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
            if (runtime->model == NOTE_FX_MODEL_ARP && runtime->suspended == 0U &&
                runtime->arp.count != 0U && runtime->next_sample < next)
                next = runtime->next_sample;
        }
    return next;
}
