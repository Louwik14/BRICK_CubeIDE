#include "Audio/synth_waveform_audio.h"

#include <string.h>

#include "Platform/memory_layout.h"
#include "stm32h7xx.h"

#define SYNTH_WAVEFORM_PUBLISH_PERIOD_SAMPLES 2400U
#define SYNTH_WAVEFORM_INVALID_INSTANCE       UINT8_MAX

typedef struct
{
    uint8_t enabled;
    brick_entity_id_t entity_id;
    uint8_t engine;
    uint8_t osc_mask;
} synth_waveform_request_t;

typedef struct
{
    synth_waveform_request_t request;
    uint32_t generation;
    uint32_t cooldown_samples;
    synth_waveform_capture_state_t state;
    uint8_t selected_instance;
    uint8_t ready_mask;
    uint8_t cycle_started_mask;
    uint8_t next_bin[SYNTH_WAVEFORM_OSC_COUNT];
    uint32_t previous_phase[SYNTH_WAVEFORM_OSC_COUNT];
    int8_t staging[SYNTH_WAVEFORM_OSC_COUNT][SYNTH_WAVEFORM_POINT_COUNT];
    int8_t completed[SYNTH_WAVEFORM_OSC_COUNT][SYNTH_WAVEFORM_POINT_COUNT];
} synth_waveform_audio_state_t;

AUDIO_HOT static synth_waveform_audio_state_t g_audio_state;

void synth_waveform_init(void)
{
    memset((void *)&g_synth_waveform_layout, 0, sizeof(g_synth_waveform_layout));
    memset(&g_audio_state, 0, sizeof(g_audio_state));
    g_audio_state.selected_instance = SYNTH_WAVEFORM_INVALID_INSTANCE;
    __DMB();
}

static int8_t synth_waveform_q7(float sample)
{
    if (sample > 1.0f) sample = 1.0f;
    if (sample < -1.0f) sample = -1.0f;
    const float scaled = sample * 127.0f;
    return (int8_t)((scaled >= 0.0f) ? (scaled + 0.5f) : (scaled - 0.5f));
}

static void synth_waveform_audio_reset_capture(void);

uint8_t synth_waveform_audio_apply_request(brick_entity_id_t entity_id,
                                           synth_waveform_engine_t engine,
                                           uint8_t osc_mask)
{
    synth_waveform_request_t request = {
        .enabled = (osc_mask != 0U),
        .entity_id = entity_id,
        .engine = (uint8_t)engine,
        .osc_mask = (uint8_t)(osc_mask & 0x03U)
    };
    if ((request.enabled == 0U) || (request.osc_mask == 0U))
    {
        request.enabled = 0U;
        request.engine = SYNTH_WAVEFORM_ENGINE_NONE;
        request.osc_mask = 0U;
    }

    const uint8_t changed = (uint8_t)(memcmp(
        &request,&g_audio_state.request,sizeof(request)) != 0);
    g_audio_state.request=request;
    if (changed != 0U) synth_waveform_audio_reset_capture();
    return 1U;
}

static void synth_waveform_audio_reset_capture(void)
{
    g_audio_state.state = (g_audio_state.request.enabled != 0U)
        ? SYNTH_WAVEFORM_CAPTURE_ARMED : SYNTH_WAVEFORM_CAPTURE_IDLE;
    g_audio_state.selected_instance = SYNTH_WAVEFORM_INVALID_INSTANCE;
    g_audio_state.ready_mask = 0U;
    g_audio_state.cycle_started_mask = 0U;
    memset(g_audio_state.next_bin, 0, sizeof(g_audio_state.next_bin));
    memset(g_audio_state.previous_phase, 0, sizeof(g_audio_state.previous_phase));
    memset(g_audio_state.staging, 0, sizeof(g_audio_state.staging));
    memset(g_audio_state.completed, 0, sizeof(g_audio_state.completed));
}

void synth_waveform_audio_begin_block(uint32_t frames)
{
    if ((g_audio_state.request.enabled != 0U)
            && (g_audio_state.state == SYNTH_WAVEFORM_CAPTURE_IDLE))
    {
        const uint32_t remaining = UINT32_MAX - g_audio_state.cooldown_samples;
        g_audio_state.cooldown_samples += (frames < remaining) ? frames : remaining;
        if (g_audio_state.cooldown_samples >= SYNTH_WAVEFORM_PUBLISH_PERIOD_SAMPLES)
            g_audio_state.state = SYNTH_WAVEFORM_CAPTURE_ARMED;
    }
}

uint8_t synth_waveform_audio_target_is(brick_entity_id_t entity_id,
                                       synth_waveform_engine_t engine)
{
    return (uint8_t)((g_audio_state.request.enabled != 0U)
        && (g_audio_state.request.entity_id == entity_id)
        && (g_audio_state.request.engine == (uint8_t)engine));
}

void synth_waveform_audio_select_instance(brick_entity_id_t entity_id,
                                          synth_waveform_engine_t engine,
                                          uint8_t instance_id)
{
    if ((synth_waveform_audio_target_is(entity_id, engine) == 0U)
            || (g_audio_state.state == SYNTH_WAVEFORM_CAPTURE_IDLE)) return;
    if (g_audio_state.selected_instance != instance_id)
    {
        g_audio_state.ready_mask = 0U;
        g_audio_state.cycle_started_mask = 0U;
        memset(g_audio_state.next_bin, 0, sizeof(g_audio_state.next_bin));
        memset(g_audio_state.previous_phase, 0, sizeof(g_audio_state.previous_phase));
        g_audio_state.selected_instance = instance_id;
    }
}

