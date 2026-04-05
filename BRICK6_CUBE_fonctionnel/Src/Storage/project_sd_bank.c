#include "Storage/project_sd_bank.h"

#include <stdio.h>
#include <string.h>

#include "Storage/memory_layout.h"
#include "Storage/pattern_sd_bank.h"
#include "Storage/sd_access_gate.h"
#include "ff.h"

static FATFS g_project_fs;
static uint8_t g_project_fs_mounted;
static uint8_t g_project_slot_has_data[PROJECT_V1_SLOT_COUNT];
UI_SDRAM static PatternSaveV1 g_project_slot_buffer;
static project_sd_bank_error_t g_project_sd_last_error;

#ifndef PROJECT_PROFILE_ENABLED
#define PROJECT_PROFILE_ENABLED 0
#endif

typedef struct
{
    uint32_t total_ms;
    uint32_t open_read_ms;
    uint32_t read_live_ms;
    uint32_t validate_patterns_ms;
    uint32_t commit_patterns_ms;
    uint32_t open_write_ms;
    uint32_t write_live_ms;
    uint32_t gather_patterns_ms;
    uint32_t write_patterns_ms;
    uint32_t sync_ms;
} project_sd_profile_t;

static void project_sd_set_error(project_sd_bank_error_t err)
{
    g_project_sd_last_error = err;
}

static void project_sd_profile_print(const char *tag, const project_sd_profile_t *p)
{
#if PROJECT_PROFILE_ENABLED
    if ((tag != 0) && (p != 0))
    {
        printf("[PROJECT][PROFILE] %s total=%lums open_r=%lums read_live=%lums val_pat=%lums commit_pat=%lums open_w=%lums write_live=%lums gather_pat=%lums write_pat=%lums sync=%lums\r\n",
               tag,
               (unsigned long)p->total_ms,
               (unsigned long)p->open_read_ms,
               (unsigned long)p->read_live_ms,
               (unsigned long)p->validate_patterns_ms,
               (unsigned long)p->commit_patterns_ms,
               (unsigned long)p->open_write_ms,
               (unsigned long)p->write_live_ms,
               (unsigned long)p->gather_patterns_ms,
               (unsigned long)p->write_patterns_ms,
               (unsigned long)p->sync_ms);
    }
#else
    (void)tag;
    (void)p;
#endif
}

static uint8_t project_sd_slot_is_valid(uint8_t project_slot)
{
    return (project_slot < PROJECT_V1_SLOT_COUNT) ? 1U : 0U;
}

static uint8_t project_sd_header_is_valid(const project_v1_file_header_t *hdr, uint8_t project_slot)
{
    if (hdr == 0)
    {
        return 0U;
    }

    return ((hdr->magic == PROJECT_V1_FILE_MAGIC)
            && (hdr->version == PROJECT_V1_FILE_VERSION)
            && (hdr->header_size == sizeof(project_v1_file_header_t))
            && (hdr->payload_size == sizeof(ProjectSaveV1))
            && (hdr->bank_count == PROJECT_V1_BANK_COUNT)
            && (hdr->pattern_count == PROJECT_V1_PATTERN_COUNT)
            && (hdr->slot_record_size == sizeof(project_v1_slot_record_t))
            && (hdr->pattern_payload_size == sizeof(PatternSaveV1))
            && (hdr->project_slot == (uint32_t)project_slot))
               ? 1U
               : 0U;
}

static void project_sd_mark_slot_validity(uint8_t project_slot, uint8_t has_data)
{
    if (project_slot < PROJECT_V1_SLOT_COUNT)
    {
        g_project_slot_has_data[project_slot] = (has_data != 0U) ? 1U : 0U;
    }
}

static uint32_t project_sd_checksum_accumulate(uint32_t seed, const uint8_t *data, uint32_t len)
{
    uint32_t crc = (seed == 0U) ? 5381UL : seed;
    for (uint32_t i = 0U; i < len; ++i)
    {
        crc = ((crc << 5) + crc) ^ data[i];
    }
    return crc;
}

static uint8_t project_sd_mount_if_needed(void)
{
    if (g_project_fs_mounted != 0U)
    {
        return 1U;
    }

    sd_access_trace_begin("project_f_mount");
    const FRESULT fr = f_mount(&g_project_fs, "0:", 1U);
    sd_access_trace_end("project_f_mount", (int)fr, 0U);
    if (fr != FR_OK)
    {
        return 0U;
    }

    g_project_fs_mounted = 1U;
    return 1U;
}

