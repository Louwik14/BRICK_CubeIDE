#include "Audio/mixer_path_diag.h"

#if defined(BRICK6_MIXER_PATH_DIAG)

#include <math.h>
#include <string.h>

#include "Audio/audio_float.h"
#include "Core/entity_topology.h"

enum
{
    MIXER_PATH_DIAG_STAGE_A = 1U,
    MIXER_PATH_DIAG_STAGE_B = 2U,
    MIXER_PATH_DIAG_STAGE_C = 4U,
    MIXER_PATH_DIAG_STAGE_D = 8U,
    MIXER_PATH_DIAG_STAGE_ALL = 15U
};

volatile mixer_path_diag_snapshot_t __attribute__((used, externally_visible))
    mixer_path_diag_snapshots[MIXER_PATH_DIAG_CAPACITY];
volatile uint32_t mixer_path_diag_write_seq
    __attribute__((used, externally_visible));
volatile uint32_t mixer_path_diag_block_seq
    __attribute__((used, externally_visible));
volatile uint32_t mixer_path_diag_captured_blocks
    __attribute__((used, externally_visible));
volatile uint8_t mixer_path_diag_enabled
    __attribute__((used, externally_visible));
volatile uint8_t mixer_path_diag_remaining_blocks
    __attribute__((used, externally_visible));
volatile uint8_t mixer_path_diag_entity_id
    __attribute__((used, externally_visible));
static volatile uint8_t g_mixer_path_diag_block_active;

static void mixer_path_diag_measure(float value, volatile float *peak,
                                    volatile uint8_t *invalid_flags)
{
    if (isnan(value) != 0)
    {
        *invalid_flags |= 1U;
        return;
    }
    if (isinf(value) != 0)
    {
        *invalid_flags |= 2U;
        return;
    }
    const float magnitude = fabsf(value);
    if (magnitude > 8.0f)
        *invalid_flags |= 4U;
    if (magnitude > *peak)
        *peak = magnitude;
}

static void mixer_path_diag_measure_stereo(
    volatile mixer_path_diag_snapshot_t *snapshot,
    const float *left, const float *right, uint32_t frames,
    volatile float *peak_l, volatile float *peak_r)
{
    if ((left == NULL) || (frames == 0U))
        return;
    for (uint32_t i = 0U; i < frames; ++i)
    {
        mixer_path_diag_measure(left[i], peak_l, &snapshot->invalid_flags);
        mixer_path_diag_measure((right != NULL) ? right[i] : left[i],
                                peak_r, &snapshot->invalid_flags);
    }
}

void __attribute__((used, noinline, externally_visible)) mixer_path_diag_reset(void)
{
    memset((void *)mixer_path_diag_snapshots, 0,
           sizeof(mixer_path_diag_snapshots));
    mixer_path_diag_write_seq = 0U;
    mixer_path_diag_block_seq = 0U;
    mixer_path_diag_captured_blocks = 0U;
    mixer_path_diag_enabled = 0U;
    mixer_path_diag_remaining_blocks = 0U;
    mixer_path_diag_entity_id = 0xFFU;
    g_mixer_path_diag_block_active = 0U;
}

void mixer_path_diag_block_begin(uint32_t frames)
{
    g_mixer_path_diag_block_active = 0U;
    if ((frames == 0U) || (frames > AUDIO_BLOCK_SIZE)
            || (mixer_path_diag_enabled == 0U)
            || (mixer_path_diag_remaining_blocks == 0U))
        return;
    if (mixer_path_diag_remaining_blocks > MIXER_PATH_DIAG_MAX_BLOCKS)
        mixer_path_diag_remaining_blocks = MIXER_PATH_DIAG_MAX_BLOCKS;
    ++mixer_path_diag_block_seq;
    g_mixer_path_diag_block_active = 1U;
}

uint8_t mixer_path_diag_begin(uint8_t entity_id, uint8_t mix_track_id,
                              uint8_t source_flags, uint8_t track_flags,
                              const float *left, const float *right,
                              uint32_t frames)
{
    if ((g_mixer_path_diag_block_active == 0U)
            || (entity_id != mixer_path_diag_entity_id)
            || (entity_id >= BRICK_ENTITY_TOP_LEVEL_COUNT)
            || (left == NULL) || (frames == 0U)
            || (frames > AUDIO_BLOCK_SIZE))
        return MIXER_PATH_DIAG_NO_SLOT;
    const uint8_t index = (uint8_t)(mixer_path_diag_write_seq
        & (MIXER_PATH_DIAG_CAPACITY - 1U));
    volatile mixer_path_diag_snapshot_t *const snapshot =
        &mixer_path_diag_snapshots[index];
    memset((void *)snapshot, 0, sizeof(*snapshot));
    snapshot->block_seq = mixer_path_diag_block_seq;
    snapshot->entity_id = entity_id;
    snapshot->mix_track_id = mix_track_id;
    snapshot->source_flags = source_flags;
    snapshot->track_flags = track_flags;
    snapshot->frames = (uint16_t)frames;
    mixer_path_diag_measure_stereo(snapshot, left, right, frames,
                                   &snapshot->peak_a_l,
                                   &snapshot->peak_a_r);
    snapshot->stage_flags = MIXER_PATH_DIAG_STAGE_A;
    return index;
}

