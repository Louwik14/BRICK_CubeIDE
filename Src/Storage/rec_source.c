#include "Storage/rec_source.h"

#include <string.h>

#include "Platform/intercore_cache.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_page_lease_control.h"
#include "Sampler/sample_stream_manager.h"
#include "Storage/sd_access_gate.h"
#include "ff.h"

typedef enum
{
    REC_SOURCE_SLOT_FREE = 0,
    REC_SOURCE_SLOT_BUILDING,
    REC_SOURCE_SLOT_CURRENT,
    REC_SOURCE_SLOT_RETIRED
} rec_source_slot_state_t;

typedef struct
{
    sample_audio_key_t key;
    uint32_t frame_count;
    uint32_t registration_epoch;
    rec_source_slot_state_t state;
    uint8_t release_requested;
    char temporary_path[REC_SOURCE_PATH_MAX];
    char final_path[REC_SOURCE_PATH_MAX];
} rec_source_slot_t;

static rec_source_slot_t g_rec_source_slots[REC_SOURCE_SLOT_COUNT];
static uint8_t g_rec_source_current_slot = UINT8_MAX;
static uint8_t g_rec_source_building_slot = UINT8_MAX;
static uint32_t g_rec_source_next_generation;
static uint32_t g_rec_source_publication_serial;

void rec_source_init(void)
{
    memset(g_rec_source_slots, 0, sizeof(g_rec_source_slots));
    (void)strcpy(g_rec_source_slots[0].temporary_path,
                 "0:/REC/REC_WORK_A.REC");
    (void)strcpy(g_rec_source_slots[0].final_path,
                 "0:/REC/REC_WORK_A.WAV");
    (void)strcpy(g_rec_source_slots[1].temporary_path,
                 "0:/REC/REC_WORK_B.REC");
    (void)strcpy(g_rec_source_slots[1].final_path,
                 "0:/REC/REC_WORK_B.WAV");
    g_rec_source_current_slot = UINT8_MAX;
    g_rec_source_building_slot = UINT8_MAX;
    g_rec_source_next_generation = 0U;
    g_rec_source_publication_serial = 0U;
    memset(&g_rec_source_projection, 0, sizeof(g_rec_source_projection));
    intercore_cache_publish(&g_rec_source_projection,
                            sizeof(g_rec_source_projection));
}

void rec_source_service(void)
{
    for (uint8_t slot = 0U; slot < REC_SOURCE_SLOT_COUNT; ++slot)
    {
        rec_source_slot_t *const candidate = &g_rec_source_slots[slot];
        if ((candidate->state != REC_SOURCE_SLOT_RETIRED)
                || (sample_page_lease_control_references_key(candidate->key) != 0U))
            continue;
        if (candidate->release_requested == 0U)
        {
            sample_stream_manager_release_key(candidate->key);
            candidate->release_requested = 1U;
            continue;
        }
        if (sample_stream_manager_key_busy(candidate->key) != 0U) continue;
        if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_RECORDER) == 0U)
            continue;
        const FRESULT temporary_result = f_unlink(candidate->temporary_path);
        const FRESULT final_result = f_unlink(candidate->final_path);
        sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
        if (((temporary_result != FR_OK) && (temporary_result != FR_NO_FILE))
                || ((final_result != FR_OK) && (final_result != FR_NO_FILE)))
            continue;
        sample_page_cache_clear_key(candidate->key);
        candidate->state = REC_SOURCE_SLOT_FREE;
        candidate->frame_count = 0U;
        candidate->registration_epoch = 0U;
        candidate->release_requested = 0U;
        candidate->key = sample_audio_key_rec(slot, 0U);
    }
}