static uint8_t project_sd_make_slot_path(char *out_path, uint32_t out_size, uint8_t project_slot)
{
    if ((out_path == 0) || (out_size < 22U))
    {
        return 0U;
    }

    const int n = snprintf(out_path, out_size, "0:/PROJECT/PJ%02u.PRJ", (unsigned)project_slot);
    return (n > 0) && ((uint32_t)n < out_size);
}

static uint8_t project_sd_walk_pattern_records(FIL *fp,
                                               uint8_t apply_to_pattern_bank,
                                               ProjectSaveV1 *project_state,
                                               uint32_t *io_checksum,
                                               uint8_t *out_has_changes)
{
    if (fp == 0)
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_INVALID_ARG);
        return 0U;
    }

    UINT br = 0U;
    for (uint8_t bank = 0U; bank < PROJECT_V1_BANK_COUNT; ++bank)
    {
        for (uint8_t pattern = 0U; pattern < PROJECT_V1_PATTERN_COUNT; ++pattern)
        {
            project_v1_slot_record_t rec;
            if ((f_read(fp, &rec, sizeof(rec), &br) != FR_OK) || (br != sizeof(rec)))
            {
                project_sd_set_error(PROJECT_SD_BANK_ERR_READ_FAIL);
                return 0U;
            }

            if ((rec.bank != bank) || (rec.pattern != pattern))
            {
                project_sd_set_error(PROJECT_SD_BANK_ERR_INVALID_SIZE);
                return 0U;
            }

            const uint8_t rec_has_payload = (rec.payload_size != 0U) ? 1U : 0U;
            if ((rec.payload_size != 0U) && (rec.payload_size != sizeof(PatternSaveV1)))
            {
                project_sd_set_error(PROJECT_SD_BANK_ERR_INVALID_SIZE);
                return 0U;
            }

            uint8_t slot_has_data = 0U;
            uint32_t slot_checksum = 0U;
            uint8_t slot_meta_valid = 0U;
            if ((apply_to_pattern_bank != 0U) || (out_has_changes != 0))
            {
                if (pattern_sd_bank_get_slot_checksum(bank, pattern, &slot_has_data, &slot_checksum) == 0U)
                {
                    project_sd_set_error(PROJECT_SD_BANK_ERR_PATTERN_READ_FAIL);
                    return 0U;
                }
                slot_meta_valid = 1U;
            }

            if (out_has_changes != 0)
            {
                const uint8_t rec_has_data = (rec.has_data != 0U) ? 1U : 0U;
                uint8_t unchanged = (slot_meta_valid != 0U) && (slot_has_data == rec_has_data);
                if ((unchanged != 0U) && (rec_has_data != 0U))
                {
                    unchanged = (slot_checksum == rec.checksum) ? 1U : 0U;
                    if ((unchanged != 0U) && (rec.payload_size != sizeof(PatternSaveV1)))
                    {
                        unchanged = 0U;
                    }
                }

                if (unchanged == 0U)
                {
                    *out_has_changes = 1U;
                }
            }

            if ((apply_to_pattern_bank != 0U) && (io_checksum == 0) && (project_state == 0))
            {
                const FSIZE_t skip_to = f_tell(fp) + rec.payload_size;

                if (rec.has_data != 0U)
                {
                    if ((slot_has_data != 0U) && (slot_checksum == rec.checksum))
                    {
                        if ((rec_has_payload != 0U) && (f_lseek(fp, skip_to) != FR_OK))
                        {
                            project_sd_set_error(PROJECT_SD_BANK_ERR_SEEK_FAIL);
                            return 0U;
                        }
                        continue;
                    }
                }
                else
                {
                    if (pattern_sd_bank_slot_has_data(bank, pattern) != 0U)
                    {
                        if (pattern_sd_bank_delete_slot(bank, pattern) == 0U)
                        {
                            project_sd_set_error(PROJECT_SD_BANK_ERR_PATTERN_DELETE_FAIL);
                            return 0U;
                        }
                    }

                    if ((rec_has_payload != 0U) && (f_lseek(fp, skip_to) != FR_OK))
                    {
                        project_sd_set_error(PROJECT_SD_BANK_ERR_SEEK_FAIL);
                        return 0U;
                    }
                    continue;
                }
            }

            if (rec_has_payload != 0U)
            {
                if ((f_read(fp, &g_project_slot_buffer, sizeof(g_project_slot_buffer), &br) != FR_OK)
                    || (br != sizeof(g_project_slot_buffer)))
                {
                    project_sd_set_error(PROJECT_SD_BANK_ERR_READ_FAIL);
                    return 0U;
                }
            }
            else
            {
                memset(&g_project_slot_buffer, 0, sizeof(g_project_slot_buffer));
            }

            if ((rec.has_data != 0U) && (rec_has_payload == 0U))
            {
                project_sd_set_error(PROJECT_SD_BANK_ERR_INVALID_SIZE);
                return 0U;
            }

            if (rec_has_payload != 0U)
            {
                if (project_sd_checksum_accumulate(0U,
                                                   (const uint8_t *)&g_project_slot_buffer,
                                                   sizeof(g_project_slot_buffer))
                    != rec.checksum)
                {
                    project_sd_set_error(PROJECT_SD_BANK_ERR_CHECKSUM_FAIL);
                    return 0U;
                }
            }

            if (io_checksum != 0)
            {
                *io_checksum = project_sd_checksum_accumulate(*io_checksum, (const uint8_t *)&rec, sizeof(rec));
                if (rec_has_payload != 0U)
                {
                    *io_checksum = project_sd_checksum_accumulate(*io_checksum,
                                                                  (const uint8_t *)&g_project_slot_buffer,
                                                                  sizeof(g_project_slot_buffer));
                }
            }

            if (project_state != 0)
            {
                project_state->state.bank_has_data[bank][pattern] = (rec.has_data != 0U) ? 1U : 0U;
            }

            if (apply_to_pattern_bank != 0U)
            {
                if (rec.has_data != 0U)
                {
                    if (pattern_sd_bank_store_slot_nosync(bank, pattern, &g_project_slot_buffer) == 0U)
                    {
                        project_sd_set_error(PROJECT_SD_BANK_ERR_PATTERN_STORE_FAIL);
                        return 0U;
                    }
                }
                else if ((pattern_sd_bank_slot_has_data(bank, pattern) != 0U)
                         && (pattern_sd_bank_delete_slot(bank, pattern) == 0U))
                {
                    project_sd_set_error(PROJECT_SD_BANK_ERR_PATTERN_DELETE_FAIL);
                    return 0U;
                }
            }
        }
    }

    return 1U;
}

