#include "Storage/kit_sd_bank.h"

#include <stdio.h>
#include <string.h>

#include "Storage/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "ff.h"

static uint8_t g_kit_slot_has_data[KIT_V1_SLOT_COUNT];
static uint8_t g_kit_slot_meta_valid[KIT_V1_SLOT_COUNT];
static uint8_t g_kit_slot_invalid[KIT_V1_SLOT_COUNT];
STORAGE_STATE_SDRAM static kit_v1_metadata_t g_kit_slot_meta[KIT_V1_SLOT_COUNT];
STORAGE_STATE_SDRAM static KitSaveV1 g_kit_sd_work;
static kit_sd_bank_error_t g_kit_sd_last_error;

static void kit_sd_set_error(kit_sd_bank_error_t err)
{
    g_kit_sd_last_error = err;
}

static uint8_t kit_sd_slot_is_valid(uint16_t slot)
{
    return (slot < KIT_V1_SLOT_COUNT) ? 1U : 0U;
}

static uint32_t kit_sd_checksum(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 5381UL;
    if (data == 0)
    {
        return 0U;
    }
    for (uint32_t i = 0U; i < len; ++i)
    {
        crc = ((crc << 5) + crc) ^ data[i];
    }
    return crc;
}

static uint8_t kit_sd_make_slot_path(char *out_path, uint32_t out_size, uint16_t slot)
{
    if ((out_path == 0) || (out_size < 26U) || (slot >= KIT_V1_SLOT_COUNT))
    {
        return 0U;
    }

    const int n = snprintf(out_path, out_size, "0:/BRICK/KIT/K%04u.B6K", (unsigned)slot);
    return ((n > 0) && ((uint32_t)n < out_size)) ? 1U : 0U;
}

static uint8_t kit_sd_header_is_valid(const kit_sd_slot_header_t *hdr)
{
    if (hdr == 0)
    {
        return 0U;
    }

    return (uint8_t)((hdr->magic == KIT_SD_FILE_MAGIC)
                     && (hdr->version == KIT_SD_FILE_VERSION)
                     && (hdr->header_size == sizeof(kit_sd_slot_header_t))
                     && (hdr->payload_size == sizeof(KitSaveV1))
                     && (hdr->track_count <= KIT_V1_TRACK_MAX));
}

static void kit_sd_meta_store(uint16_t slot, const kit_sd_slot_header_t *hdr)
{
    if ((kit_sd_slot_is_valid(slot) == 0U) || (hdr == 0))
    {
        return;
    }

    g_kit_slot_has_data[slot] = 1U;
    g_kit_slot_meta_valid[slot] = 1U;
    g_kit_slot_invalid[slot] = 0U;
    memset(&g_kit_slot_meta[slot], 0, sizeof(g_kit_slot_meta[slot]));
    memcpy(g_kit_slot_meta[slot].name, hdr->name, sizeof(g_kit_slot_meta[slot].name));
    g_kit_slot_meta[slot].track_count = hdr->track_count;
    memcpy(g_kit_slot_meta[slot].summary, hdr->summary, sizeof(g_kit_slot_meta[slot].summary));
}

static void kit_sd_meta_clear(uint16_t slot)
{
    if (kit_sd_slot_is_valid(slot) == 0U)
    {
        return;
    }

    g_kit_slot_has_data[slot] = 0U;
    g_kit_slot_meta_valid[slot] = 1U;
    g_kit_slot_invalid[slot] = 0U;
    memset(&g_kit_slot_meta[slot], 0, sizeof(g_kit_slot_meta[slot]));
}

static void kit_sd_meta_mark_invalid(uint16_t slot)
{
    if (kit_sd_slot_is_valid(slot) == 0U)
    {
        return;
    }

    g_kit_slot_has_data[slot] = 1U;
    g_kit_slot_meta_valid[slot] = 0U;
    g_kit_slot_invalid[slot] = 1U;
    memset(&g_kit_slot_meta[slot], 0, sizeof(g_kit_slot_meta[slot]));
}

