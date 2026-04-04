#include "Storage/pattern_sd_bank.h"

#include <stdio.h>
#include <string.h>

#include "Storage/sd_access_gate.h"
#include "ff.h"

#define PATTERN_BANK_COUNT 16U
#define PATTERN_PER_BANK   16U
#define PATTERN_MAGIC      0x31544150UL /* PAT1 */
#define PATTERN_VERSION    1U

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint16_t version;
    uint32_t payload_size;
    uint32_t checksum;
} pattern_sd_slot_header_t;

static FATFS g_pattern_fs;
static uint8_t g_pattern_fs_mounted;
static uint8_t g_slot_has_data[PATTERN_BANK_COUNT][PATTERN_PER_BANK];
static PatternSaveV1 g_boot_pattern;
static uint8_t g_boot_pattern_valid;

static uint8_t pattern_sd_slot_is_valid(uint8_t bank, uint8_t pattern)
{
    return (bank < PATTERN_BANK_COUNT) && (pattern < PATTERN_PER_BANK);
}

static uint32_t pattern_sd_checksum(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 5381UL;
    for (uint32_t i = 0U; i < len; ++i)
    {
        crc = ((crc << 5) + crc) ^ data[i];
    }
    return crc;
}

static uint8_t pattern_sd_mount_if_needed(void)
{
    if (g_pattern_fs_mounted != 0U)
    {
        return 1U;
    }

    sd_access_trace_begin("pattern_f_mount");
    const FRESULT fr = f_mount(&g_pattern_fs, "0:", 1U);
    sd_access_trace_end("pattern_f_mount", (int)fr, 0U);
    if (fr != FR_OK)
    {
        return 0U;
    }

    g_pattern_fs_mounted = 1U;
    return 1U;
}

static uint8_t pattern_sd_make_slot_path(char *out_path, uint32_t out_size, uint8_t bank, uint8_t pattern)
{
    if ((out_path == 0) || (out_size < 24U))
    {
        return 0U;
    }

    const int n = snprintf(out_path, out_size, "0:/PATTERN/B%02u_P%02u.PAT", (unsigned)bank, (unsigned)pattern);
    return (n > 0) && ((uint32_t)n < out_size);
}

static uint8_t pattern_sd_scan_slots(void)
{
    if (f_mkdir("0:/PATTERN") != FR_OK)
    {
        /* Ignore if already exists / readonly mismatch during scan phase. */
    }

    for (uint8_t bank = 0U; bank < PATTERN_BANK_COUNT; ++bank)
    {
        for (uint8_t pattern = 0U; pattern < PATTERN_PER_BANK; ++pattern)
        {
            char path[32];
            FILINFO info;
            g_slot_has_data[bank][pattern] = 0U;

            if (pattern_sd_make_slot_path(path, sizeof(path), bank, pattern) == 0U)
            {
                continue;
            }

            if (f_stat(path, &info) == FR_OK)
            {
                g_slot_has_data[bank][pattern] = 1U;
            }
        }
    }

    return 1U;
}

void pattern_sd_bank_init(const PatternSaveV1 *boot_pattern)
{
    memset(&g_pattern_fs, 0, sizeof(g_pattern_fs));
    memset(&g_slot_has_data, 0, sizeof(g_slot_has_data));
    g_pattern_fs_mounted = 0U;
    g_boot_pattern_valid = 0U;

    if (boot_pattern != 0)
    {
        memcpy(&g_boot_pattern, boot_pattern, sizeof(g_boot_pattern));
        g_boot_pattern_valid = 1U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PATTERN) == 0U)
    {
        return;
    }

    if ((pattern_sd_mount_if_needed() != 0U))
    {
        (void)pattern_sd_scan_slots();
    }

    sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);
}

uint8_t pattern_sd_bank_slot_has_data(uint8_t bank, uint8_t pattern)
{
    if (pattern_sd_slot_is_valid(bank, pattern) == 0U)
    {
        return 0U;
    }
    return g_slot_has_data[bank][pattern];
}