static void project_sd_scan_slots(void)
{
    (void)f_mkdir("0:/PROJECT");

    for (uint8_t slot = 0U; slot < PROJECT_V1_SLOT_COUNT; ++slot)
    {
        char path[32];
        FIL fp;
        UINT br = 0U;
        project_v1_file_header_t hdr;
        g_project_slot_has_data[slot] = 0U;

        if (project_sd_make_slot_path(path, sizeof(path), slot) == 0U)
        {
            continue;
        }

        if (f_open(&fp, path, FA_READ) != FR_OK)
        {
            continue;
        }

        if ((f_read(&fp, &hdr, sizeof(hdr), &br) == FR_OK)
            && (br == sizeof(hdr))
            && (project_sd_header_is_valid(&hdr, slot) != 0U))
        {
            g_project_slot_has_data[slot] = 1U;
        }

        (void)f_close(&fp);
    }
}

void project_sd_bank_refresh_slots(void)
{
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT) == 0U)
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_GATE_BUSY);
        return;
    }

    if (project_sd_mount_if_needed() != 0U)
    {
        project_sd_scan_slots();
        project_sd_set_error(PROJECT_SD_BANK_ERR_NONE);
    }
    else
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_MOUNT_FAIL);
    }

    sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
}

uint8_t project_sd_bank_list_slots(uint8_t *out_slots, uint8_t max_slots)
{
    uint8_t count = 0U;
    if ((out_slots == 0) || (max_slots == 0U))
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_INVALID_ARG);
        return 0U;
    }

    for (uint8_t slot = 0U; slot < PROJECT_V1_SLOT_COUNT; ++slot)
    {
        if (g_project_slot_has_data[slot] == 0U)
        {
            continue;
        }

        if (count >= max_slots)
        {
            break;
        }

        out_slots[count] = slot;
        count++;
    }

    project_sd_set_error(PROJECT_SD_BANK_ERR_NONE);
    return count;
}

