#include "Storage/rec_source.h"

#include <string.h>
#include <stdio.h>
#include <stddef.h>

#include "Platform/intercore_cache.h"
#include "Platform/memory_layout.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_page_cache_port.h"
#include "Sampler/sample_page_lease_control.h"
#include "Sampler/sample_stream_manager.h"
#include "Storage/sd_access_gate.h"
#include "Storage/undo_v2.h"
#include "Storage/wav_parser.h"
#include "ff.h"

#define REC_SOURCE_INVALID_SLOT UINT8_MAX
#define REC_SOURCE_PROMOTION_MAGIC 0x5250524AUL
#define REC_SOURCE_PROMOTION_JOURNAL_0 REC_SOURCE_DIRECTORY "/REC_PROMOTE.0"
#define REC_SOURCE_PROMOTION_JOURNAL_1 REC_SOURCE_DIRECTORY "/REC_PROMOTE.1"

typedef enum
{
    REC_SOURCE_PROMOTION_INTENT = 1,
    REC_SOURCE_PROMOTION_RENAMED,
    REC_SOURCE_PROMOTION_COMMITTED
} rec_source_promotion_phase_t;

typedef struct
{
    uint32_t magic;
    uint32_t sequence;
    uint32_t generation;
    uint32_t media_epoch;
    uint32_t phase;
    char old_path[REC_SOURCE_PATH_MAX];
    char new_path[REC_SOURCE_PATH_MAX];
    uint32_t checksum;
} rec_source_promotion_journal_t;

typedef struct
{
    sample_audio_key_t key;
    uint32_t frame_count;
    uint32_t registration_epoch;
    uint32_t media_epoch;
    uint64_t reserved_file_bytes;
    uint64_t valid_file_bytes;
    sample_stream_physical_extent_t extents[RECORDER_FILE_RESERVATION_MAX_EXTENTS];
    uint16_t extent_count;
    uint16_t sector_size;
    rec_source_state_t state;
    rec_source_ownership_t ownership;
    uint8_t release_requested;
    rec_source_waveform_summary_t waveform;
    char temporary_path[REC_SOURCE_PATH_MAX];
    char path[REC_SOURCE_PATH_MAX];
} rec_source_generation_t;

STORAGE_STATE_SDRAM static rec_source_generation_t
    g_rec_source_generations[REC_SOURCE_SLOT_COUNT];
static uint8_t g_rec_source_current_slot = REC_SOURCE_INVALID_SLOT;
static uint8_t g_rec_source_building_slot = REC_SOURCE_INVALID_SLOT;
static uint32_t g_rec_source_next_generation;
static uint32_t g_rec_source_publication_serial;
static uint8_t g_rec_source_recovery_pending;

static uint8_t copy_path(char *dst, const char *src)
{
    if ((dst == NULL) || (src == NULL) || (src[0] == '\0')) return 0U;
    for (uint32_t i = 0U; i < REC_SOURCE_PATH_MAX; ++i)
    {
        dst[i] = src[i];
        if (src[i] == '\0') return 1U;
    }
    dst[0] = '\0';
    return 0U;
}

uint8_t rec_source_ensure_directory(void)
{
    FRESULT result = f_mkdir(REC_SOURCE_PARENT_DIRECTORY);
    if ((result != FR_OK) && (result != FR_EXIST)) return 0U;
    result = f_mkdir(REC_SOURCE_DIRECTORY);
    return (uint8_t)((result == FR_OK) || (result == FR_EXIST));
}

static int8_t find_generation(uint32_t generation)
{
    if (generation == 0U) return -1;
    for (uint8_t slot = 0U; slot < REC_SOURCE_SLOT_COUNT; ++slot)
        if ((g_rec_source_generations[slot].state != REC_SOURCE_STATE_FREE_PREPARED)
                && (g_rec_source_generations[slot].key.generation == generation))
            return (int8_t)slot;
    return -1;
}

static uint32_t allocate_generation(void)
{
    do
    {
        g_rec_source_next_generation++;
        if (g_rec_source_next_generation == 0U) g_rec_source_next_generation++;
    }
    while (find_generation(g_rec_source_next_generation) >= 0);
    return g_rec_source_next_generation;
}

