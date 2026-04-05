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

static uint8_t project_sd_slot_is_valid(uint8_t project_slot)
{
    return (project_slot < PROJECT_V1_SLOT_COUNT) ? 1U : 0U;
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

static void project_sd_scan_slots(void)
{
    (void)f_mkdir("0:/PROJECT");

    for (uint8_t slot = 0U; slot < PROJECT_V1_SLOT_COUNT; ++slot)
    {
        char path[32];
        FILINFO info;
        g_project_slot_has_data[slot] = 0U;

        if (project_sd_make_slot_path(path, sizeof(path), slot) == 0U)
        {
            continue;
        }

        if (f_stat(path, &info) == FR_OK)
        {
            g_project_slot_has_data[slot] = 1U;
        }
    }
}

void project_sd_bank_init(void)
{
    memset(&g_project_fs, 0, sizeof(g_project_fs));
    memset(&g_project_slot_has_data, 0, sizeof(g_project_slot_has_data));
    g_project_fs_mounted = 0U;

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT) == 0U)
    {
        return;
    }

    if (project_sd_mount_if_needed() != 0U)
    {
        project_sd_scan_slots();
    }

    sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
}

uint8_t project_sd_bank_slot_has_data(uint8_t project_slot)
{
    if (project_sd_slot_is_valid(project_slot) == 0U)
    {
        return 0U;
    }

    return g_project_slot_has_data[project_slot];
}

uint8_t project_sd_bank_load_slot(uint8_t project_slot, ProjectSaveV1 *out_project, uint32_t *out_save_counter)
{
    if ((project_sd_slot_is_valid(project_slot) == 0U) || (out_project == 0))
    {
        return 0U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT) == 0U)
    {
        return 0U;
    }

    uint8_t ok = 0U;
    FIL fp;
    UINT br = 0U;
    project_v1_file_header_t hdr;
    char path[32];
    uint32_t checksum = 0U;

    if ((project_sd_mount_if_needed() == 0U)
        || (project_sd_make_slot_path(path, sizeof(path), project_slot) == 0U))
    {
        goto done;
    }

    sd_access_trace_begin("project_f_open_read");
    const FRESULT fr_open = f_open(&fp, path, FA_READ);
    sd_access_trace_end("project_f_open_read", (int)fr_open, 0U);
    if (fr_open != FR_OK)
    {
        goto done;
    }

    if ((f_read(&fp, &hdr, sizeof(hdr), &br) != FR_OK) || (br != sizeof(hdr)))
    {
        (void)f_close(&fp);
        goto done;
    }

    if ((hdr.magic != PROJECT_V1_FILE_MAGIC)
        || (hdr.version != PROJECT_V1_FILE_VERSION)
        || (hdr.header_size != sizeof(project_v1_file_header_t))
        || (hdr.payload_size != sizeof(ProjectSaveV1))
        || (hdr.bank_count != PROJECT_V1_BANK_COUNT)
        || (hdr.pattern_count != PROJECT_V1_PATTERN_COUNT)
        || (hdr.slot_record_size != sizeof(project_v1_slot_record_t))
        || (hdr.pattern_payload_size != sizeof(PatternSaveV1))
        || (hdr.project_slot != (uint32_t)project_slot))
    {
        (void)f_close(&fp);
        goto done;
    }

    if ((f_read(&fp, out_project, sizeof(*out_project), &br) != FR_OK) || (br != sizeof(*out_project)))
    {
        (void)f_close(&fp);
        goto done;
    }

    checksum = project_sd_checksum_accumulate(checksum, (const uint8_t *)out_project, sizeof(*out_project));

    for (uint8_t bank = 0U; bank < PROJECT_V1_BANK_COUNT; ++bank)
    {
        for (uint8_t pattern = 0U; pattern < PROJECT_V1_PATTERN_COUNT; ++pattern)
        {
            project_v1_slot_record_t rec;
            if ((f_read(&fp, &rec, sizeof(rec), &br) != FR_OK) || (br != sizeof(rec)))
            {
                (void)f_close(&fp);
                goto done;
            }

            if ((rec.bank != bank) || (rec.pattern != pattern) || (rec.payload_size != sizeof(PatternSaveV1)))
            {
                (void)f_close(&fp);
                goto done;
            }

            if ((f_read(&fp, &g_project_slot_buffer, sizeof(g_project_slot_buffer), &br) != FR_OK)
                || (br != sizeof(g_project_slot_buffer)))
            {
                (void)f_close(&fp);
                goto done;
            }

            if (project_sd_checksum_accumulate(0U,
                                               (const uint8_t *)&g_project_slot_buffer,
                                               sizeof(g_project_slot_buffer))
                != rec.checksum)
            {
                (void)f_close(&fp);
                goto done;
            }

            checksum = project_sd_checksum_accumulate(checksum, (const uint8_t *)&rec, sizeof(rec));
            checksum = project_sd_checksum_accumulate(checksum,
                                                      (const uint8_t *)&g_project_slot_buffer,
                                                      sizeof(g_project_slot_buffer));

            out_project->state.bank_has_data[bank][pattern] = (rec.has_data != 0U) ? 1U : 0U;

            if (rec.has_data != 0U)
            {
                if (pattern_sd_bank_store_slot(bank, pattern, &g_project_slot_buffer) == 0U)
                {
                    (void)f_close(&fp);
                    goto done;
                }
            }
            else if (pattern_sd_bank_delete_slot(bank, pattern) == 0U)
            {
                (void)f_close(&fp);
                goto done;
            }
        }
    }

    (void)f_close(&fp);

    if (checksum != hdr.checksum)
    {
        goto done;
    }

    if (out_save_counter != 0)
    {
        *out_save_counter = hdr.save_counter;
    }

    g_project_slot_has_data[project_slot] = 1U;
    ok = 1U;