void project_sd_bank_init(void)
{
    memset(&g_project_fs, 0, sizeof(g_project_fs));
    memset(&g_project_slot_has_data, 0, sizeof(g_project_slot_has_data));
    g_project_fs_mounted = 0U;
    project_sd_set_error(PROJECT_SD_BANK_ERR_NONE);

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT) == 0U)
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_GATE_BUSY);
        return;
    }

    if (project_sd_mount_if_needed() != 0U)
    {
        project_sd_scan_slots();
        project_sd_set_error(PROJECT_SD_BANK_ERR_NONE);
    }
    else
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_MOUNT_FAIL);
    }

    sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
}

uint8_t project_sd_bank_slot_has_data(uint8_t project_slot)
{
    if (project_sd_slot_is_valid(project_slot) == 0U)
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_INVALID_SLOT);
        return 0U;
    }

    project_sd_set_error(PROJECT_SD_BANK_ERR_NONE);
    return g_project_slot_has_data[project_slot];
}

uint8_t project_sd_bank_load_slot(uint8_t project_slot, ProjectSaveV1 *out_project, uint32_t *out_save_counter)
{
    if ((project_sd_slot_is_valid(project_slot) == 0U) || (out_project == 0))
    {
        project_sd_set_error((project_sd_slot_is_valid(project_slot) == 0U)
                                 ? PROJECT_SD_BANK_ERR_INVALID_SLOT
                                 : PROJECT_SD_BANK_ERR_INVALID_ARG);
        return 0U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT) == 0U)
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_GATE_BUSY);
        return 0U;
    }

    uint8_t ok = 0U;
    const uint32_t t_load_start = HAL_GetTick();
    project_sd_profile_t prof = { 0 };
    FIL fp;
    UINT br = 0U;
    project_v1_file_header_t hdr;
    project_v1_file_header_t commit_hdr;
    char path[32];
    uint32_t checksum = 0U;

    if ((project_sd_mount_if_needed() == 0U)
        || (project_sd_make_slot_path(path, sizeof(path), project_slot) == 0U))
    {
        project_sd_set_error((g_project_fs_mounted == 0U) ? PROJECT_SD_BANK_ERR_MOUNT_FAIL
                                                          : PROJECT_SD_BANK_ERR_PATH_FAIL);
        goto done;
    }

    sd_access_trace_begin("project_f_open_read");
    const uint32_t t_open_read = HAL_GetTick();
    const FRESULT fr_open = f_open(&fp, path, FA_READ);
    prof.open_read_ms += (HAL_GetTick() - t_open_read);
    sd_access_trace_end("project_f_open_read", (int)fr_open, 0U);
    if (fr_open != FR_OK)
    {
        if ((fr_open == FR_NO_FILE) || (fr_open == FR_NO_PATH))
        {
            project_sd_mark_slot_validity(project_slot, 0U);
        }
        project_sd_set_error(PROJECT_SD_BANK_ERR_OPEN_FAIL);
        goto done;
    }

    if ((f_read(&fp, &hdr, sizeof(hdr), &br) != FR_OK) || (br != sizeof(hdr)))
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_READ_FAIL);
        (void)f_close(&fp);
        goto done;
    }

    if (project_sd_header_is_valid(&hdr, project_slot) == 0U)
    {
        project_sd_mark_slot_validity(project_slot, 0U);
        project_sd_set_error(PROJECT_SD_BANK_ERR_INVALID_HEADER);
        (void)f_close(&fp);
        goto done;
    }

    const uint32_t t_read_live = HAL_GetTick();
    if ((f_read(&fp, out_project, sizeof(*out_project), &br) != FR_OK) || (br != sizeof(*out_project)))
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_READ_FAIL);
        (void)f_close(&fp);
        goto done;
    }
    prof.read_live_ms += (HAL_GetTick() - t_read_live);

    checksum = project_sd_checksum_accumulate(checksum, (const uint8_t *)out_project, sizeof(*out_project));
    const uint32_t t_validate_patterns = HAL_GetTick();
    uint8_t has_pattern_changes = 0U;
    if (project_sd_walk_pattern_records(&fp, 0U, out_project, &checksum, &has_pattern_changes) == 0U)
    {
        (void)f_close(&fp);
        goto done;
    }
    prof.validate_patterns_ms += (HAL_GetTick() - t_validate_patterns);

    (void)f_close(&fp);

    if (checksum != hdr.checksum)
    {
        project_sd_mark_slot_validity(project_slot, 0U);
        project_sd_set_error(PROJECT_SD_BANK_ERR_CHECKSUM_FAIL);
        goto done;
    }

    if (has_pattern_changes != 0U)
    {
        /* Commit phase: only reached after full project validation succeeds. */
        sd_access_trace_begin("project_f_open_read_commit");
        const uint32_t t_open_read_commit = HAL_GetTick();
        const FRESULT fr_reopen = f_open(&fp, path, FA_READ);
        prof.open_read_ms += (HAL_GetTick() - t_open_read_commit);
        sd_access_trace_end("project_f_open_read_commit", (int)fr_reopen, 0U);
        if (fr_reopen != FR_OK)
        {
            project_sd_set_error(PROJECT_SD_BANK_ERR_OPEN_FAIL);
            goto done;
        }

        if ((f_read(&fp, &commit_hdr, sizeof(commit_hdr), &br) != FR_OK) || (br != sizeof(commit_hdr)))
        {
            project_sd_set_error(PROJECT_SD_BANK_ERR_READ_FAIL);
            (void)f_close(&fp);
            goto done;
        }

        if ((commit_hdr.magic != hdr.magic)
            || (commit_hdr.version != hdr.version)
            || (commit_hdr.project_slot != hdr.project_slot)
            || (commit_hdr.save_counter != hdr.save_counter)
            || (commit_hdr.checksum != hdr.checksum))
        {
            project_sd_set_error(PROJECT_SD_BANK_ERR_INVALID_HEADER);
            (void)f_close(&fp);
            goto done;
        }

        if (f_lseek(&fp, sizeof(commit_hdr) + sizeof(ProjectSaveV1)) != FR_OK)
        {
            project_sd_set_error(PROJECT_SD_BANK_ERR_SEEK_FAIL);
            (void)f_close(&fp);
            goto done;
        }

        const uint32_t t_commit_patterns = HAL_GetTick();
        if (project_sd_walk_pattern_records(&fp, 1U, 0, 0, 0) == 0U)
        {
            (void)f_close(&fp);
            goto done;
        }
        prof.commit_patterns_ms += (HAL_GetTick() - t_commit_patterns);

        (void)f_close(&fp);
    }

    if (out_save_counter != 0)
    {
        *out_save_counter = hdr.save_counter;
    }

    project_sd_mark_slot_validity(project_slot, 1U);
    project_sd_set_error(PROJECT_SD_BANK_ERR_NONE);
    ok = 1U;