uint8_t rec_source_begin_build(const char **temporary_path,
                               const char **final_path,
                               sample_audio_key_t *key)
{
    if ((temporary_path == NULL) || (final_path == NULL) || (key == NULL)
            || (g_rec_source_building_slot != UINT8_MAX)) return 0U;
    for (uint8_t slot = 0U; slot < REC_SOURCE_SLOT_COUNT; ++slot)
    {
        rec_source_slot_t *const candidate = &g_rec_source_slots[slot];
        if (candidate->state != REC_SOURCE_SLOT_FREE) continue;
        const FRESULT old_final = f_unlink(candidate->final_path);
        if ((old_final != FR_OK) && (old_final != FR_NO_FILE)) return 0U;
        uint32_t generation = ++g_rec_source_next_generation;
        if (generation == 0U) generation = ++g_rec_source_next_generation;
        candidate->key = sample_audio_key_rec(slot, generation);
        candidate->frame_count = 0U;
        candidate->registration_epoch = 0U;
        candidate->release_requested = 0U;
        candidate->state = REC_SOURCE_SLOT_BUILDING;
        g_rec_source_building_slot = slot;
        *temporary_path = candidate->temporary_path;
        *final_path = candidate->final_path;
        *key = candidate->key;
        return 1U;
    }
    return 0U;
}

sample_audio_key_t rec_source_building_key(void)
{
    return (g_rec_source_building_slot < REC_SOURCE_SLOT_COUNT)
        ? g_rec_source_slots[g_rec_source_building_slot].key
        : sample_audio_key_rec(0U, 0U);
}

const char *rec_source_building_temporary_path(void)
{
    return (g_rec_source_building_slot < REC_SOURCE_SLOT_COUNT)
        ? g_rec_source_slots[g_rec_source_building_slot].temporary_path : NULL;
}

const char *rec_source_building_final_path(void)
{
    return (g_rec_source_building_slot < REC_SOURCE_SLOT_COUNT)
        ? g_rec_source_slots[g_rec_source_building_slot].final_path : NULL;
}

uint8_t rec_source_building_active(void)
{
    return (g_rec_source_building_slot < REC_SOURCE_SLOT_COUNT) ? 1U : 0U;
}

uint8_t rec_source_publish_building(uint32_t frame_count,
                                    uint32_t registration_epoch)
{
    if ((g_rec_source_building_slot >= REC_SOURCE_SLOT_COUNT)
            || (frame_count == 0U) || (registration_epoch == 0U)) return 0U;
    const uint8_t slot = g_rec_source_building_slot;
    rec_source_slot_t *const building = &g_rec_source_slots[slot];
    if (building->state != REC_SOURCE_SLOT_BUILDING) return 0U;

    const uint32_t inactive = g_rec_source_projection.active_snapshot ^ 1U;
    rec_source_snapshot_t *const next =
        &g_rec_source_projection.snapshots[inactive];
    memset(next, 0, sizeof(*next));
    next->key = building->key;
    next->frame_count = frame_count;
    next->sample_rate = 48000U;
    next->registration_epoch = registration_epoch;
    next->publication_serial = ++g_rec_source_publication_serial;
    next->ready = 1U;
    intercore_cache_publish(next, sizeof(*next));
    g_rec_source_projection.active_snapshot = inactive;
    intercore_cache_publish((const void *)&g_rec_source_projection.active_snapshot,
                            sizeof(g_rec_source_projection.active_snapshot));

    if (g_rec_source_current_slot < REC_SOURCE_SLOT_COUNT)
        g_rec_source_slots[g_rec_source_current_slot].state = REC_SOURCE_SLOT_RETIRED;
    building->frame_count = frame_count;
    building->registration_epoch = registration_epoch;
    building->release_requested = 0U;
    building->state = REC_SOURCE_SLOT_CURRENT;
    g_rec_source_current_slot = slot;
    g_rec_source_building_slot = UINT8_MAX;
    return 1U;
}

void rec_source_abort_building(void)
{
    if (g_rec_source_building_slot >= REC_SOURCE_SLOT_COUNT) return;
    rec_source_slot_t *const building =
        &g_rec_source_slots[g_rec_source_building_slot];
    sample_page_cache_clear_key(building->key);
    building->state = REC_SOURCE_SLOT_FREE;
    building->frame_count = 0U;
    building->registration_epoch = 0U;
    building->release_requested = 0U;
    g_rec_source_building_slot = UINT8_MAX;
}

uint8_t rec_source_current_snapshot(rec_source_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) return 0U;
    const uint32_t active = g_rec_source_projection.active_snapshot;
    *out_snapshot = g_rec_source_projection.snapshots[active];
    return out_snapshot->ready;
}
