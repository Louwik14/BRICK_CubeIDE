#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "NoteFx/note_fx_engine.h"
#include "Seq/seq_division_catalog.h"

static note_fx_event_t g_events[NOTE_FX_SLOT_COUNT * NOTE_FX_MAX_OUTPUTS * 2U];
static uint16_t g_event_count;
static note_fx_event_t g_chain_events[NOTE_FX_SLOT_COUNT * 2U];
static uint8_t g_chain_event_count;

static note_fx_result_t capture_event(const note_fx_event_t *event, void *context)
{
    (void)context;
    assert(event != 0);
    assert(g_event_count < (uint16_t)(sizeof(g_events) / sizeof(g_events[0])));
    g_events[g_event_count++] = *event;
    return NOTE_EVENT_RESULT_ACCEPTED;
}

static void clear_events(void)
{
    memset(g_events, 0, sizeof(g_events));
    g_event_count = 0U;
}

static note_fx_result_t capture_chain_event(const note_fx_event_t *event,
                                            void *context)
{
    (void)context;
    assert(event != 0);
    assert(g_chain_event_count
           < (uint8_t)(sizeof(g_chain_events) / sizeof(g_chain_events[0])));
    g_chain_events[g_chain_event_count++] = *event;
    if (event->stage < NOTE_EVENT_STAGE_TERMINAL_HANDOFF)
    {
        return note_fx_engine_stage_source(event, event->stage,
                                           capture_chain_event, 0);
    }
    return NOTE_EVENT_RESULT_ACCEPTED;
}

static void clear_chain_events(void)
{
    memset(g_chain_events, 0, sizeof(g_chain_events));
    g_chain_event_count = 0U;
}

static note_fx_event_t source_event(uint8_t kind, uint8_t note,
                                    uint64_t sample, uint32_t token,
                                    uint8_t stage)
{
    return (note_fx_event_t){
        .sample_abs = sample,
        .track = 0U,
        .destination_id = 4U,
        .note = note,
        .velocity = (kind == NOTE_EVENT_KIND_ON) ? 100U : 0U,
        .kind = kind,
        .provenance = NOTE_EVENT_SOURCE_KEY,
        .stage = stage,
        .source_token = token,
        .occurrence_id = token,
        .generation = 1U
    };
}

static void configure_euclid(uint8_t slot, uint8_t length, uint8_t pulse)
{
    note_fx_engine_configure(0U, slot, NOTE_FX_MODEL_EUCLID,
                             length, pulse, SEQ_DIVISION_ARP_DEFAULT_INDEX);
}