done:
    prof.total_ms = HAL_GetTick() - t_load_start;
    project_sd_profile_print("LOAD", &prof);
    sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
    return ok;
}

uint8_t project_sd_bank_store_slot(uint8_t project_slot, const ProjectSaveV1 *project, uint32_t save_counter)
{
    if ((project_sd_slot_is_valid(project_slot) == 0U) || (project == 0))
    {
        project_sd_set_error((project_sd_slot_is_valid(project_slot) == 0U)
                                 ? PROJECT_SD_BANK_ERR_INVALID_SLOT
                                 : PROJECT_SD_BANK_ERR_INVALID_ARG);
        return 0U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT) == 0U)
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_GATE_BUSY);
        return 0U;
    }

    uint8_t ok = 0U;
    const uint32_t t_save_start = HAL_GetTick();
    project_sd_profile_t prof = { 0 };
    FIL fp;
    UINT bw = 0U;
    project_v1_file_header_t hdr;
    char path[32];
    uint32_t checksum = 0U;

    if ((project_sd_mount_if_needed() == 0U)
        || (project_sd_make_slot_path(path, sizeof(path), project_slot) == 0U))
    {
        project_sd_set_error((g_project_fs_mounted == 0U) ? PROJECT_SD_BANK_ERR_MOUNT_FAIL
                                                          : PROJECT_SD_BANK_ERR_PATH_FAIL);
        goto done;
    }

    (void)f_mkdir("0:/PROJECT");

    sd_access_trace_begin("project_f_open_write");
    const uint32_t t_open_write = HAL_GetTick();
    const FRESULT fr_open = f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE);
    prof.open_write_ms += (HAL_GetTick() - t_open_write);
    sd_access_trace_end("project_f_open_write", (int)fr_open, 0U);
    if (fr_open != FR_OK)
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_OPEN_FAIL);
        goto done;
    }

    hdr.magic = PROJECT_V1_FILE_MAGIC;
    hdr.version = PROJECT_V1_FILE_VERSION;
    hdr.header_size = sizeof(project_v1_file_header_t);
    hdr.payload_size = sizeof(ProjectSaveV1);
    hdr.bank_count = PROJECT_V1_BANK_COUNT;
    hdr.pattern_count = PROJECT_V1_PATTERN_COUNT;
    hdr.slot_record_size = sizeof(project_v1_slot_record_t);
    hdr.pattern_payload_size = sizeof(PatternSaveV1);
    hdr.project_slot = project_slot;
    hdr.save_counter = save_counter;
    hdr.checksum = 0U;

    sd_access_trace_begin("project_f_write");
    const uint32_t t_write_live = HAL_GetTick();
    const FRESULT fr_wh = f_write(&fp, &hdr, sizeof(hdr), &bw);
    sd_access_trace_end("project_f_write", (int)fr_wh, 0U);
    if ((fr_wh != FR_OK) || (bw != sizeof(hdr)))
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_WRITE_FAIL);
        (void)f_close(&fp);
        goto done;
    }

    sd_access_trace_begin("project_f_write");
    const FRESULT fr_wp = f_write(&fp, project, sizeof(*project), &bw);
    sd_access_trace_end("project_f_write", (int)fr_wp, 0U);
    if ((fr_wp != FR_OK) || (bw != sizeof(*project)))
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_WRITE_FAIL);
        (void)f_close(&fp);
        goto done;
    }
    prof.write_live_ms += (HAL_GetTick() - t_write_live);

    checksum = project_sd_checksum_accumulate(checksum, (const uint8_t *)project, sizeof(*project));

    for (uint8_t bank = 0U; bank < PROJECT_V1_BANK_COUNT; ++bank)
    {
        for (uint8_t pattern = 0U; pattern < PROJECT_V1_PATTERN_COUNT; ++pattern)
        {
            uint8_t has_data = 0U;
            uint32_t slot_checksum = 0U;
            project_v1_slot_record_t rec;
            const uint32_t t_gather_pattern = HAL_GetTick();

            if (pattern_sd_bank_get_slot_checksum(bank, pattern, &has_data, &slot_checksum) == 0U)
            {
                project_sd_set_error(PROJECT_SD_BANK_ERR_PATTERN_READ_FAIL);
                (void)f_close(&fp);
                goto done;
            }

            if ((has_data != 0U) && (pattern_sd_bank_load_slot(bank, pattern, &g_project_slot_buffer) == 0U))
            {
                project_sd_set_error(PROJECT_SD_BANK_ERR_PATTERN_READ_FAIL);
                (void)f_close(&fp);
                goto done;
            }
            prof.gather_patterns_ms += (HAL_GetTick() - t_gather_pattern);

            rec.bank = bank;
            rec.pattern = pattern;
            rec.has_data = has_data;
            rec.reserved = 0U;
            rec.payload_size = (has_data != 0U) ? sizeof(PatternSaveV1) : 0U;
            rec.checksum = (has_data != 0U) ? slot_checksum : 0U;

            sd_access_trace_begin("project_f_write");
            const uint32_t t_write_pattern = HAL_GetTick();
            const FRESULT fr_wr = f_write(&fp, &rec, sizeof(rec), &bw);
            sd_access_trace_end("project_f_write", (int)fr_wr, 0U);
            if ((fr_wr != FR_OK) || (bw != sizeof(rec)))
            {
                project_sd_set_error(PROJECT_SD_BANK_ERR_WRITE_FAIL);
                (void)f_close(&fp);
                goto done;
            }

            if (has_data != 0U)
            {
                sd_access_trace_begin("project_f_write");
                const FRESULT fr_wd = f_write(&fp, &g_project_slot_buffer, sizeof(g_project_slot_buffer), &bw);
                sd_access_trace_end("project_f_write", (int)fr_wd, 0U);
                if ((fr_wd != FR_OK) || (bw != sizeof(g_project_slot_buffer)))
                {
                    project_sd_set_error(PROJECT_SD_BANK_ERR_WRITE_FAIL);
                    (void)f_close(&fp);
                    goto done;
                }
            }
            prof.write_patterns_ms += (HAL_GetTick() - t_write_pattern);

            checksum = project_sd_checksum_accumulate(checksum, (const uint8_t *)&rec, sizeof(rec));
            if (has_data != 0U)
            {
                checksum = project_sd_checksum_accumulate(checksum,
                                                          (const uint8_t *)&g_project_slot_buffer,
                                                          sizeof(g_project_slot_buffer));
            }
        }
    }

    hdr.checksum = checksum;

    if (f_lseek(&fp, 0U) != FR_OK)
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_SEEK_FAIL);
        (void)f_close(&fp);
        goto done;
    }

    sd_access_trace_begin("project_f_write");
    const FRESULT fr_rewrite = f_write(&fp, &hdr, sizeof(hdr), &bw);
    sd_access_trace_end("project_f_write", (int)fr_rewrite, 0U);
    if ((fr_rewrite != FR_OK) || (bw != sizeof(hdr)))
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_WRITE_FAIL);
        (void)f_close(&fp);
        goto done;
    }

    sd_access_trace_begin("project_f_sync");
    const uint32_t t_sync = HAL_GetTick();
    const FRESULT fr_sync = f_sync(&fp);
    prof.sync_ms += (HAL_GetTick() - t_sync);
    sd_access_trace_end("project_f_sync", (int)fr_sync, 0U);
    (void)f_close(&fp);
    if (fr_sync != FR_OK)
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_SYNC_FAIL);
        goto done;
    }

    project_sd_mark_slot_validity(project_slot, 1U);
    project_sd_set_error(PROJECT_SD_BANK_ERR_NONE);
    ok = 1U;

