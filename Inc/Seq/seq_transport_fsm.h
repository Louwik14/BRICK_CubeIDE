#ifndef SEQ_TRANSPORT_FSM_H
#define SEQ_TRANSPORT_FSM_H

#include <stdint.h>

typedef enum
{
    SEQ_TRANSPORT_FSM_STOPPED = 0,
    SEQ_TRANSPORT_FSM_START_PENDING,
    SEQ_TRANSPORT_FSM_RUNNING
} seq_transport_fsm_state_t;

typedef struct
{
    seq_transport_fsm_state_t state;
    uint32_t rec_count_in_remaining_steps;
} seq_transport_fsm_t;

/*
 * Contract surface:
 * - transport state ownership and transitions.
 * - start/continue/stop/count-in policy.
 * - no tempo or clock-source policy here.
 */
void seq_transport_fsm_init(seq_transport_fsm_t *fsm);
/*
 * Contract surface:
 * - internal lifecycle reset / pending-abort helper.
 * - clears transport state without introducing clock or tempo policy.
 * - caller remains responsible for orchestration context.
 */
void seq_transport_fsm_reset(seq_transport_fsm_t *fsm);
uint8_t seq_transport_fsm_request_start(seq_transport_fsm_t *fsm,
                                        uint8_t rec_armed,
                                        uint8_t rec_count_in_mode);
uint8_t seq_transport_fsm_request_stop(seq_transport_fsm_t *fsm);
uint8_t seq_transport_fsm_request_continue(seq_transport_fsm_t *fsm);
/*
 * Contract surface:
 * - internal lifecycle helper to cancel a pending start.
 * - does not own clock policy or cadence.
 */
void seq_transport_fsm_abort_pending(seq_transport_fsm_t *fsm);
uint8_t seq_transport_fsm_on_step_pulse(seq_transport_fsm_t *fsm);

uint8_t seq_transport_fsm_is_stopped(const seq_transport_fsm_t *fsm);
uint8_t seq_transport_fsm_is_start_pending(const seq_transport_fsm_t *fsm);
uint8_t seq_transport_fsm_is_running(const seq_transport_fsm_t *fsm);
uint32_t seq_transport_fsm_get_rec_count_in_remaining_steps(const seq_transport_fsm_t *fsm);

uint8_t seq_transport_fsm_allow_advance(const seq_transport_fsm_t *fsm);
uint8_t seq_transport_fsm_allow_schedule_play(const seq_transport_fsm_t *fsm);
uint8_t seq_transport_fsm_allow_live_rec(const seq_transport_fsm_t *fsm, uint8_t rec_armed);

#endif /* SEQ_TRANSPORT_FSM_H */