static void publish_projection(const rec_source_generation_t *target)
{
    const uint32_t inactive = g_rec_source_projection.active_snapshot ^ 1U;
    rec_source_snapshot_t *const next = &g_rec_source_projection.snapshots[inactive];
    memset(next, 0, sizeof(*next));
    next->publication_serial = ++g_rec_source_publication_serial;
    if (target != NULL)
    {
        next->key = target->key;
        next->frame_count = target->frame_count;
        next->sample_rate = 48000U;
        next->registration_epoch = target->registration_epoch;
        next->ready = 1U;
    }
    intercore_cache_publish(next, sizeof(*next));
    g_rec_source_projection.active_snapshot = inactive;
    intercore_cache_publish((const void *)&g_rec_source_projection.active_snapshot,
                            sizeof(g_rec_source_projection.active_snapshot));
}

static uint8_t target_pages_ready(const rec_source_generation_t *target)
{
    uint32_t pages = (target->frame_count + SAMPLE_PAGE_FRAMES - 1U)
        / SAMPLE_PAGE_FRAMES;
    if (pages > SAMPLE_PAGE_MIN_READY_PAGES) pages = SAMPLE_PAGE_MIN_READY_PAGES;
    if (pages == 0U) return 0U;
    (void)sample_page_cache_reserve_start_pages_key(target->key, 0U, pages);
    for (uint32_t page = 0U; page < pages; ++page)
        if (sample_page_cache_get_page_state_key(target->key, page)
                != SAMPLE_PAGE_READY) return 0U;
    return 1U;
}

uint8_t rec_source_switch_current(uint32_t generation)
{
    const int8_t target_index = find_generation(generation);
    if ((generation != 0U) && (target_index < 0)) return 0U;
    if ((target_index >= 0)
            && (target_pages_ready(&g_rec_source_generations[(uint8_t)target_index]) == 0U))
        return 0U;
    if ((g_rec_source_current_slot < REC_SOURCE_SLOT_COUNT)
            && ((target_index < 0)
                || (g_rec_source_current_slot != (uint8_t)target_index)))
        g_rec_source_generations[g_rec_source_current_slot].state = REC_SOURCE_STATE_UNDO;
    if (target_index < 0)
    {
        g_rec_source_current_slot = REC_SOURCE_INVALID_SLOT;
        publish_projection(NULL);
        return 1U;
    }
    rec_source_generation_t *const target =
        &g_rec_source_generations[(uint8_t)target_index];
    target->state = REC_SOURCE_STATE_CURRENT;
    target->release_requested = 0U;
    g_rec_source_current_slot = (uint8_t)target_index;
    publish_projection(target);
    return 1U;
}

static const char *promotion_journal_path(uint32_t sequence)
{
    return ((sequence & 1U) != 0U) ? REC_SOURCE_PROMOTION_JOURNAL_1
                                   : REC_SOURCE_PROMOTION_JOURNAL_0;
}

static uint32_t promotion_journal_checksum(
    const rec_source_promotion_journal_t *journal)
{
    const uint8_t *const bytes = (const uint8_t *)journal;
    uint32_t hash = 2166136261UL;
    for (uint32_t i = 0U; i < offsetof(rec_source_promotion_journal_t, checksum); ++i)
        hash = (hash ^ bytes[i]) * 16777619UL;
    return hash;
}

static uint8_t promotion_journal_valid(
    const rec_source_promotion_journal_t *journal)
{
    return (uint8_t)((journal->magic == REC_SOURCE_PROMOTION_MAGIC)
        && (journal->sequence != 0U)
        && (journal->phase >= REC_SOURCE_PROMOTION_INTENT)
        && (journal->phase <= REC_SOURCE_PROMOTION_COMMITTED)
        && (journal->old_path[0] != '\0') && (journal->new_path[0] != '\0')
        && (journal->old_path[REC_SOURCE_PATH_MAX - 1U] == '\0')
        && (journal->new_path[REC_SOURCE_PATH_MAX - 1U] == '\0')
        && (journal->checksum == promotion_journal_checksum(journal)));
}