done:
    prof.total_ms = HAL_GetTick() - t_save_start;
    project_sd_profile_print("SAVE", &prof);
    sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
    return ok;
}

uint8_t project_sd_bank_is_slot_equivalent_to_live(uint8_t project_slot)
{
    if (project_sd_slot_is_valid(project_slot) == 0U)
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_INVALID_SLOT);
        return 0U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT) == 0U)
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_GATE_BUSY);
        return 0U;
    }

    uint8_t is_equivalent = 0U;
    FIL fp;
    UINT br = 0U;
    project_v1_file_header_t hdr;
    ProjectSaveV1 project_state;
    char path[32];
    uint32_t checksum = 0U;
    uint8_t has_pattern_changes = 1U;

    if ((project_sd_mount_if_needed() == 0U)
        || (project_sd_make_slot_path(path, sizeof(path), project_slot) == 0U))
    {
        project_sd_set_error((g_project_fs_mounted == 0U) ? PROJECT_SD_BANK_ERR_MOUNT_FAIL
                                                          : PROJECT_SD_BANK_ERR_PATH_FAIL);
        goto done;
    }

    const FRESULT fr_open = f_open(&fp, path, FA_READ);
    if (fr_open != FR_OK)
    {
        if ((fr_open == FR_NO_FILE) || (fr_open == FR_NO_PATH))
        {
            project_sd_mark_slot_validity(project_slot, 0U);
        }
        project_sd_set_error(PROJECT_SD_BANK_ERR_OPEN_FAIL);
        goto done;
    }

    if ((f_read(&fp, &hdr, sizeof(hdr), &br) != FR_OK) || (br != sizeof(hdr)))
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_READ_FAIL);
        (void)f_close(&fp);
        goto done;
    }

    if (project_sd_header_is_valid(&hdr, project_slot) == 0U)
    {
        project_sd_mark_slot_validity(project_slot, 0U);
        project_sd_set_error(PROJECT_SD_BANK_ERR_INVALID_HEADER);
        (void)f_close(&fp);
        goto done;
    }

    if ((f_read(&fp, &project_state, sizeof(project_state), &br) != FR_OK) || (br != sizeof(project_state)))
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_READ_FAIL);
        (void)f_close(&fp);
        goto done;
    }

    checksum = project_sd_checksum_accumulate(checksum, (const uint8_t *)&project_state, sizeof(project_state));
    has_pattern_changes = 0U;
    if (project_sd_walk_pattern_records(&fp, 0U, 0, &checksum, &has_pattern_changes) == 0U)
    {
        (void)f_close(&fp);
        goto done;
    }

    (void)f_close(&fp);

    if (checksum != hdr.checksum)
    {
        project_sd_mark_slot_validity(project_slot, 0U);
        project_sd_set_error(PROJECT_SD_BANK_ERR_CHECKSUM_FAIL);
        goto done;
    }

    is_equivalent = (has_pattern_changes == 0U) ? 1U : 0U;
    project_sd_mark_slot_validity(project_slot, 1U);
    project_sd_set_error(PROJECT_SD_BANK_ERR_NONE);