void mixer_path_diag_set_coefficients(uint8_t index,
                                      float gain_first, float gain_last,
                                      float pan_l_first, float pan_l_last,
                                      float pan_r_first, float pan_r_last,
                                      float vca_first, float vca_last,
                                      float mute_first, float mute_last)
{
    if (index >= MIXER_PATH_DIAG_CAPACITY)
        return;
    volatile mixer_path_diag_snapshot_t *const snapshot =
        &mixer_path_diag_snapshots[index];
    snapshot->gain_first = gain_first;
    snapshot->gain_last = gain_last;
    snapshot->pan_l_first = pan_l_first;
    snapshot->pan_l_last = pan_l_last;
    snapshot->pan_r_first = pan_r_first;
    snapshot->pan_r_last = pan_r_last;
    snapshot->vca_first = vca_first;
    snapshot->vca_last = vca_last;
    snapshot->mute_gain_first = mute_first;
    snapshot->mute_gain_last = mute_last;
}

void mixer_path_diag_capture_bc_sample(uint8_t index,
                                       float b_l, float b_r,
                                       float c_l, float c_r)
{
    if (index >= MIXER_PATH_DIAG_CAPACITY)
        return;
    volatile mixer_path_diag_snapshot_t *const snapshot =
        &mixer_path_diag_snapshots[index];
    mixer_path_diag_measure(b_l, &snapshot->peak_b_l, &snapshot->invalid_flags);
    mixer_path_diag_measure(b_r, &snapshot->peak_b_r, &snapshot->invalid_flags);
    mixer_path_diag_measure(c_l, &snapshot->peak_c_l, &snapshot->invalid_flags);
    mixer_path_diag_measure(c_r, &snapshot->peak_c_r, &snapshot->invalid_flags);
    snapshot->stage_flags |= MIXER_PATH_DIAG_STAGE_B | MIXER_PATH_DIAG_STAGE_C;
}

void mixer_path_diag_capture_bc(uint8_t index,
                                const float *left, const float *right,
                                uint32_t frames, float contribution_gain)
{
    if ((index >= MIXER_PATH_DIAG_CAPACITY) || (left == NULL)
            || (right == NULL) || (frames == 0U))
        return;
    for (uint32_t i = 0U; i < frames; ++i)
    {
        mixer_path_diag_capture_bc_sample(index, left[i], right[i],
                                          left[i] * contribution_gain,
                                          right[i] * contribution_gain);
    }
}

void mixer_path_diag_capture_d(uint8_t index,
                               const float *left, const float *right,
                               uint32_t frames)
{
    if (index >= MIXER_PATH_DIAG_CAPACITY)
        return;
    volatile mixer_path_diag_snapshot_t *const snapshot =
        &mixer_path_diag_snapshots[index];
    mixer_path_diag_measure_stereo(snapshot, left, right, frames,
                                   &snapshot->peak_d_l,
                                   &snapshot->peak_d_r);
    snapshot->stage_flags |= MIXER_PATH_DIAG_STAGE_D;
}

void mixer_path_diag_commit(uint8_t index)
{
    if (index >= MIXER_PATH_DIAG_CAPACITY)
        return;
    volatile mixer_path_diag_snapshot_t *const snapshot =
        &mixer_path_diag_snapshots[index];
    if ((snapshot->entity_id != mixer_path_diag_entity_id)
            || (snapshot->frames == 0U)
            || (snapshot->stage_flags != MIXER_PATH_DIAG_STAGE_ALL))
    {
        snapshot->seq = 0U;
        return;
    }
    const uint32_t sequence = mixer_path_diag_write_seq + 1U;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    snapshot->seq = sequence;
    mixer_path_diag_write_seq = sequence;
    ++mixer_path_diag_captured_blocks;
    --mixer_path_diag_remaining_blocks;
    if (mixer_path_diag_remaining_blocks == 0U)
        mixer_path_diag_enabled = 0U;
    g_mixer_path_diag_block_active = 0U;
}

#endif