done:
    sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
    return ok;
}

uint8_t project_sd_bank_store_slot(uint8_t project_slot, const ProjectSaveV1 *project, uint32_t save_counter)
{
    if ((project_sd_slot_is_valid(project_slot) == 0U) || (project == 0))
    {
        return 0U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT) == 0U)
    {
        return 0U;
    }

    uint8_t ok = 0U;
    FIL fp;
    UINT bw = 0U;
    project_v1_file_header_t hdr;
    char path[32];
    uint32_t checksum = 0U;

    if ((project_sd_mount_if_needed() == 0U)
        || (project_sd_make_slot_path(path, sizeof(path), project_slot) == 0U))
    {
        goto done;
    }

    (void)f_mkdir("0:/PROJECT");

    sd_access_trace_begin("project_f_open_write");
    const FRESULT fr_open = f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE);
    sd_access_trace_end("project_f_open_write", (int)fr_open, 0U);
    if (fr_open != FR_OK)
    {
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
    const FRESULT fr_wh = f_write(&fp, &hdr, sizeof(hdr), &bw);
    sd_access_trace_end("project_f_write", (int)fr_wh, 0U);
    if ((fr_wh != FR_OK) || (bw != sizeof(hdr)))
    {
        (void)f_close(&fp);
        goto done;
    }

    sd_access_trace_begin("project_f_write");
    const FRESULT fr_wp = f_write(&fp, project, sizeof(*project), &bw);
    sd_access_trace_end("project_f_write", (int)fr_wp, 0U);
    if ((fr_wp != FR_OK) || (bw != sizeof(*project)))
    {
        (void)f_close(&fp);
        goto done;
    }

    checksum = project_sd_checksum_accumulate(checksum, (const uint8_t *)project, sizeof(*project));

    for (uint8_t bank = 0U; bank < PROJECT_V1_BANK_COUNT; ++bank)
    {
        for (uint8_t pattern = 0U; pattern < PROJECT_V1_PATTERN_COUNT; ++pattern)
        {
            const uint8_t has_data = pattern_sd_bank_slot_has_data(bank, pattern);
            project_v1_slot_record_t rec;

            if ((has_data != 0U)
                && (pattern_sd_bank_load_slot(bank, pattern, &g_project_slot_buffer) == 0U))
            {
                (void)f_close(&fp);
                goto done;
            }

            if (has_data == 0U)
            {
                memset(&g_project_slot_buffer, 0, sizeof(g_project_slot_buffer));
            }

            rec.bank = bank;
            rec.pattern = pattern;
            rec.has_data = has_data;
            rec.reserved = 0U;
            rec.payload_size = sizeof(PatternSaveV1);
            rec.checksum = project_sd_checksum_accumulate(0U,
                                                          (const uint8_t *)&g_project_slot_buffer,
                                                          sizeof(g_project_slot_buffer));

            sd_access_trace_begin("project_f_write");
            const FRESULT fr_wr = f_write(&fp, &rec, sizeof(rec), &bw);
            sd_access_trace_end("project_f_write", (int)fr_wr, 0U);
            if ((fr_wr != FR_OK) || (bw != sizeof(rec)))
            {
                (void)f_close(&fp);
                goto done;
            }

            sd_access_trace_begin("project_f_write");
            const FRESULT fr_wd = f_write(&fp, &g_project_slot_buffer, sizeof(g_project_slot_buffer), &bw);
            sd_access_trace_end("project_f_write", (int)fr_wd, 0U);
            if ((fr_wd != FR_OK) || (bw != sizeof(g_project_slot_buffer)))
            {
                (void)f_close(&fp);
                goto done;
            }

            checksum = project_sd_checksum_accumulate(checksum, (const uint8_t *)&rec, sizeof(rec));
            checksum = project_sd_checksum_accumulate(checksum,
                                                      (const uint8_t *)&g_project_slot_buffer,
                                                      sizeof(g_project_slot_buffer));
        }
    }

    hdr.checksum = checksum;

    if (f_lseek(&fp, 0U) != FR_OK)
    {
        (void)f_close(&fp);
        goto done;
    }

    sd_access_trace_begin("project_f_write");
    const FRESULT fr_rewrite = f_write(&fp, &hdr, sizeof(hdr), &bw);
    sd_access_trace_end("project_f_write", (int)fr_rewrite, 0U);
    if ((fr_rewrite != FR_OK) || (bw != sizeof(hdr)))
    {
        (void)f_close(&fp);
        goto done;
    }

    sd_access_trace_begin("project_f_sync");
    const FRESULT fr_sync = f_sync(&fp);
    sd_access_trace_end("project_f_sync", (int)fr_sync, 0U);
    (void)f_close(&fp);
    if (fr_sync != FR_OK)
    {
        goto done;
    }

    g_project_slot_has_data[project_slot] = 1U;
    ok = 1U;

done:
    sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
    return ok;
}
