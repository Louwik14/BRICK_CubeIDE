#include "Seq/seq_transport_owner.h"
#include "Seq/seq_live_rec_session.h"
#include "Platform/memory_layout.h"
#include <string.h>

static CONTROL_M4_SRAM2 seq_runtime_state_t g_state;
static volatile uint64_t g_sample_timeline;
static volatile uint32_t g_external_pulses;
static uint32_t g_transport_step;
static uint32_t g_midi_clock_period_q16;
static uint64_t g_midi_clock_next_q16;
static uint8_t g_midi_clock_enabled;

seq_runtime_state_t *seq_transport_owner_state(void){return &g_state;}
void seq_transport_owner_init(void){memset(&g_state,0,sizeof(g_state));
    g_sample_timeline=0U;g_external_pulses=0U;g_transport_step=0U;
    g_midi_clock_period_q16=1U;g_midi_clock_next_q16=0U;g_midi_clock_enabled=0U;}
void seq_transport_owner_reset_sample_timeline(uint64_t sample){g_sample_timeline=sample;}
uint64_t seq_transport_owner_get_sample_timeline(void){return g_sample_timeline;}
uint32_t seq_transport_owner_get_transport_step(void){return g_transport_step;}
void seq_transport_owner_prepare_start_lifecycle(seq_runtime_state_t *state,
    seq_clock_bridge_t *clock_bridge,uint32_t now_tick){if(!state||!clock_bridge)return;
    seq_clock_bridge_prepare_internal_run(clock_bridge);state->running=0U;
    state->tick_accum=0U;state->ext_clock_tick_accum=0U;
    state->last_tick_count=now_tick;state->step_sample_q16=g_sample_timeline<<16;
    g_external_pulses=0U;g_transport_step=0U;seq_live_rec_session_reset_capture();}
void seq_transport_owner_begin_running_at_sample_q16(seq_runtime_state_t *state,
    seq_transport_fsm_t *fsm,seq_clock_bridge_t *clock_bridge,
    uint32_t now_tick,uint64_t start_q16){if(!state||!fsm||!clock_bridge
        ||seq_transport_fsm_is_running(fsm)==0U)return;
    state->running=1U;
    state->last_tick_count=now_tick;state->step_sample_q16=start_q16;
    state->ticks_per_step=(uint16_t)(clock_bridge->internal_next_step_ticks
        ?clock_bridge->internal_next_step_ticks:1U);g_transport_step=0U;
    memset(state->play_step,0,sizeof(state->play_step));
    memset(state->traversal_phase,0,sizeof(state->traversal_phase));
    memset(state->track_div_phase,0,sizeof(state->track_div_phase));
    seq_live_rec_session_on_transport_start();}
void seq_transport_owner_stop_lifecycle_apply(seq_runtime_state_t *state,
    uint64_t effective_sample){(void)effective_sample;if(!state)return;
    seq_live_rec_session_on_transport_stop(g_sample_timeline,state->samples_per_step_q16);
    state->running=0U;state->step_sample_q16=0U;g_external_pulses=0U;}
void seq_transport_owner_set_midi_clock_enabled(uint8_t enabled){g_midi_clock_enabled=enabled;}
void seq_transport_owner_set_midi_clock_period_q16(uint32_t period){g_midi_clock_period_q16=period?period:1U;}
void seq_transport_owner_rebase_midi_clock(uint64_t sample){g_midi_clock_next_q16=(sample<<16)+g_midi_clock_period_q16;(void)g_midi_clock_enabled;}
void seq_transport_owner_set_external_step_pulses_pending(uint32_t pending){g_external_pulses=pending;}
void seq_transport_owner_increment_external_step_pulses_pending(void){if(g_external_pulses!=UINT32_MAX)++g_external_pulses;}
uint32_t seq_transport_owner_external_step_pulses_pending(void){return g_external_pulses;}
void seq_transport_owner_enqueue_transport_start(uint64_t sample){g_sample_timeline=sample;}