static uint8_t read_promotion_journal(const char *path,
                                      rec_source_promotion_journal_t *journal)
{
    FIL file;
    UINT read = 0U;
    memset(journal, 0, sizeof(*journal));
    if (f_open(&file, path, FA_READ) != FR_OK) return 0U;
    const uint8_t ok = (uint8_t)((f_read(&file, journal, sizeof(*journal), &read)
            == FR_OK) && (read == sizeof(*journal)));
    (void)f_close(&file);
    return (uint8_t)((ok != 0U) && (promotion_journal_valid(journal) != 0U));
}

static uint8_t load_promotion_journal(rec_source_promotion_journal_t *journal)
{
    rec_source_promotion_journal_t a, b;
    const uint8_t a_valid = read_promotion_journal(
        REC_SOURCE_PROMOTION_JOURNAL_0, &a);
    const uint8_t b_valid = read_promotion_journal(
        REC_SOURCE_PROMOTION_JOURNAL_1, &b);
    if ((a_valid == 0U) && (b_valid == 0U)) return 0U;
    *journal = ((b_valid != 0U)
            && ((a_valid == 0U) || (b.sequence > a.sequence))) ? b : a;
    return 1U;
}

static void clear_promotion_journals(void)
{
    (void)f_unlink(REC_SOURCE_PROMOTION_JOURNAL_0);
    (void)f_unlink(REC_SOURCE_PROMOTION_JOURNAL_1);
}

static uint8_t promotion_journal_files_exist(void)
{
    FILINFO info;
    return (uint8_t)((f_stat(REC_SOURCE_PROMOTION_JOURNAL_0, &info) == FR_OK)
        || (f_stat(REC_SOURCE_PROMOTION_JOURNAL_1, &info) == FR_OK));
}