static uint8_t kit_sd_read_header(uint16_t slot,
                                  kit_sd_slot_header_t *out_hdr,
                                  uint8_t *out_missing)
{
    FIL fp;
    UINT br = 0U;
    char path[32];

    if (out_missing != 0)
    {
        *out_missing = 0U;
    }
    if ((kit_sd_slot_is_valid(slot) == 0U) || (out_hdr == 0))
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_INVALID_SLOT);
        return 0U;
    }
    if (kit_sd_make_slot_path(path, sizeof(path), slot) == 0U)
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_PATH_FAIL);
        return 0U;
    }

    const FRESULT fr_open = f_open(&fp, path, FA_READ);
    if (fr_open != FR_OK)
    {
        if (((fr_open == FR_NO_FILE) || (fr_open == FR_NO_PATH)) && (out_missing != 0))
        {
            *out_missing = 1U;
        }
        kit_sd_set_error(KIT_SD_BANK_ERR_OPEN_FAIL);
        return 0U;
    }

    const FRESULT fr_read = f_read(&fp, out_hdr, sizeof(*out_hdr), &br);
    (void)f_close(&fp);
    if ((fr_read != FR_OK) || (br != sizeof(*out_hdr)))
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_READ_FAIL);
        return 0U;
    }

    if (kit_sd_header_is_valid(out_hdr) == 0U)
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_INVALID_HEADER);
        return 0U;
    }

    kit_sd_set_error(KIT_SD_BANK_ERR_NONE);
    return 1U;
}

static void kit_sd_scan_slots(void)
{
    (void)f_mkdir("0:/BRICK");
    (void)f_mkdir("0:/BRICK/KIT");

    for (uint16_t slot = 0U; slot < KIT_V1_SLOT_COUNT; ++slot)
    {
        kit_sd_slot_header_t hdr;
        uint8_t missing = 0U;
        g_kit_slot_has_data[slot] = 0U;
        g_kit_slot_meta_valid[slot] = 0U;
        g_kit_slot_invalid[slot] = 0U;
        memset(&g_kit_slot_meta[slot], 0, sizeof(g_kit_slot_meta[slot]));

        if (kit_sd_read_header(slot, &hdr, &missing) != 0U)
        {
            kit_sd_meta_store(slot, &hdr);
        }
        else if (missing != 0U)
        {
            kit_sd_meta_clear(slot);
        }
        else
        {
            kit_sd_meta_mark_invalid(slot);
        }
    }
    kit_sd_set_error(KIT_SD_BANK_ERR_NONE);
}

void kit_sd_bank_init(void)
{
    memset(g_kit_slot_has_data, 0, sizeof(g_kit_slot_has_data));
    memset(g_kit_slot_meta_valid, 0, sizeof(g_kit_slot_meta_valid));
    memset(g_kit_slot_invalid, 0, sizeof(g_kit_slot_invalid));
    memset(g_kit_slot_meta, 0, sizeof(g_kit_slot_meta));
    kit_sd_set_error(KIT_SD_BANK_ERR_NONE);

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_KIT) == 0U)
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_GATE_BUSY);
        return;
    }

    if (sd_access_fs_mount_if_needed() != 0U)
    {
        kit_sd_scan_slots();
    }
    else
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_MOUNT_FAIL);
    }

    sd_access_gate_release(SD_ACCESS_CLIENT_KIT);
}

uint8_t kit_sd_bank_slot_has_data(uint16_t slot)
{
    if (kit_sd_slot_is_valid(slot) == 0U)
    {
        return 0U;
    }
    return g_kit_slot_has_data[slot];
}

kit_sd_slot_state_t kit_sd_bank_get_slot_state(uint16_t slot)
{
    if (kit_sd_slot_is_valid(slot) == 0U)
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_INVALID_SLOT);
        return KIT_SD_SLOT_INVALID;
    }
    if (g_kit_slot_invalid[slot] != 0U)
    {
        return KIT_SD_SLOT_INVALID;
    }
    if (g_kit_slot_has_data[slot] == 0U)
    {
        return KIT_SD_SLOT_EMPTY;
    }
    return KIT_SD_SLOT_VALID;
}

uint16_t kit_sd_bank_find_first_empty_slot(void)
{
    for (uint16_t slot = 0U; slot < KIT_V1_SLOT_COUNT; ++slot)
    {
        if (g_kit_slot_has_data[slot] == 0U)
        {
            return slot;
        }
    }
    return KIT_V1_INVALID_SLOT;
}