int main(void)
{
    const uint32_t ten_samples_q16 = 10U << 16;
    note_fx_engine_init();
    configure_euclid(0U, 4U, 2U);

    clear_events();
    note_fx_event_t event = source_event(NOTE_EVENT_KIND_ON, 60U, 100U, 1U, 0U);
    assert(note_fx_engine_stage_source(&event, 0U, capture_event, 0) == 1);
    note_fx_engine_process(100U, 1U, ten_samples_q16, capture_event, 0);
    assert(g_event_count == 1U);
    assert(g_events[0].kind == NOTE_EVENT_KIND_ON);
    assert(g_events[0].sample_abs == 100U);
    assert(g_events[0].stage == 1U);
    assert(g_events[0].source_token == g_events[0].occurrence_id);
    const uint32_t first_child = g_events[0].occurrence_id;

    clear_events();
    note_fx_engine_process(110U, 1U, ten_samples_q16, capture_event, 0);
    assert(g_event_count == 1U);
    assert(g_events[0].kind == NOTE_EVENT_KIND_OFF);
    assert(g_events[0].sample_abs == 110U);
    assert(g_events[0].occurrence_id == first_child);

    clear_events();
    note_fx_engine_process(120U, 1U, ten_samples_q16, capture_event, 0);
    assert(g_event_count == 1U);
    assert(g_events[0].kind == NOTE_EVENT_KIND_ON);
    assert(g_events[0].sample_abs == 120U);

    clear_events();
    event = source_event(NOTE_EVENT_KIND_OFF, 60U, 125U, 1U, 0U);
    assert(note_fx_engine_stage_source(&event, 0U, capture_event, 0) == 1);
    assert(g_event_count == 1U);
    assert(g_events[0].kind == NOTE_EVENT_KIND_OFF);
    assert(g_events[0].sample_abs == 125U);
    assert(note_fx_engine_next_deadline() == UINT64_MAX);

    /* A short source is closed before the next pulse and never latches. */
    note_fx_engine_init();
    configure_euclid(0U, 4U, 2U);
    clear_events();
    event = source_event(NOTE_EVENT_KIND_ON, 64U, 200U, 2U, 0U);
    assert(note_fx_engine_stage_source(&event, 0U, capture_event, 0) == 1);
    note_fx_engine_process(200U, 1U, ten_samples_q16, capture_event, 0);
    assert(g_event_count == 1U);
    clear_events();
    event = source_event(NOTE_EVENT_KIND_OFF, 64U, 205U, 2U, 0U);
    assert(note_fx_engine_stage_source(&event, 0U, capture_event, 0) == 1);
    assert(g_event_count == 1U);
    clear_events();
    note_fx_engine_process(210U, 1U, ten_samples_q16, capture_event, 0);
    assert(g_event_count == 0U);

    /* PULSE=0 keeps the source active and advances its deadline silently. */
    note_fx_engine_init();
    configure_euclid(0U, 4U, 0U);
    clear_events();
    event = source_event(NOTE_EVENT_KIND_ON, 67U, 300U, 3U, 0U);
    assert(note_fx_engine_stage_source(&event, 0U, capture_event, 0) == 1);
    note_fx_engine_process(300U, 1U, ten_samples_q16, capture_event, 0);
    assert(g_event_count == 0U);
    assert(note_fx_engine_next_deadline() == 310U);
    event = source_event(NOTE_EVENT_KIND_OFF, 67U, 305U, 3U, 0U);
    assert(note_fx_engine_stage_source(&event, 0U, capture_event, 0) == 1);

    /* Two slots keep independent phases and both consume their own source. */
    note_fx_engine_init();
    configure_euclid(0U, 4U, 2U);
    configure_euclid(1U, 4U, 2U);
    clear_events();
    event = source_event(NOTE_EVENT_KIND_ON, 60U, 400U, 4U, 0U);
    assert(note_fx_engine_stage_source(&event, 0U, capture_event, 0) == 1);
    event = source_event(NOTE_EVENT_KIND_ON, 72U, 400U, 5U, 1U);
    assert(note_fx_engine_stage_source(&event, 1U, capture_event, 0) == 1);
    note_fx_engine_process(400U, 1U, ten_samples_q16, capture_event, 0);
    assert(g_event_count == 2U);
    assert(g_events[0].sample_abs == 400U);
    assert(g_events[1].sample_abs == 400U);
    assert(g_events[0].stage == 1U);
    assert(g_events[1].stage == 2U);

    /* Each of the three slots creates a distinct child and the third slot
     * hands it to the common terminal boundary. */
    note_fx_engine_init();
    configure_euclid(0U, 4U, 2U);
    configure_euclid(1U, 4U, 2U);
    configure_euclid(2U, 4U, 2U);
    clear_chain_events();
    event = source_event(NOTE_EVENT_KIND_ON, 60U, 500U, 6U, 0U);
    assert(note_fx_engine_stage_source(&event, 0U,
                                       capture_chain_event, 0) == 1);
    note_fx_engine_process(500U, 1U, ten_samples_q16,
                           capture_chain_event, 0);
    assert(g_chain_event_count == NOTE_FX_SLOT_COUNT);
    for (uint8_t i = 0U; i < NOTE_FX_SLOT_COUNT; ++i)
    {
        assert(g_chain_events[i].kind == NOTE_EVENT_KIND_ON);
        assert(g_chain_events[i].stage == (uint8_t)(i + 1U));
        assert(g_chain_events[i].source_token
               == g_chain_events[i].occurrence_id);
        if (i != 0U)
            assert(g_chain_events[i].occurrence_id
                   != g_chain_events[i - 1U].occurrence_id);
    }

    clear_chain_events();
    note_fx_engine_process(510U, 1U, ten_samples_q16,
                           capture_chain_event, 0);
    assert(g_chain_event_count == NOTE_FX_SLOT_COUNT);
    for (uint8_t i = 0U; i < NOTE_FX_SLOT_COUNT; ++i)
    {
        assert(g_chain_events[i].kind == NOTE_EVENT_KIND_OFF);
        assert(g_chain_events[i].sample_abs == 510U);
        assert(g_chain_events[i].stage == (uint8_t)(i + 1U));
        assert(g_chain_events[i].occurrence_id
               == g_chain_events[i].source_token);
    }

    /* Three independent EUCLID instances accept the full fixed source
     * ledger and generate one bounded fan-out per source at the fastest
     * full-mask deadline. */
    note_fx_engine_init();
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
        configure_euclid(slot, 1U, 1U);
    clear_events();
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        for (uint8_t source = 0U; source < NOTE_FX_EUCLID_MAX_SOURCES; ++source)
        {
            event = source_event(NOTE_EVENT_KIND_ON,
                                 (uint8_t)(36U + slot * 16U + source),
                                 700U, (uint32_t)(1000U + slot * 16U + source),
                                 slot);
            assert(note_fx_engine_stage_source(&event, slot,
                                               capture_event, 0) == 1);
        }
    }
    note_fx_engine_process(700U, 1U, ten_samples_q16, capture_event, 0);
    assert(g_event_count == (uint16_t)(NOTE_FX_SLOT_COUNT
                                       * NOTE_FX_EUCLID_MAX_SOURCES));
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        const note_fx_slot_diag_t diag = note_fx_engine_slot_diag(0U, slot);
        assert(diag.source_high_water == NOTE_FX_EUCLID_MAX_SOURCES);
        assert(diag.owned_high_water == NOTE_FX_EUCLID_MAX_OWNED);
    }
    clear_events();
    note_fx_engine_process(710U, 1U, ten_samples_q16, capture_event, 0);
    assert(g_event_count == (uint16_t)(NOTE_FX_SLOT_COUNT
                                       * NOTE_FX_EUCLID_MAX_SOURCES));
    for (uint16_t i = 0U; i < g_event_count; ++i)
        assert(g_events[i].kind == NOTE_EVENT_KIND_OFF);

    /* Reconfiguration is slot-local: old owned notes close first, the
     * changed slot rephases, and a neighboring active slot keeps its source
     * and schedule. */
    note_fx_engine_init();
    configure_euclid(0U, 4U, 2U);
    configure_euclid(1U, 4U, 2U);
    clear_events();
    event = source_event(NOTE_EVENT_KIND_ON, 60U, 600U, 7U, 0U);
    assert(note_fx_engine_stage_source(&event, 0U, capture_event, 0) == 1);
    event = source_event(NOTE_EVENT_KIND_ON, 72U, 600U, 8U, 1U);
    assert(note_fx_engine_stage_source(&event, 1U, capture_event, 0) == 1);
    note_fx_engine_process(600U, 1U, ten_samples_q16, capture_event, 0);
    assert(g_event_count == 2U);
    uint32_t slot0_child = 0U;
    uint32_t slot1_child = 0U;
    for (uint8_t i = 0U; i < g_event_count; ++i)
    {
        if (g_events[i].note == 60U)
            slot0_child = g_events[i].occurrence_id;
        if (g_events[i].note == 72U)
            slot1_child = g_events[i].occurrence_id;
    }
    assert(slot0_child != 0U);
    assert(slot1_child != 0U);

    clear_events();
    note_fx_engine_configure(0U, 0U, NOTE_FX_MODEL_EUCLID,
                             8U, 4U, SEQ_DIVISION_ARP_DEFAULT_INDEX);
    note_fx_engine_process(610U, 1U, ten_samples_q16, capture_event, 0);
    assert(g_event_count == 3U);
    assert(g_events[0].kind == NOTE_EVENT_KIND_OFF);
    assert(g_events[0].note == 60U);
    assert(g_events[0].occurrence_id == slot0_child);
    assert(g_events[1].kind == NOTE_EVENT_KIND_OFF);
    assert(g_events[1].note == 72U);
    assert(g_events[1].occurrence_id == slot1_child);
    assert(g_events[2].kind == NOTE_EVENT_KIND_ON);
    assert(g_events[2].note == 60U);

    clear_events();
    note_fx_engine_configure(0U, 0U, NOTE_FX_MODEL_OFF, 0U, 0U, 0U);
    note_fx_engine_process(620U, 1U, ten_samples_q16, capture_event, 0);
    assert(g_event_count == 2U);
    assert(g_events[0].kind == NOTE_EVENT_KIND_OFF);
    assert(g_events[0].note == 60U);
    assert(g_events[1].kind == NOTE_EVENT_KIND_ON);
    assert(g_events[1].note == 72U);

    return 0;
}