static uint8_t write_promotion_journal(rec_source_promotion_journal_t *journal,
                                       rec_source_promotion_phase_t phase)
{
    FIL file;
    UINT written = 0U;
    journal->sequence++;
    if (journal->sequence == 0U) journal->sequence++;
    journal->phase = (uint32_t)phase;
    journal->checksum = promotion_journal_checksum(journal);
    if (f_open(&file, promotion_journal_path(journal->sequence),
               FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return 0U;
    const FRESULT write_result = f_write(&file, journal, sizeof(*journal), &written);
    const FRESULT sync_result = (write_result == FR_OK) ? f_sync(&file) : write_result;
    const FRESULT close_result = f_close(&file);
    return (uint8_t)((write_result == FR_OK) && (written == sizeof(*journal))
                     && (sync_result == FR_OK) && (close_result == FR_OK));
}

static void recover_promotion(void)
{
    rec_source_promotion_journal_t journal;
    if (load_promotion_journal(&journal) == 0U)
    {
        /* Invalid journal files are ambiguous and are deliberately retained. */
        g_rec_source_recovery_pending = 0U;
        return;
    }
    FILINFO old_info, new_info;
    const uint8_t old_exists = (f_stat(journal.old_path, &old_info) == FR_OK);
    const uint8_t new_exists = (f_stat(journal.new_path, &new_info) == FR_OK);
    if ((old_exists != 0U) && (new_exists == 0U))
        clear_promotion_journals(); /* rollback truth */
    else if ((old_exists == 0U) && (new_exists != 0U))
    {
        int8_t recovered_slot = find_generation(journal.generation);
        if (recovered_slot < 0)
        {
            for (uint8_t slot = 0U; slot < REC_SOURCE_SLOT_COUNT; ++slot)
                if (g_rec_source_generations[slot].state
                        == REC_SOURCE_STATE_FREE_PREPARED)
                { recovered_slot = (int8_t)slot; break; }
            if (recovered_slot < 0) return;
            FIL wav;
            wav_info_t info;
            memset(&wav, 0, sizeof(wav));
            if ((f_open(&wav, journal.new_path, FA_READ) != FR_OK)
                    || (wav_parser_parse_info(&wav, &info) == 0))
            { if (wav.obj.fs != NULL) (void)f_close(&wav); return; }
            const uint32_t frames = (info.block_align != 0U)
                ? (info.data_size / info.block_align) : 0U;
            rec_source_generation_t *const recovered =
                &g_rec_source_generations[(uint8_t)recovered_slot];
            recovered->key = sample_audio_key_rec((uint8_t)recovered_slot,
                                                  journal.generation);
            if ((frames == 0U) || (sample_page_cache_port_register_file(
                    recovered->key, journal.new_path, &info, frames,
                    info.data_offset, &wav) == 0U))
            { (void)f_close(&wav); return; }
            (void)f_close(&wav);
            sample_page_stream_info_t stream;
            if ((sample_page_cache_get_stream_info_key(recovered->key, &stream) == 0U)
                    || (sample_page_cache_get_registration_epoch_key(
                        recovered->key, &recovered->registration_epoch) == 0U)
                    || (copy_path(recovered->path, journal.new_path) == 0U)) return;
            recovered->frame_count = frames;
            recovered->media_epoch = journal.media_epoch;
            recovered->valid_file_bytes = stream.stream_safe.file_size;
            recovered->reserved_file_bytes = stream.stream_safe.file_size;
            recovered->sector_size = stream.stream_safe.sector_size;
            recovered->extent_count = stream.stream_safe.physical_map.extent_count;
            for (uint16_t i = 0U; i < recovered->extent_count; ++i)
                if (sample_stream_physical_map_get_extent(
                        &stream.stream_safe.physical_map, i,
                        &recovered->extents[i]) == 0U) return;
            recovered->ownership = REC_SOURCE_OWNERSHIP_PERSISTENT;
            recovered->state = REC_SOURCE_STATE_UNDO;
            if (g_rec_source_next_generation < journal.generation)
                g_rec_source_next_generation = journal.generation;
        }
        else
        {
            rec_source_generation_t *const recovered =
                &g_rec_source_generations[(uint8_t)recovered_slot];
            if ((strcmp(recovered->path, journal.new_path) != 0)
                    && ((sample_page_cache_update_stream_path_key(
                            recovered->key, journal.new_path) == 0U)
                        || (copy_path(recovered->path, journal.new_path) == 0U)))
                return;
            recovered->ownership = REC_SOURCE_OWNERSHIP_PERSISTENT;
        }
        if (rec_source_switch_current(journal.generation) == 0U) return;
        undo_v2_expire_audio();
        (void)write_promotion_journal(&journal,
                                      REC_SOURCE_PROMOTION_COMMITTED);
        clear_promotion_journals();
        g_rec_source_recovery_pending = 0U;
        return;
    }
    /* both/neither is deliberately left for manual recovery. */
    g_rec_source_recovery_pending = 0U;
}

void rec_source_init(void)
{
    memset(g_rec_source_generations, 0, sizeof(g_rec_source_generations));
    for (uint8_t slot = 0U; slot < REC_SOURCE_SLOT_COUNT; ++slot)
    {
        (void)snprintf(g_rec_source_generations[slot].temporary_path,
                       REC_SOURCE_PATH_MAX,
                       REC_SOURCE_DIRECTORY "/REC_GEN_%u.REC", slot);
        (void)snprintf(g_rec_source_generations[slot].path,
                       REC_SOURCE_PATH_MAX,
                       REC_SOURCE_DIRECTORY "/REC_GEN_%u.WAV", slot);
    }
    g_rec_source_current_slot = REC_SOURCE_INVALID_SLOT;
    g_rec_source_building_slot = REC_SOURCE_INVALID_SLOT;
    g_rec_source_next_generation = 0U;
    g_rec_source_publication_serial = 0U;
    g_rec_source_recovery_pending = 1U;
    memset(&g_rec_source_projection, 0, sizeof(g_rec_source_projection));
    intercore_cache_publish(&g_rec_source_projection, sizeof(g_rec_source_projection));
}

void rec_source_service(void)
{
    if ((g_rec_source_recovery_pending != 0U)
            && (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_RECORDER) != 0U))
    {
        if (sd_access_fs_mount_if_needed() != 0U) recover_promotion();
        sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
    }
    for (uint8_t slot = 0U; slot < REC_SOURCE_SLOT_COUNT; ++slot)
    {
        rec_source_generation_t *const candidate = &g_rec_source_generations[slot];
        if ((candidate->state != REC_SOURCE_STATE_RETIRED)
                || (sample_page_lease_control_references_key(candidate->key) != 0U)) continue;
        if (candidate->release_requested == 0U)
        { sample_stream_manager_release_key(candidate->key);
          candidate->release_requested = 1U; continue; }
        if (sample_stream_manager_key_busy(candidate->key) != 0U) continue;
        if ((candidate->ownership == REC_SOURCE_OWNERSHIP_TEMPORARY)
                && (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_RECORDER) == 0U)) continue;
        FRESULT temp_result = FR_NO_FILE, path_result = FR_NO_FILE;
        if (candidate->ownership == REC_SOURCE_OWNERSHIP_TEMPORARY)
        {
            temp_result = f_unlink(candidate->temporary_path);
            path_result = f_unlink(candidate->path);
            sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
        }
        if (((temp_result != FR_OK) && (temp_result != FR_NO_FILE))
                || ((path_result != FR_OK) && (path_result != FR_NO_FILE))) continue;
        sample_page_cache_clear_key(candidate->key);
        candidate->state = REC_SOURCE_STATE_FREE_PREPARED;
        candidate->frame_count = 0U;
        candidate->registration_epoch = 0U;
        candidate->release_requested = 0U;
        candidate->ownership = REC_SOURCE_OWNERSHIP_TEMPORARY;
        candidate->key = sample_audio_key_rec(slot, 0U);
        (void)snprintf(candidate->temporary_path, REC_SOURCE_PATH_MAX,
                       REC_SOURCE_DIRECTORY "/REC_GEN_%u.REC", slot);
        (void)snprintf(candidate->path, REC_SOURCE_PATH_MAX,
                       REC_SOURCE_DIRECTORY "/REC_GEN_%u.WAV", slot);
    }
}