uint8_t pattern_sd_bank_load_slot(uint8_t bank, uint8_t pattern, PatternSaveV1 *out_pattern)
{
    if ((pattern_sd_slot_is_valid(bank, pattern) == 0U) || (out_pattern == 0))
    {
        return 0U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PATTERN) == 0U)
    {
        return 0U;
    }

    uint8_t ok = 0U;
    FIL fp;
    UINT br = 0U;
    pattern_sd_slot_header_t hdr;
    char path[32];

    if ((pattern_sd_mount_if_needed() == 0U) || (pattern_sd_make_slot_path(path, sizeof(path), bank, pattern) == 0U))
    {
        goto done;
    }

    sd_access_trace_begin("pattern_f_open_read");
    const FRESULT fr_open = f_open(&fp, path, FA_READ);
    sd_access_trace_end("pattern_f_open_read", (int)fr_open, 0U);
    if (fr_open != FR_OK)
    {
        if (g_boot_pattern_valid != 0U)
        {
            memcpy(out_pattern, &g_boot_pattern, sizeof(*out_pattern));
            ok = 1U;
        }
        goto done;
    }

    if ((f_read(&fp, &hdr, sizeof(hdr), &br) != FR_OK) || (br != sizeof(hdr)))
    {
        (void)f_close(&fp);
        goto done;
    }

    if ((hdr.magic != PATTERN_MAGIC) || (hdr.version != PATTERN_VERSION) || (hdr.payload_size != sizeof(PatternSaveV1)))
    {
        (void)f_close(&fp);
        goto done;
    }

    if ((f_read(&fp, out_pattern, sizeof(*out_pattern), &br) != FR_OK) || (br != sizeof(*out_pattern)))
    {
        (void)f_close(&fp);
        goto done;
    }

    (void)f_close(&fp);

    if (pattern_sd_checksum((const uint8_t *)out_pattern, sizeof(*out_pattern)) != hdr.checksum)
    {
        goto done;
    }

    g_slot_has_data[bank][pattern] = 1U;
    ok = 1U;

done:
    sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);
    return ok;
}

uint8_t pattern_sd_bank_store_slot(uint8_t bank, uint8_t pattern, const PatternSaveV1 *pattern_data)
{
    if ((pattern_sd_slot_is_valid(bank, pattern) == 0U) || (pattern_data == 0))
    {
        return 0U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PATTERN) == 0U)
    {
        return 0U;
    }

    uint8_t ok = 0U;
    FIL fp;
    UINT bw = 0U;
    pattern_sd_slot_header_t hdr;
    char path[32];

    if ((pattern_sd_mount_if_needed() == 0U) || (pattern_sd_make_slot_path(path, sizeof(path), bank, pattern) == 0U))
    {
        goto done;
    }

    (void)f_mkdir("0:/PATTERN");

    sd_access_trace_begin("pattern_f_open_write");
    const FRESULT fr_open = f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE);
    sd_access_trace_end("pattern_f_open_write", (int)fr_open, 0U);
    if (fr_open != FR_OK)
    {
        goto done;
    }

    hdr.magic = PATTERN_MAGIC;
    hdr.version = PATTERN_VERSION;
    hdr.payload_size = sizeof(PatternSaveV1);
    hdr.checksum = pattern_sd_checksum((const uint8_t *)pattern_data, sizeof(*pattern_data));

    sd_access_trace_begin("pattern_f_write");
    const FRESULT fr_wh = f_write(&fp, &hdr, sizeof(hdr), &bw);
    sd_access_trace_end("pattern_f_write", (int)fr_wh, 0U);
    if ((fr_wh != FR_OK) || (bw != sizeof(hdr)))
    {
        (void)f_close(&fp);
        goto done;
    }

    sd_access_trace_begin("pattern_f_write");
    const FRESULT fr_wp = f_write(&fp, pattern_data, sizeof(*pattern_data), &bw);
    sd_access_trace_end("pattern_f_write", (int)fr_wp, 0U);
    if ((fr_wp != FR_OK) || (bw != sizeof(*pattern_data)))
    {
        (void)f_close(&fp);
        goto done;
    }

    sd_access_trace_begin("pattern_f_sync");
    const FRESULT fr_sync = f_sync(&fp);
    sd_access_trace_end("pattern_f_sync", (int)fr_sync, 0U);
    (void)f_close(&fp);
    if (fr_sync != FR_OK)
    {
        goto done;
    }

    g_slot_has_data[bank][pattern] = 1U;
    ok = 1U;

done:
    sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);
    return ok;
}
