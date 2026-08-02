#include "Storage/pattern_sd_bank.h"

#include <stdio.h>
#include <string.h>

#include "Storage/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "ff.h"

#define PATTERN_BANK_COUNT 16U
#define PATTERN_PER_BANK   16U
#define PATTERN_MAGIC      0x31544150UL /* PAT1 */
#define PATTERN_VERSION    6U /* Eight homogeneous track slots. */
#define PATTERN_WRITE_CHUNK_BYTES (512U * 8U)

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint16_t version;
    uint32_t payload_size;
    uint32_t checksum;
} pattern_sd_slot_header_t;

static uint8_t g_slot_has_data[PATTERN_BANK_COUNT][PATTERN_PER_BANK];
static uint8_t g_slot_meta_cache_valid[PATTERN_BANK_COUNT][PATTERN_PER_BANK];
STORAGE_STATE_SDRAM static uint32_t g_slot_checksum_cache[PATTERN_BANK_COUNT][PATTERN_PER_BANK];
static DMA_BUFFER uint8_t g_pattern_write_chunk[PATTERN_WRITE_CHUNK_BYTES];

static uint8_t pattern_sd_slot_is_valid(uint8_t bank, uint8_t pattern)
{
    return (bank < PATTERN_BANK_COUNT) && (pattern < PATTERN_PER_BANK);
}

static uint8_t pattern_sd_bank_store_slot_internal(uint8_t bank,
                                                   uint8_t pattern,
                                                   const PatternSaveV1 *pattern_data,
                                                   uint8_t do_sync);
static uint8_t pattern_sd_read_valid_slot_header(uint8_t bank,
                                                 uint8_t pattern,
                                                 pattern_sd_slot_header_t *out_hdr,
                                                 uint8_t *out_missing);
static uint8_t pattern_sd_write_payload_chunked(FIL *fp, const PatternSaveV1 *pattern_data);
static uint8_t pattern_sd_header_is_supported(const pattern_sd_slot_header_t *hdr);

static uint32_t pattern_sd_checksum(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 5381UL;
    for (uint32_t i = 0U; i < len; ++i)
    {
        crc = ((crc << 5) + crc) ^ data[i];
    }
    return crc;
}

static uint8_t pattern_sd_header_is_supported(const pattern_sd_slot_header_t *hdr)
{
    if (hdr == 0)
    {
        return 0U;
    }

    if (hdr->magic != PATTERN_MAGIC)
    {
        return 0U;
    }

    if ((hdr->version == PATTERN_VERSION) && (hdr->payload_size == sizeof(PatternSaveV1)))
    {
        return 1U;
    }

    return 0U;
}

static void pattern_sd_meta_cache_invalidate(uint8_t bank, uint8_t pattern)
{
    if (pattern_sd_slot_is_valid(bank, pattern) == 0U)
    {
        return;
    }

    g_slot_meta_cache_valid[bank][pattern] = 0U;
}

static void pattern_sd_meta_cache_store(uint8_t bank, uint8_t pattern, uint8_t has_data, uint32_t checksum)
{
    if (pattern_sd_slot_is_valid(bank, pattern) == 0U)
    {
        return;
    }

    g_slot_has_data[bank][pattern] = (has_data != 0U) ? 1U : 0U;
    g_slot_checksum_cache[bank][pattern] = (has_data != 0U) ? checksum : 0U;
    g_slot_meta_cache_valid[bank][pattern] = 1U;
}

static uint8_t pattern_sd_mount_if_needed(void)
{
    return sd_access_fs_mount_if_needed();
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
            g_slot_has_data[bank][pattern] = 0U;
            g_slot_meta_cache_valid[bank][pattern] = 0U;
            g_slot_checksum_cache[bank][pattern] = 0U;

            pattern_sd_slot_header_t hdr;
            if (pattern_sd_read_valid_slot_header(bank, pattern, &hdr, 0) != 0U)
            {
                pattern_sd_meta_cache_store(bank, pattern, 1U, hdr.checksum);
            }
            else
            {
                pattern_sd_meta_cache_store(bank, pattern, 0U, 0U);
            }
        }
    }

    return 1U;
}