uint8_t rec_source_begin_build(const char **temporary_path, const char **final_path,
                               sample_audio_key_t *key)
{
    if ((temporary_path == NULL) || (final_path == NULL) || (key == NULL)
            || (g_rec_source_building_slot != REC_SOURCE_INVALID_SLOT)) return 0U;
    for (uint8_t slot = 0U; slot < REC_SOURCE_SLOT_COUNT; ++slot)
    {
        rec_source_generation_t *const candidate = &g_rec_source_generations[slot];
        if (candidate->state != REC_SOURCE_STATE_FREE_PREPARED) continue;
        const FRESULT old_final = f_unlink(candidate->path);
        if ((old_final != FR_OK) && (old_final != FR_NO_FILE)) return 0U;
        const uint32_t generation = allocate_generation();
        candidate->key = sample_audio_key_rec(slot, generation);
        candidate->frame_count = 0U;
        candidate->registration_epoch = 0U;
        candidate->release_requested = 0U;
        candidate->ownership = REC_SOURCE_OWNERSHIP_TEMPORARY;
        candidate->state = REC_SOURCE_STATE_BUILDING;
        g_rec_source_building_slot = slot;
        *temporary_path = candidate->temporary_path;
        *final_path = candidate->path;
        *key = candidate->key;
        return 1U;
    }
    return 0U;
}

sample_audio_key_t rec_source_building_key(void)
{ return (g_rec_source_building_slot < REC_SOURCE_SLOT_COUNT)
    ? g_rec_source_generations[g_rec_source_building_slot].key
    : sample_audio_key_rec(0U, 0U); }
const char *rec_source_building_temporary_path(void)
{ return (g_rec_source_building_slot < REC_SOURCE_SLOT_COUNT)
    ? g_rec_source_generations[g_rec_source_building_slot].temporary_path : NULL; }