uint8_t kit_sd_bank_get_slot_metadata(uint16_t slot, kit_v1_metadata_t *out_meta)
{
    if ((kit_sd_slot_is_valid(slot) == 0U) || (out_meta == 0))
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_INVALID_ARG);
        return 0U;
    }

    if (g_kit_slot_has_data[slot] == 0U)
    {
        memset(out_meta, 0, sizeof(*out_meta));
        return 0U;
    }

    if (g_kit_slot_meta_valid[slot] == 0U)
    {
        if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_KIT) == 0U)
        {
            kit_sd_set_error(KIT_SD_BANK_ERR_GATE_BUSY);
            return 0U;
        }

        kit_sd_slot_header_t hdr;
        uint8_t missing = 0U;
        if (sd_access_fs_mount_if_needed() == 0U)
        {
            kit_sd_set_error(KIT_SD_BANK_ERR_MOUNT_FAIL);
            sd_access_gate_release(SD_ACCESS_CLIENT_KIT);
            return 0U;
        }
        if (kit_sd_read_header(slot, &hdr, &missing) != 0U)
        {
            kit_sd_meta_store(slot, &hdr);
        }
        else if (missing != 0U)
        {
            kit_sd_meta_clear(slot);
            sd_access_gate_release(SD_ACCESS_CLIENT_KIT);
            return 0U;
        }
        else
        {
            kit_sd_meta_mark_invalid(slot);
            sd_access_gate_release(SD_ACCESS_CLIENT_KIT);
            return 0U;
        }
        sd_access_gate_release(SD_ACCESS_CLIENT_KIT);
    }

    if (g_kit_slot_invalid[slot] != 0U)
    {
        return 0U;
    }

    memcpy(out_meta, &g_kit_slot_meta[slot], sizeof(*out_meta));
    return 1U;
}

uint8_t kit_sd_bank_store_slot(uint16_t slot, const KitSaveV1 *kit)
{
    if ((kit_sd_slot_is_valid(slot) == 0U) || (kit == 0))
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_INVALID_ARG);
        return 0U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_KIT) == 0U)
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_GATE_BUSY);
        return 0U;
    }

    uint8_t ok = 0U;
    FIL fp;
    UINT bw = 0U;
    char path[32];
    kit_sd_slot_header_t hdr;

    if (sd_access_fs_mount_if_needed() == 0U)
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_MOUNT_FAIL);
        goto done;
    }
    (void)f_mkdir("0:/BRICK");
    (void)f_mkdir("0:/BRICK/KIT");
    if (kit_sd_make_slot_path(path, sizeof(path), slot) == 0U)
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_PATH_FAIL);
        goto done;
    }

    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = KIT_SD_FILE_MAGIC;
    hdr.version = KIT_SD_FILE_VERSION;
    hdr.header_size = sizeof(hdr);
    hdr.payload_size = sizeof(*kit);
    hdr.track_count = kit->meta.track_count;
    memcpy(hdr.name, kit->meta.name, sizeof(hdr.name));
    memcpy(hdr.summary, kit->meta.summary, sizeof(hdr.summary));
    hdr.checksum = kit_sd_checksum((const uint8_t *)kit, sizeof(*kit));

    const FRESULT fr_open = f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr_open != FR_OK)
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_OPEN_FAIL);
        goto done;
    }

    if ((f_write(&fp, &hdr, sizeof(hdr), &bw) != FR_OK) || (bw != sizeof(hdr)))
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_WRITE_FAIL);
        (void)f_close(&fp);
        goto done;
    }

    if ((f_write(&fp, kit, sizeof(*kit), &bw) != FR_OK) || (bw != sizeof(*kit)))
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_WRITE_FAIL);
        (void)f_close(&fp);
        goto done;
    }

    if (f_sync(&fp) != FR_OK)
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_SYNC_FAIL);
        (void)f_close(&fp);
        goto done;
    }
    (void)f_close(&fp);

    kit_sd_meta_store(slot, &hdr);
    kit_sd_set_error(KIT_SD_BANK_ERR_NONE);
    ok = 1U;

done:
    sd_access_gate_release(SD_ACCESS_CLIENT_KIT);
    return ok;
}

uint8_t kit_sd_bank_load_slot(uint16_t slot, KitSaveV1 *out_kit)
{
    if ((kit_sd_slot_is_valid(slot) == 0U) || (out_kit == 0))
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_INVALID_ARG);
        return 0U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_KIT) == 0U)
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_GATE_BUSY);
        return 0U;
    }

    uint8_t ok = 0U;
    FIL fp;
    UINT br = 0U;
    char path[32];
    kit_sd_slot_header_t hdr;

    if (sd_access_fs_mount_if_needed() == 0U)
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_MOUNT_FAIL);
        goto done;
    }
    if (kit_sd_make_slot_path(path, sizeof(path), slot) == 0U)
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_PATH_FAIL);
        goto done;
    }
    const FRESULT fr_open = f_open(&fp, path, FA_READ);
    if (fr_open != FR_OK)
    {
        if ((fr_open == FR_NO_FILE) || (fr_open == FR_NO_PATH))
        {
            kit_sd_meta_clear(slot);
        }
        kit_sd_set_error(KIT_SD_BANK_ERR_OPEN_FAIL);
        goto done;
    }
    if ((f_read(&fp, &hdr, sizeof(hdr), &br) != FR_OK) || (br != sizeof(hdr)))
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_READ_FAIL);
        (void)f_close(&fp);
        goto done;
    }
    if (kit_sd_header_is_valid(&hdr) == 0U)
    {
        kit_sd_meta_mark_invalid(slot);
        kit_sd_set_error(KIT_SD_BANK_ERR_INVALID_HEADER);
        (void)f_close(&fp);
        goto done;
    }
    if ((f_read(&fp, out_kit, sizeof(*out_kit), &br) != FR_OK) || (br != sizeof(*out_kit)))
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_READ_FAIL);
        (void)f_close(&fp);
        goto done;
    }
    (void)f_close(&fp);

    if (kit_sd_checksum((const uint8_t *)out_kit, sizeof(*out_kit)) != hdr.checksum)
    {
        kit_sd_meta_mark_invalid(slot);
        kit_sd_set_error(KIT_SD_BANK_ERR_CHECKSUM_FAIL);
        goto done;
    }

    kit_sd_meta_store(slot, &hdr);
    kit_sd_set_error(KIT_SD_BANK_ERR_NONE);
    ok = 1U;