done:
    sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
    return is_equivalent;
}

uint8_t project_sd_bank_delete_slot(uint8_t project_slot)
{
    if (project_sd_slot_is_valid(project_slot) == 0U)
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_INVALID_SLOT);
        return 0U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT) == 0U)
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_GATE_BUSY);
        return 0U;
    }

    uint8_t ok = 0U;
    char path[32];

    if ((project_sd_mount_if_needed() == 0U)
        || (project_sd_make_slot_path(path, sizeof(path), project_slot) == 0U))
    {
        project_sd_set_error((g_project_fs_mounted == 0U) ? PROJECT_SD_BANK_ERR_MOUNT_FAIL
                                                          : PROJECT_SD_BANK_ERR_PATH_FAIL);
        goto done;
    }

    sd_access_trace_begin("project_f_unlink");
    const FRESULT fr_unlink = f_unlink(path);
    sd_access_trace_end("project_f_unlink", (int)fr_unlink, 0U);
    if ((fr_unlink != FR_OK) && (fr_unlink != FR_NO_FILE))
    {
        project_sd_set_error(PROJECT_SD_BANK_ERR_UNLINK_FAIL);
        goto done;
    }

    project_sd_mark_slot_validity(project_slot, 0U);
    project_sd_set_error(PROJECT_SD_BANK_ERR_NONE);
    ok = 1U;