const char *rec_source_building_final_path(void)
{ return (g_rec_source_building_slot < REC_SOURCE_SLOT_COUNT)
    ? g_rec_source_generations[g_rec_source_building_slot].path : NULL; }
uint8_t rec_source_building_active(void)
{ return (g_rec_source_building_slot < REC_SOURCE_SLOT_COUNT) ? 1U : 0U; }

uint8_t rec_source_publish_building(uint32_t frame_count, uint32_t registration_epoch,
                                    const audio_recorder_storage_map_copy_t *map)
{
    if ((g_rec_source_building_slot >= REC_SOURCE_SLOT_COUNT) || (frame_count == 0U)
            || (registration_epoch == 0U) || (map == NULL)
            || (map->extent_count > RECORDER_FILE_RESERVATION_MAX_EXTENTS)) return 0U;
    rec_source_generation_t *const building =
        &g_rec_source_generations[g_rec_source_building_slot];
    if (building->state != REC_SOURCE_STATE_BUILDING) return 0U;
    building->frame_count = frame_count;
    building->registration_epoch = registration_epoch;
    building->media_epoch = map->media_epoch;
    building->reserved_file_bytes = map->reserved_file_bytes;
    building->valid_file_bytes = map->valid_file_bytes;
    building->extent_count = map->extent_count;
    building->sector_size = map->sector_size;
    memcpy(building->extents, map->extents,
           (size_t)map->extent_count * sizeof(map->extents[0]));
    const uint32_t before = (g_rec_source_current_slot < REC_SOURCE_SLOT_COUNT)
        ? g_rec_source_generations[g_rec_source_current_slot].key.generation : 0U;
    const uint32_t after = building->key.generation;
    if ((undo_v2_audio_transition_can_commit(before, after) == 0U)
            || (target_pages_ready(building) == 0U)
            || (rec_source_waveform_captured_frames() != frame_count)
            || ((building->waveform.ready == 0U)
                && (rec_source_waveform_finish(&building->waveform) == 0U)))
        return 0U;
    building->waveform.generation = after;
    if (undo_v2_commit_audio_transition(before, after) != UNDO_V2_STATUS_OK) return 0U;
    const uint8_t building_slot = g_rec_source_building_slot;
    if (rec_source_switch_current(after) == 0U) return 0U;
    g_rec_source_generations[building_slot].state = REC_SOURCE_STATE_CURRENT;
    g_rec_source_building_slot = REC_SOURCE_INVALID_SLOT;
    return 1U;
}

void rec_source_abort_building(void)
{
    if (g_rec_source_building_slot >= REC_SOURCE_SLOT_COUNT) return;
    rec_source_waveform_abort();
    rec_source_generation_t *const building =
        &g_rec_source_generations[g_rec_source_building_slot];
    sample_page_cache_clear_key(building->key);
    building->state = REC_SOURCE_STATE_RETIRED;
    building->release_requested = 0U;
    g_rec_source_building_slot = REC_SOURCE_INVALID_SLOT;
}

uint8_t rec_source_clear_current(void)
{
    if (g_rec_source_current_slot >= REC_SOURCE_SLOT_COUNT) return 1U;
    const uint32_t before = g_rec_source_generations[g_rec_source_current_slot].key.generation;
    if (undo_v2_audio_transition_can_commit(before, 0U) == 0U) return 0U;
    if (undo_v2_commit_audio_transition(before, 0U) != UNDO_V2_STATUS_OK) return 0U;
    return rec_source_switch_current(0U);
}

void rec_source_release_history_pair(uint32_t before_generation,
                                     uint32_t after_generation)
{
    const uint32_t pair[2] = { before_generation, after_generation };
    for (uint8_t i = 0U; i < 2U; ++i)
    {
        const int8_t slot = find_generation(pair[i]);
        if ((slot < 0) || ((uint8_t)slot == g_rec_source_current_slot)
                || ((uint8_t)slot == g_rec_source_building_slot)) continue;
        g_rec_source_generations[(uint8_t)slot].state = REC_SOURCE_STATE_RETIRED;
        g_rec_source_generations[(uint8_t)slot].release_requested = 0U;
    }
}

