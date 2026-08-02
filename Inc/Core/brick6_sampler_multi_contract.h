/**
 * @file brick6_sampler_multi_contract.h
 * @brief Internal contract for the runtime identity and lifetime of Multi voices.
 *
 * This header freezes the non-persistent identity used by the future
 * per-voice Multi DSP path. It does not allocate or render anything.
 */
#pragma once

#include <stdint.h>

#define BRICK6_SAMPLER_MULTI_MAX_VOICES (8U)
#define BRICK6_SAMPLER_MULTI_VOICE_INDEX_INVALID UINT8_MAX

typedef enum
{
    /* No owner; the handle is invalid and the slot may be allocated. */
    BRICK6_SAMPLER_MULTI_VOICE_FREE = 0,
    /* Gate is held. Reader and VCA may both still be active. */
    BRICK6_SAMPLER_MULTI_VOICE_HELD,
    /* Gate is off, or the reader ended while the VCA tail is still active. */
    BRICK6_SAMPLER_MULTI_VOICE_RELEASE,
    /* Forced or terminal teardown is pending; the slot is not reusable yet. */
    BRICK6_SAMPLER_MULTI_VOICE_TERMINAL
} brick6_sampler_multi_voice_state_t;

/**
 * Runtime-only identity of one Multi note occurrence.
 *
 * voice_index addresses g_sampler_multi_voice[] and generation is the
 * non-zero trigger_order captured at allocation. The pair is invalidated
 * before a slot is reused. This value is never persisted in a step, pattern,
 * project, patch or kit.
 */
typedef struct
{
    uint8_t voice_index;
    uint8_t reserved[3U];
    uint32_t generation;
} brick6_sampler_multi_voice_handle_t;

/*
 * Lifetime contract:
 *
 *   FREE -> HELD       Note On reserves and publishes a new handle.
 *   HELD -> RELEASE    Note Off closes only that handle's gate.
 *   HELD -> RELEASE    A natural EOF may release reader resources while the
 *                      VCA tail keeps the handle alive.
 *   *    -> TERMINAL   Steal, panic, transport stop or instrument change
 *                      closes the gate and starts bounded teardown.
 *   RELEASE/TERMINAL -> FREE
 *                      Only after reader/pages/owner and DSP state are done.
 *
 * A voice contributes audio only while source_active && vca_active. If the
 * VCA becomes idle first, the reader and its pages are stopped immediately.
 * If the reader reaches EOF first, only the DSP/VCA tail remains. A slot and
 * handle are never reused while either side of that lifetime is still active.
 */