done:
    sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
    return ok;
}

project_sd_bank_error_t project_sd_bank_get_last_error(void)
{
    return g_project_sd_last_error;
}

const char *project_sd_bank_error_to_string(project_sd_bank_error_t err)
{
    switch (err)
    {
        case PROJECT_SD_BANK_ERR_NONE: return "NONE";
        case PROJECT_SD_BANK_ERR_INVALID_SLOT: return "INVALID_SLOT";
        case PROJECT_SD_BANK_ERR_INVALID_ARG: return "INVALID_ARG";
        case PROJECT_SD_BANK_ERR_GATE_BUSY: return "GATE_BUSY";
        case PROJECT_SD_BANK_ERR_MOUNT_FAIL: return "MOUNT_FAIL";
        case PROJECT_SD_BANK_ERR_PATH_FAIL: return "PATH_FAIL";
        case PROJECT_SD_BANK_ERR_OPEN_FAIL: return "OPEN_FAIL";
        case PROJECT_SD_BANK_ERR_READ_FAIL: return "READ_FAIL";
        case PROJECT_SD_BANK_ERR_WRITE_FAIL: return "WRITE_FAIL";
        case PROJECT_SD_BANK_ERR_SYNC_FAIL: return "SYNC_FAIL";
        case PROJECT_SD_BANK_ERR_SEEK_FAIL: return "SEEK_FAIL";
        case PROJECT_SD_BANK_ERR_INVALID_HEADER: return "INVALID_HEADER";
        case PROJECT_SD_BANK_ERR_INVALID_SIZE: return "INVALID_SIZE";
        case PROJECT_SD_BANK_ERR_CHECKSUM_FAIL: return "CHECKSUM_FAIL";
        case PROJECT_SD_BANK_ERR_PATTERN_READ_FAIL: return "PATTERN_READ_FAIL";
        case PROJECT_SD_BANK_ERR_PATTERN_STORE_FAIL: return "PATTERN_STORE_FAIL";
        case PROJECT_SD_BANK_ERR_PATTERN_DELETE_FAIL: return "PATTERN_DELETE_FAIL";
        case PROJECT_SD_BANK_ERR_UNLINK_FAIL: return "UNLINK_FAIL";
        default: return "UNKNOWN";
    }
}