uint8_t rec_source_promote_current(const char *persistent_path)
{
    if ((persistent_path == NULL) || (g_rec_source_current_slot >= REC_SOURCE_SLOT_COUNT))
        return 0U;
    rec_source_generation_t *const current =
        &g_rec_source_generations[g_rec_source_current_slot];
    if (current->ownership == REC_SOURCE_OWNERSHIP_PERSISTENT)
        return (uint8_t)(strcmp(current->path, persistent_path) == 0);
    if (promotion_journal_files_exist() != 0U) return 0U;
    rec_source_promotion_journal_t journal;
    memset(&journal, 0, sizeof(journal));
    journal.magic = REC_SOURCE_PROMOTION_MAGIC;
    journal.generation = current->key.generation;
    journal.media_epoch = current->media_epoch;
    if ((copy_path(journal.old_path, current->path) == 0U)
            || (copy_path(journal.new_path, persistent_path) == 0U)
            || (write_promotion_journal(&journal,
                    REC_SOURCE_PROMOTION_INTENT) == 0U)) return 0U;
    if (f_rename(current->path, persistent_path) != FR_OK)
    {
        FILINFO old_info, new_info;
        if ((f_stat(current->path, &old_info) == FR_OK)
                && (f_stat(persistent_path, &new_info) == FR_NO_FILE))
            clear_promotion_journals();
        else
            g_rec_source_recovery_pending = 1U;
        return 0U;
    }
    if (write_promotion_journal(&journal, REC_SOURCE_PROMOTION_RENAMED) == 0U)
    {
        if (f_rename(persistent_path, current->path) == FR_OK)
            clear_promotion_journals();
        else
            g_rec_source_recovery_pending = 1U;
        return 0U;
    }
    if (sample_page_cache_update_stream_path_key(current->key, persistent_path) == 0U)
    {
        if (f_rename(persistent_path, current->path) == FR_OK)
            clear_promotion_journals();
        else
            g_rec_source_recovery_pending = 1U;
        return 0U;
    }
    if (copy_path(current->path, persistent_path) == 0U) return 0U;
    current->ownership = REC_SOURCE_OWNERSHIP_PERSISTENT;
    undo_v2_expire_audio();
    /* RENAMED remains sufficient for recovery if this final marker fails. */
    (void)write_promotion_journal(&journal, REC_SOURCE_PROMOTION_COMMITTED);
    clear_promotion_journals();
    return 1U;
}

uint8_t rec_source_current_snapshot(rec_source_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) return 0U;
    const uint32_t active = g_rec_source_projection.active_snapshot;
    *out_snapshot = g_rec_source_projection.snapshots[active];
    return out_snapshot->ready;
}

uint8_t rec_source_current_path(const char **out_path, rec_source_snapshot_t *out_snapshot)
{
    if ((out_path == NULL) || (out_snapshot == NULL)
            || (g_rec_source_current_slot >= REC_SOURCE_SLOT_COUNT)) return 0U;
    rec_source_generation_t *const current =
        &g_rec_source_generations[g_rec_source_current_slot];
    if ((current->state != REC_SOURCE_STATE_CURRENT)
            || (rec_source_current_snapshot(out_snapshot) == 0U)
            || (sample_audio_key_equal(&current->key, &out_snapshot->key) == 0U)) return 0U;
    *out_path = current->path;
    return 1U;
}

uint8_t rec_source_current_is_temporary(void)
{
    return (uint8_t)((g_rec_source_current_slot < REC_SOURCE_SLOT_COUNT)
        && (g_rec_source_generations[g_rec_source_current_slot].ownership
            == REC_SOURCE_OWNERSHIP_TEMPORARY));
}

const rec_source_waveform_summary_t *rec_source_current_waveform(void)
{
    if (g_rec_source_current_slot >= REC_SOURCE_SLOT_COUNT) return NULL;
    const rec_source_generation_t *const current =
        &g_rec_source_generations[g_rec_source_current_slot];
    return (current->waveform.ready != 0U) ? &current->waveform : NULL;
}