uint8_t synth_waveform_audio_instance_mask(uint8_t instance_id)
{
    return (uint8_t)(((g_audio_state.state == SYNTH_WAVEFORM_CAPTURE_ARMED)
            || (g_audio_state.state == SYNTH_WAVEFORM_CAPTURE_ACTIVE))
        && (g_audio_state.selected_instance == instance_id)
        ? (g_audio_state.request.osc_mask & (uint8_t)~g_audio_state.ready_mask) : 0U);
}

void synth_waveform_audio_restart_instance(uint8_t instance_id)
{
    if (((g_audio_state.state != SYNTH_WAVEFORM_CAPTURE_ARMED)
            && (g_audio_state.state != SYNTH_WAVEFORM_CAPTURE_ACTIVE))
            || (g_audio_state.selected_instance != instance_id)) return;
    g_audio_state.ready_mask = 0U;
    g_audio_state.cycle_started_mask = 0U;
    memset(g_audio_state.next_bin, 0, sizeof(g_audio_state.next_bin));
    memset(g_audio_state.previous_phase, 0, sizeof(g_audio_state.previous_phase));
    memset(g_audio_state.staging, 0, sizeof(g_audio_state.staging));
    memset(g_audio_state.completed, 0, sizeof(g_audio_state.completed));
    g_audio_state.state = SYNTH_WAVEFORM_CAPTURE_ARMED;
}

static void synth_waveform_audio_publish(void)
{
    synth_waveform_snapshot_t snapshot;
    snapshot.generation = ++g_audio_state.generation;
    snapshot.entity_id = g_audio_state.request.entity_id;
    snapshot.engine = g_audio_state.request.engine;
    snapshot.osc_mask = g_audio_state.request.osc_mask;
    snapshot.voice_instance = g_audio_state.selected_instance;
    memcpy(snapshot.points, g_audio_state.completed, sizeof(snapshot.points));

    uint32_t sequence = g_synth_waveform_layout.sequence;
    if ((sequence & 1U) != 0U) sequence++;
    g_synth_waveform_layout.sequence = sequence + 1U;
    __DMB();
    g_synth_waveform_layout.snapshot = snapshot;
    __DMB();
    g_synth_waveform_layout.sequence = sequence + 2U;
    g_audio_state.ready_mask = 0U;
    g_audio_state.cycle_started_mask = 0U;
    g_audio_state.selected_instance = SYNTH_WAVEFORM_INVALID_INSTANCE;
    g_audio_state.cooldown_samples = 0U;
    g_audio_state.state = SYNTH_WAVEFORM_CAPTURE_IDLE;
}

void __attribute__((noinline)) synth_waveform_audio_capture_sample(
                              uint8_t instance_id,
                              uint8_t osc,
                              uint32_t carrier_phase,
                              float sample)
{
    if (((g_audio_state.state != SYNTH_WAVEFORM_CAPTURE_ARMED)
            && (g_audio_state.state != SYNTH_WAVEFORM_CAPTURE_ACTIVE))
            || (instance_id != g_audio_state.selected_instance)
            || (osc >= SYNTH_WAVEFORM_OSC_COUNT)
            || ((g_audio_state.request.osc_mask & (uint8_t)(1U << osc)) == 0U)) return;

    const uint8_t bit = (uint8_t)(1U << osc);
    const uint32_t previous = g_audio_state.previous_phase[osc];
    g_audio_state.previous_phase[osc] = carrier_phase;
    if ((g_audio_state.cycle_started_mask & bit) == 0U)
    {
        if (carrier_phase >= previous) return;
        g_audio_state.cycle_started_mask |= bit;
        g_audio_state.state = SYNTH_WAVEFORM_CAPTURE_ACTIVE;
        g_audio_state.next_bin[osc] = 1U;
        g_audio_state.staging[osc][0] = synth_waveform_q7(sample);
        return;
    }

    if (carrier_phase < previous)
    {
        /* A retrigger/phase reset cannot be distinguished from a wrap here;
         * either way it defines a clean new carrier cycle. */
        g_audio_state.next_bin[osc] = 1U;
        g_audio_state.staging[osc][0] = synth_waveform_q7(sample);
        return;
    }

    uint8_t next = g_audio_state.next_bin[osc];
    while (next < SYNTH_WAVEFORM_POINT_COUNT)
    {
        const uint32_t threshold = (uint32_t)(
            ((uint64_t)next << 32U) / SYNTH_WAVEFORM_POINT_COUNT);
        if (carrier_phase < threshold) break;
        g_audio_state.staging[osc][next++] = synth_waveform_q7(sample);
    }
    g_audio_state.next_bin[osc] = next;
    if (next == SYNTH_WAVEFORM_POINT_COUNT)
    {
        memcpy(g_audio_state.completed[osc], g_audio_state.staging[osc],
               SYNTH_WAVEFORM_POINT_COUNT);
        g_audio_state.ready_mask |= bit;
    }

    if (((g_audio_state.ready_mask & g_audio_state.request.osc_mask)
            == g_audio_state.request.osc_mask))
    {
        g_audio_state.state = SYNTH_WAVEFORM_CAPTURE_READY;
        synth_waveform_audio_publish();
    }
}