done:
    sd_access_gate_release(SD_ACCESS_CLIENT_KIT);
    return ok;
}

uint8_t kit_sd_bank_delete_slot(uint16_t slot)
{
    if (kit_sd_slot_is_valid(slot) == 0U)
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_INVALID_SLOT);
        return 0U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_KIT) == 0U)
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_GATE_BUSY);
        return 0U;
    }

    uint8_t ok = 0U;
    char path[32];

    if (sd_access_fs_mount_if_needed() == 0U)
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_MOUNT_FAIL);
        goto done;
    }
    if (kit_sd_make_slot_path(path, sizeof(path), slot) == 0U)
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_PATH_FAIL);
        goto done;
    }

    const FRESULT fr_unlink = f_unlink(path);
    if ((fr_unlink != FR_OK) && (fr_unlink != FR_NO_FILE) && (fr_unlink != FR_NO_PATH))
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_WRITE_FAIL);
        goto done;
    }

    kit_sd_meta_clear(slot);
    kit_sd_set_error(KIT_SD_BANK_ERR_NONE);
    ok = 1U;

done:
    sd_access_gate_release(SD_ACCESS_CLIENT_KIT);
    return ok;
}

uint8_t kit_sd_bank_rename_slot(uint16_t slot, const char *name)
{
    if ((kit_sd_slot_is_valid(slot) == 0U) || (name == 0))
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_INVALID_ARG);
        return 0U;
    }
    if (g_kit_slot_invalid[slot] != 0U)
    {
        kit_sd_set_error(KIT_SD_BANK_ERR_INVALID_HEADER);
        return 0U;
    }

    if (kit_sd_bank_load_slot(slot, &g_kit_sd_work) == 0U)
    {
        return 0U;
    }

    memset(g_kit_sd_work.meta.name, 0, sizeof(g_kit_sd_work.meta.name));
    (void)snprintf(g_kit_sd_work.meta.name, sizeof(g_kit_sd_work.meta.name), "%s", name);
    return kit_sd_bank_store_slot(slot, &g_kit_sd_work);
}

kit_sd_bank_error_t kit_sd_bank_get_last_error(void)
{
    return g_kit_sd_last_error;
}

const char *kit_sd_bank_error_to_string(kit_sd_bank_error_t err)
{
    switch (err)
    {
        case KIT_SD_BANK_ERR_NONE: return "NONE";
        case KIT_SD_BANK_ERR_INVALID_SLOT: return "INVALID_SLOT";
        case KIT_SD_BANK_ERR_INVALID_ARG: return "INVALID_ARG";
        case KIT_SD_BANK_ERR_GATE_BUSY: return "GATE_BUSY";
        case KIT_SD_BANK_ERR_MOUNT_FAIL: return "MOUNT_FAIL";
        case KIT_SD_BANK_ERR_PATH_FAIL: return "PATH_FAIL";
        case KIT_SD_BANK_ERR_OPEN_FAIL: return "OPEN_FAIL";
        case KIT_SD_BANK_ERR_READ_FAIL: return "READ_FAIL";
        case KIT_SD_BANK_ERR_WRITE_FAIL: return "WRITE_FAIL";
        case KIT_SD_BANK_ERR_SYNC_FAIL: return "SYNC_FAIL";
        case KIT_SD_BANK_ERR_INVALID_HEADER: return "INVALID_HEADER";
        case KIT_SD_BANK_ERR_CHECKSUM_FAIL: return "CHECKSUM_FAIL";
        default: return "UNKNOWN";
    }
}