void pattern_sd_bank_init(void)
{
    memset(&g_slot_has_data, 0, sizeof(g_slot_has_data));
    memset(&g_slot_meta_cache_valid, 0, sizeof(g_slot_meta_cache_valid));
    memset(&g_slot_checksum_cache, 0, sizeof(g_slot_checksum_cache));

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

uint8_t pattern_sd_bank_get_slot_checksum(uint8_t bank,
                                          uint8_t pattern,
                                          uint8_t *out_has_data,
                                          uint32_t *out_checksum)
{
    if ((pattern_sd_slot_is_valid(bank, pattern) == 0U) || (out_has_data == 0) || (out_checksum == 0))
    {
        return 0U;
    }

    if (g_slot_has_data[bank][pattern] == 0U)
    {
        pattern_sd_meta_cache_store(bank, pattern, 0U, 0U);
        *out_has_data = 0U;
        *out_checksum = 0U;
        return 1U;
    }

    if (g_slot_meta_cache_valid[bank][pattern] != 0U)
    {
        *out_has_data = g_slot_has_data[bank][pattern];
        *out_checksum = (g_slot_has_data[bank][pattern] != 0U) ? g_slot_checksum_cache[bank][pattern] : 0U;
        return 1U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PATTERN) == 0U)
    {
        return 0U;
    }

    uint8_t ok = 0U;
    pattern_sd_slot_header_t hdr;
    uint8_t missing = 0U;

    if (pattern_sd_mount_if_needed() == 0U)
    {
        pattern_sd_meta_cache_invalidate(bank, pattern);
        goto done;
    }

    if (pattern_sd_read_valid_slot_header(bank, pattern, &hdr, &missing) == 0U)
    {
        if (missing != 0U)
        {
            pattern_sd_meta_cache_store(bank, pattern, 0U, 0U);
            *out_has_data = 0U;
            *out_checksum = 0U;
            ok = 1U;
        }
        else
        {
            pattern_sd_meta_cache_invalidate(bank, pattern);
        }
        goto done;
    }

    pattern_sd_meta_cache_store(bank, pattern, 1U, hdr.checksum);
    *out_has_data = 1U;
    *out_checksum = hdr.checksum;
    ok = 1U;

done:
    sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);
    return ok;
}

static uint8_t pattern_sd_read_valid_slot_header(uint8_t bank,
                                                 uint8_t pattern,
                                                 pattern_sd_slot_header_t *out_hdr,
                                                 uint8_t *out_missing)
{
    FIL fp;
    UINT br = 0U;
    pattern_sd_slot_header_t hdr;
    char path[32];

    if (out_missing != 0)
    {
        *out_missing = 0U;
    }

    if (pattern_sd_make_slot_path(path, sizeof(path), bank, pattern) == 0U)
    {
        return 0U;
    }

    const FRESULT fr_open = f_open(&fp, path, FA_READ);
    if (fr_open != FR_OK)
    {
        if ((out_missing != 0) && ((fr_open == FR_NO_FILE) || (fr_open == FR_NO_PATH)))
        {
            *out_missing = 1U;
        }
        return 0U;
    }

    if ((f_read(&fp, &hdr, sizeof(hdr), &br) != FR_OK) || (br != sizeof(hdr)))
    {
        (void)f_close(&fp);
        return 0U;
    }

    (void)f_close(&fp);

    if (pattern_sd_header_is_supported(&hdr) == 0U)
    {
        return 0U;
    }

    if (out_hdr != 0)
    {
        *out_hdr = hdr;
    }
    return 1U;
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
        if ((fr_open == FR_NO_FILE) || (fr_open == FR_NO_PATH))
        {
            pattern_sd_meta_cache_store(bank, pattern, 0U, 0U);
        }
        goto done;
    }

    if ((f_read(&fp, &hdr, sizeof(hdr), &br) != FR_OK) || (br != sizeof(hdr)))
    {
        (void)f_close(&fp);
        goto done;
    }

    if (pattern_sd_header_is_supported(&hdr) == 0U)
    {
        (void)f_close(&fp);
        goto done;
    }

    memset(out_pattern, 0, sizeof(*out_pattern));
    if ((f_read(&fp, out_pattern, hdr.payload_size, &br) != FR_OK) || (br != hdr.payload_size))
    {
        (void)f_close(&fp);
        goto done;
    }

    (void)f_close(&fp);

    if (pattern_sd_checksum((const uint8_t *)out_pattern, hdr.payload_size) != hdr.checksum)
    {
        goto done;
    }

    pattern_sd_meta_cache_store(bank, pattern, 1U, hdr.checksum);
    ok = 1U;

