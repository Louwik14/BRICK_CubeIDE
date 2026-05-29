/*
 * Module: seq_transport_fsm
 * Role: Machine d'états transport (STOPPED/START_PENDING/RUNNING).
 * Responsibilities: traiter requêtes start/stop/continue,
 * gérer le count-in REC et transitions d'état déterministes.
 * Integration: utilisée par seq_runtime; ne gère ni clock timing fin ni émission de notes.
 */
#include "Seq/seq_transport_fsm.h"

static uint32_t seq_transport_fsm_roll_steps_from_mode(uint8_t mode)
{
    switch (mode)
    {
        case 1U:
            return 4U;
        case 2U:
            return 8U;
        case 3U:
            return 16U;
        default:
            return 0U;
    }
}

void seq_transport_fsm_init(seq_transport_fsm_t *fsm)
{
    seq_transport_fsm_reset(fsm);
}

void seq_transport_fsm_reset(seq_transport_fsm_t *fsm)
{
    /* Internal lifecycle helper: clear transport state without introducing clock policy. */
    if (fsm == 0)
    {
        return;
    }

    fsm->state = SEQ_TRANSPORT_FSM_STOPPED;
    fsm->rec_count_in_remaining_steps = 0U;
}

uint8_t seq_transport_fsm_request_start(seq_transport_fsm_t *fsm,
                                        uint8_t rec_armed,
                                        uint8_t rec_roll_mode)
{
    /* Hybrid seam: transition policy lives here; clock policy only influences when pulses arrive. */
    if (fsm == 0)
    {
        return 0U;
    }

    if (fsm->state != SEQ_TRANSPORT_FSM_STOPPED)
    {
        return 0U;
    }

    fsm->rec_count_in_remaining_steps = (rec_armed != 0U)
                                       ? seq_transport_fsm_roll_steps_from_mode(rec_roll_mode)
                                       : 0U;

    if (fsm->rec_count_in_remaining_steps > 0U)
    {
        fsm->state = SEQ_TRANSPORT_FSM_START_PENDING;
        return 1U;
    }

    fsm->state = SEQ_TRANSPORT_FSM_RUNNING;
    return 1U;
}

uint8_t seq_transport_fsm_request_stop(seq_transport_fsm_t *fsm)
{
    if (fsm == 0)
    {
        return 0U;
    }

    if (fsm->state == SEQ_TRANSPORT_FSM_STOPPED)
    {
        return 0U;
    }

    fsm->state = SEQ_TRANSPORT_FSM_STOPPED;
    fsm->rec_count_in_remaining_steps = 0U;
    return 1U;
}

uint8_t seq_transport_fsm_request_continue(seq_transport_fsm_t *fsm)
{
    /* Hybrid seam: continue is a transport transition, not a tempo/clock decision. */
    if (fsm == 0)
    {
        return 0U;
    }

    if (fsm->state == SEQ_TRANSPORT_FSM_RUNNING)
    {
        return 0U;
    }

    if (fsm->state == SEQ_TRANSPORT_FSM_START_PENDING)
    {
        return 1U;
    }

    fsm->state = SEQ_TRANSPORT_FSM_RUNNING;
    return 1U;
}

void seq_transport_fsm_abort_pending(seq_transport_fsm_t *fsm)
{
    /* Internal lifecycle helper: cancel a pending start without touching tempo or cadence. */
    if (fsm == 0)
    {
        return;
    }

    if (fsm->state == SEQ_TRANSPORT_FSM_START_PENDING)
    {
        fsm->state = SEQ_TRANSPORT_FSM_STOPPED;
        fsm->rec_count_in_remaining_steps = 0U;
    }
}

uint8_t seq_transport_fsm_on_step_pulse(seq_transport_fsm_t *fsm)
{
    /* Hybrid seam: step pulses are consumed here to advance transport state, but the pulse source is owned upstream. */
    if ((fsm == 0) || (fsm->state != SEQ_TRANSPORT_FSM_START_PENDING))
    {
        return 0U;
    }

    if (fsm->rec_count_in_remaining_steps > 0U)
    {
        fsm->rec_count_in_remaining_steps--;
    }

    if (fsm->rec_count_in_remaining_steps == 0U)
    {
        fsm->state = SEQ_TRANSPORT_FSM_RUNNING;
        return 1U;
    }

    return 0U;
}

uint8_t seq_transport_fsm_is_stopped(const seq_transport_fsm_t *fsm)
{
    return ((fsm != 0) && (fsm->state == SEQ_TRANSPORT_FSM_STOPPED)) ? 1U : 0U;
}

uint8_t seq_transport_fsm_is_start_pending(const seq_transport_fsm_t *fsm)
{
    return ((fsm != 0) && (fsm->state == SEQ_TRANSPORT_FSM_START_PENDING)) ? 1U : 0U;
}

uint8_t seq_transport_fsm_is_running(const seq_transport_fsm_t *fsm)
{
    return ((fsm != 0) && (fsm->state == SEQ_TRANSPORT_FSM_RUNNING)) ? 1U : 0U;
}

uint32_t seq_transport_fsm_get_rec_count_in_remaining_steps(const seq_transport_fsm_t *fsm)
{
    return (fsm != 0) ? fsm->rec_count_in_remaining_steps : 0U;
}

uint8_t seq_transport_fsm_allow_advance(const seq_transport_fsm_t *fsm)
{
    /* Query surface: step advancement is only allowed when transport state says running. */
    return seq_transport_fsm_is_running(fsm);
}

uint8_t seq_transport_fsm_allow_schedule_play(const seq_transport_fsm_t *fsm)
{
    /* Query surface: scheduling follows transport running state only. */
    return seq_transport_fsm_is_running(fsm);
}

uint8_t seq_transport_fsm_allow_live_rec(const seq_transport_fsm_t *fsm, uint8_t rec_armed)
{
    /* Query surface: live-rec is gated by transport state plus armed/count-in policy. */
    return ((rec_armed != 0U)
            && (seq_transport_fsm_is_running(fsm) != 0U)
            && (seq_transport_fsm_get_rec_count_in_remaining_steps(fsm) == 0U)) ? 1U : 0U;
}