done:
    sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);
    return ok;
}

uint8_t pattern_sd_bank_store_slot(uint8_t bank, uint8_t pattern, const PatternSaveV1 *pattern_data)
{
    return pattern_sd_bank_store_slot_internal(bank, pattern, pattern_data, 1U);
}

uint8_t pattern_sd_bank_store_slot_nosync(uint8_t bank, uint8_t pattern, const PatternSaveV1 *pattern_data)
{
    return pattern_sd_bank_store_slot_internal(bank, pattern, pattern_data, 0U);
}

static uint8_t pattern_sd_write_payload_chunked(FIL *fp, const PatternSaveV1 *pattern_data)
{
    if ((fp == 0) || (pattern_data == 0))
    {
        return 0U;
    }

    const uint8_t *cursor = (const uint8_t *)pattern_data;
    uint32_t remaining = sizeof(*pattern_data);

    while (remaining != 0U)
    {
        const UINT chunk = (remaining > PATTERN_WRITE_CHUNK_BYTES)
            ? (UINT)PATTERN_WRITE_CHUNK_BYTES
            : (UINT)remaining;
        UINT bw = 0U;

        memcpy(g_pattern_write_chunk, cursor, chunk);

        sd_access_trace_begin("pattern_f_write_payload");
        const FRESULT fr = f_write(fp, g_pattern_write_chunk, chunk, &bw);
        sd_access_trace_end("pattern_f_write_payload", (int)fr, 0U);
        if ((fr != FR_OK) || (bw != chunk))
        {
            return 0U;
        }

        cursor += chunk;
        remaining -= (uint32_t)chunk;
    }

    return 1U;
}

static uint8_t pattern_sd_bank_store_slot_internal(uint8_t bank,
                                                   uint8_t pattern,
                                                   const PatternSaveV1 *pattern_data,
                                                   uint8_t do_sync)
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

    if (pattern_sd_write_payload_chunked(&fp, pattern_data) == 0U)
    {
        (void)f_close(&fp);
        goto done;
    }

    if (do_sync != 0U)
    {
        sd_access_trace_begin("pattern_f_sync");
        const FRESULT fr_sync = f_sync(&fp);
        sd_access_trace_end("pattern_f_sync", (int)fr_sync, 0U);
        if (fr_sync != FR_OK)
        {
            (void)f_close(&fp);
            goto done;
        }
    }

    (void)f_close(&fp);

    pattern_sd_meta_cache_store(bank, pattern, 1U, hdr.checksum);
    ok = 1U;

done:
    sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);
    return ok;
}

uint8_t pattern_sd_bank_delete_slot(uint8_t bank, uint8_t pattern)
{
    if (pattern_sd_slot_is_valid(bank, pattern) == 0U)
    {
        return 0U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PATTERN) == 0U)
    {
        return 0U;
    }

    uint8_t ok = 0U;
    char path[32];

    if ((pattern_sd_mount_if_needed() == 0U) || (pattern_sd_make_slot_path(path, sizeof(path), bank, pattern) == 0U))
    {
        goto done;
    }

    const FRESULT fr = f_unlink(path);
    if ((fr == FR_OK) || (fr == FR_NO_FILE) || (fr == FR_NO_PATH))
    {
        pattern_sd_meta_cache_store(bank, pattern, 0U, 0U);
        ok = 1U;
    }

done:
    sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);
    return ok;
}
