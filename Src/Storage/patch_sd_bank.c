#include "Storage/patch_sd_bank.h"

#include <stdio.h>
#include <string.h>

#include "Storage/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "ff.h"

static uint8_t g_patch_slot_has_data[PATCH_V1_SLOT_COUNT];
static uint8_t g_patch_slot_meta_valid[PATCH_V1_SLOT_COUNT];
static uint8_t g_patch_slot_invalid[PATCH_V1_SLOT_COUNT];
STORAGE_STATE_SDRAM static patch_v1_metadata_t g_patch_slot_meta[PATCH_V1_SLOT_COUNT];
static patch_sd_bank_error_t g_patch_sd_last_error;

static void patch_sd_set_error(patch_sd_bank_error_t err)
{
    g_patch_sd_last_error = err;
}

static uint8_t patch_sd_slot_is_valid(uint16_t slot)
{
    return (slot < PATCH_V1_SLOT_COUNT) ? 1U : 0U;
}

static uint32_t patch_sd_checksum(const uint8_t *data, uint32_t len)
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

static uint8_t patch_sd_make_slot_path(char *out_path, uint32_t out_size, uint16_t slot)
{
    if ((out_path == 0) || (out_size < 28U) || (slot >= PATCH_V1_SLOT_COUNT))
    {
        return 0U;
    }

    const int n = snprintf(out_path, out_size, "0:/BRICK/PATCH/P%04u.B6P", (unsigned)slot);
    return ((n > 0) && ((uint32_t)n < out_size)) ? 1U : 0U;
}

static uint8_t patch_sd_header_is_valid(const patch_sd_slot_header_t *hdr)
{
    if (hdr == 0)
    {
        return 0U;
    }

    return (uint8_t)((hdr->magic == PATCH_SD_FILE_MAGIC)
                     && (hdr->version == PATCH_SD_FILE_VERSION)
                     && (hdr->header_size == sizeof(patch_sd_slot_header_t))
                     && (hdr->payload_size == sizeof(PatchSaveV1)));
}

static void patch_sd_meta_store(uint16_t slot, const patch_sd_slot_header_t *hdr)
{
    if ((patch_sd_slot_is_valid(slot) == 0U) || (hdr == 0))
    {
        return;
    }

    g_patch_slot_has_data[slot] = 1U;
    g_patch_slot_meta_valid[slot] = 1U;
    g_patch_slot_invalid[slot] = 0U;
    memset(&g_patch_slot_meta[slot], 0, sizeof(g_patch_slot_meta[slot]));
    memcpy(g_patch_slot_meta[slot].name, hdr->name, sizeof(g_patch_slot_meta[slot].name));
    g_patch_slot_meta[slot].family = hdr->family;
    g_patch_slot_meta[slot].type = hdr->type;
    g_patch_slot_meta[slot].source_track = hdr->source_track;
    g_patch_slot_meta[slot].summary_family = hdr->summary_family;
    g_patch_slot_meta[slot].summary_type = hdr->summary_type;
}

static void patch_sd_meta_clear(uint16_t slot)
{
    if (patch_sd_slot_is_valid(slot) == 0U)
    {
        return;
    }

    g_patch_slot_has_data[slot] = 0U;
    g_patch_slot_meta_valid[slot] = 1U;
    g_patch_slot_invalid[slot] = 0U;
    memset(&g_patch_slot_meta[slot], 0, sizeof(g_patch_slot_meta[slot]));
}

static void patch_sd_meta_mark_invalid(uint16_t slot)
{
    if (patch_sd_slot_is_valid(slot) == 0U)
    {
        return;
    }

    g_patch_slot_has_data[slot] = 1U;
    g_patch_slot_meta_valid[slot] = 0U;
    g_patch_slot_invalid[slot] = 1U;
    memset(&g_patch_slot_meta[slot], 0, sizeof(g_patch_slot_meta[slot]));
}

static uint8_t patch_sd_read_header(uint16_t slot,
                                    patch_sd_slot_header_t *out_hdr,
                                    uint8_t *out_missing)
{
    FIL fp;
    UINT br = 0U;
    char path[32];

    if (out_missing != 0)
    {
        *out_missing = 0U;
    }
    if ((patch_sd_slot_is_valid(slot) == 0U) || (out_hdr == 0))
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_INVALID_SLOT);
        return 0U;
    }
    if (patch_sd_make_slot_path(path, sizeof(path), slot) == 0U)
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_PATH_FAIL);
        return 0U;
    }

    const FRESULT fr_open = f_open(&fp, path, FA_READ);
    if (fr_open != FR_OK)
    {
        if (((fr_open == FR_NO_FILE) || (fr_open == FR_NO_PATH)) && (out_missing != 0))
        {
            *out_missing = 1U;
        }
        patch_sd_set_error(PATCH_SD_BANK_ERR_OPEN_FAIL);
        return 0U;
    }

    const FRESULT fr_read = f_read(&fp, out_hdr, sizeof(*out_hdr), &br);
    (void)f_close(&fp);
    if ((fr_read != FR_OK) || (br != sizeof(*out_hdr)))
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_READ_FAIL);
        return 0U;
    }

    if (patch_sd_header_is_valid(out_hdr) == 0U)
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_INVALID_HEADER);
        return 0U;
    }

    patch_sd_set_error(PATCH_SD_BANK_ERR_NONE);
    return 1U;
}

static void patch_sd_scan_slots(void)
{
    (void)f_mkdir("0:/BRICK");
    (void)f_mkdir("0:/BRICK/PATCH");

    for (uint16_t slot = 0U; slot < PATCH_V1_SLOT_COUNT; ++slot)
    {
        patch_sd_slot_header_t hdr;
        uint8_t missing = 0U;
        g_patch_slot_has_data[slot] = 0U;
        g_patch_slot_meta_valid[slot] = 0U;
        memset(&g_patch_slot_meta[slot], 0, sizeof(g_patch_slot_meta[slot]));

        if (patch_sd_read_header(slot, &hdr, &missing) != 0U)
        {
            patch_sd_meta_store(slot, &hdr);
        }
        else if (missing != 0U)
        {
            patch_sd_meta_clear(slot);
        }
        else
        {
            patch_sd_meta_mark_invalid(slot);
        }
    }
    patch_sd_set_error(PATCH_SD_BANK_ERR_NONE);
}

void patch_sd_bank_init(void)
{
    memset(g_patch_slot_has_data, 0, sizeof(g_patch_slot_has_data));
    memset(g_patch_slot_meta_valid, 0, sizeof(g_patch_slot_meta_valid));
    memset(g_patch_slot_invalid, 0, sizeof(g_patch_slot_invalid));
    memset(g_patch_slot_meta, 0, sizeof(g_patch_slot_meta));
    patch_sd_set_error(PATCH_SD_BANK_ERR_NONE);

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PATCH) == 0U)
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_GATE_BUSY);
        return;
    }

    if (sd_access_fs_mount_if_needed() != 0U)
    {
        patch_sd_scan_slots();
    }
    else
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_MOUNT_FAIL);
    }

    sd_access_gate_release(SD_ACCESS_CLIENT_PATCH);
}

uint8_t patch_sd_bank_slot_has_data(uint16_t slot)
{
    if (patch_sd_slot_is_valid(slot) == 0U)
    {
        return 0U;
    }
    return g_patch_slot_has_data[slot];
}

patch_sd_slot_state_t patch_sd_bank_get_slot_state(uint16_t slot)
{
    if (patch_sd_slot_is_valid(slot) == 0U)
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_INVALID_SLOT);
        return PATCH_SD_SLOT_INVALID;
    }
    if (g_patch_slot_invalid[slot] != 0U)
    {
        return PATCH_SD_SLOT_INVALID;
    }
    if (g_patch_slot_has_data[slot] == 0U)
    {
        return PATCH_SD_SLOT_EMPTY;
    }
    return PATCH_SD_SLOT_VALID;
}

uint16_t patch_sd_bank_find_first_empty_slot(void)
{
    for (uint16_t slot = 0U; slot < PATCH_V1_SLOT_COUNT; ++slot)
    {
        if (g_patch_slot_has_data[slot] == 0U)
        {
            return slot;
        }
    }
    return PATCH_V1_INVALID_SLOT;
}

uint8_t patch_sd_bank_get_slot_metadata(uint16_t slot, patch_v1_metadata_t *out_meta)
{
    if ((patch_sd_slot_is_valid(slot) == 0U) || (out_meta == 0))
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_INVALID_ARG);
        return 0U;
    }

    if (g_patch_slot_has_data[slot] == 0U)
    {
        memset(out_meta, 0, sizeof(*out_meta));
        return 0U;
    }

    if (g_patch_slot_meta_valid[slot] == 0U)
    {
        if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PATCH) == 0U)
        {
            patch_sd_set_error(PATCH_SD_BANK_ERR_GATE_BUSY);
            return 0U;
        }

        patch_sd_slot_header_t hdr;
        uint8_t missing = 0U;
        if (sd_access_fs_mount_if_needed() == 0U)
        {
            patch_sd_set_error(PATCH_SD_BANK_ERR_MOUNT_FAIL);
            sd_access_gate_release(SD_ACCESS_CLIENT_PATCH);
            return 0U;
        }
        if (patch_sd_read_header(slot, &hdr, &missing) != 0U)
        {
            patch_sd_meta_store(slot, &hdr);
        }
        else if (missing != 0U)
        {
            patch_sd_meta_clear(slot);
            sd_access_gate_release(SD_ACCESS_CLIENT_PATCH);
            return 0U;
        }
        else
        {
            patch_sd_meta_mark_invalid(slot);
            sd_access_gate_release(SD_ACCESS_CLIENT_PATCH);
            return 0U;
        }
        sd_access_gate_release(SD_ACCESS_CLIENT_PATCH);
    }

    if (g_patch_slot_invalid[slot] != 0U)
    {
        return 0U;
    }

    memcpy(out_meta, &g_patch_slot_meta[slot], sizeof(*out_meta));
    return 1U;
}

uint8_t patch_sd_bank_store_slot(uint16_t slot, const PatchSaveV1 *patch)
{
    if ((patch_sd_slot_is_valid(slot) == 0U) || (patch == 0))
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_INVALID_ARG);
        return 0U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PATCH) == 0U)
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_GATE_BUSY);
        return 0U;
    }

    uint8_t ok = 0U;
    FIL fp;
    UINT bw = 0U;
    char path[32];
    patch_sd_slot_header_t hdr;

    if (sd_access_fs_mount_if_needed() == 0U)
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_MOUNT_FAIL);
        goto done;
    }
    (void)f_mkdir("0:/BRICK");
    (void)f_mkdir("0:/BRICK/PATCH");
    if (patch_sd_make_slot_path(path, sizeof(path), slot) == 0U)
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_PATH_FAIL);
        goto done;
    }

    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = PATCH_SD_FILE_MAGIC;
    hdr.version = PATCH_SD_FILE_VERSION;
    hdr.header_size = sizeof(hdr);
    hdr.payload_size = sizeof(*patch);
    hdr.family = patch->meta.family;
    hdr.type = patch->meta.type;
    hdr.source_track = patch->meta.source_track;
    hdr.summary_family = patch->meta.summary_family;
    hdr.summary_type = patch->meta.summary_type;
    memcpy(hdr.name, patch->meta.name, sizeof(hdr.name));
    hdr.checksum = patch_sd_checksum((const uint8_t *)patch, sizeof(*patch));

    const FRESULT fr_open = f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr_open != FR_OK)
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_OPEN_FAIL);
        goto done;
    }

    if ((f_write(&fp, &hdr, sizeof(hdr), &bw) != FR_OK) || (bw != sizeof(hdr)))
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_WRITE_FAIL);
        (void)f_close(&fp);
        goto done;
    }

    if ((f_write(&fp, patch, sizeof(*patch), &bw) != FR_OK) || (bw != sizeof(*patch)))
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_WRITE_FAIL);
        (void)f_close(&fp);
        goto done;
    }

    if (f_sync(&fp) != FR_OK)
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_SYNC_FAIL);
        (void)f_close(&fp);
        goto done;
    }
    (void)f_close(&fp);

    patch_sd_meta_store(slot, &hdr);
    patch_sd_set_error(PATCH_SD_BANK_ERR_NONE);
    ok = 1U;

done:
    sd_access_gate_release(SD_ACCESS_CLIENT_PATCH);
    return ok;
}

uint8_t patch_sd_bank_delete_slot(uint16_t slot)
{
    if (patch_sd_slot_is_valid(slot) == 0U)
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_INVALID_SLOT);
        return 0U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PATCH) == 0U)
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_GATE_BUSY);
        return 0U;
    }

    uint8_t ok = 0U;
    char path[32];

    if (sd_access_fs_mount_if_needed() == 0U)
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_MOUNT_FAIL);
        goto done;
    }
    if (patch_sd_make_slot_path(path, sizeof(path), slot) == 0U)
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_PATH_FAIL);
        goto done;
    }

    const FRESULT fr_unlink = f_unlink(path);
    if ((fr_unlink != FR_OK) && (fr_unlink != FR_NO_FILE) && (fr_unlink != FR_NO_PATH))
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_WRITE_FAIL);
        goto done;
    }

    patch_sd_meta_clear(slot);
    patch_sd_set_error(PATCH_SD_BANK_ERR_NONE);
    ok = 1U;

done:
    sd_access_gate_release(SD_ACCESS_CLIENT_PATCH);
    return ok;
}

uint8_t patch_sd_bank_rename_slot(uint16_t slot, const char *name)
{
    if ((patch_sd_slot_is_valid(slot) == 0U) || (name == 0))
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_INVALID_ARG);
        return 0U;
    }
    if (g_patch_slot_invalid[slot] != 0U)
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_INVALID_HEADER);
        return 0U;
    }

    PatchSaveV1 patch;
    if (patch_sd_bank_load_slot(slot, &patch) == 0U)
    {
        return 0U;
    }

    memset(patch.meta.name, 0, sizeof(patch.meta.name));
    (void)snprintf(patch.meta.name, sizeof(patch.meta.name), "%s", name);
    return patch_sd_bank_store_slot(slot, &patch);
}

uint8_t patch_sd_bank_load_slot(uint16_t slot, PatchSaveV1 *out_patch)
{
    if ((patch_sd_slot_is_valid(slot) == 0U) || (out_patch == 0))
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_INVALID_ARG);
        return 0U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PATCH) == 0U)
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_GATE_BUSY);
        return 0U;
    }

    uint8_t ok = 0U;
    FIL fp;
    UINT br = 0U;
    char path[32];
    patch_sd_slot_header_t hdr;

    if (sd_access_fs_mount_if_needed() == 0U)
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_MOUNT_FAIL);
        goto done;
    }
    if (patch_sd_make_slot_path(path, sizeof(path), slot) == 0U)
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_PATH_FAIL);
        goto done;
    }
    const FRESULT fr_open = f_open(&fp, path, FA_READ);
    if (fr_open != FR_OK)
    {
        if ((fr_open == FR_NO_FILE) || (fr_open == FR_NO_PATH))
        {
            patch_sd_meta_clear(slot);
        }
        patch_sd_set_error(PATCH_SD_BANK_ERR_OPEN_FAIL);
        goto done;
    }
    if ((f_read(&fp, &hdr, sizeof(hdr), &br) != FR_OK) || (br != sizeof(hdr)))
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_READ_FAIL);
        (void)f_close(&fp);
        goto done;
    }
    if (patch_sd_header_is_valid(&hdr) == 0U)
    {
        patch_sd_meta_mark_invalid(slot);
        patch_sd_set_error(PATCH_SD_BANK_ERR_INVALID_HEADER);
        (void)f_close(&fp);
        goto done;
    }
    if ((f_read(&fp, out_patch, sizeof(*out_patch), &br) != FR_OK) || (br != sizeof(*out_patch)))
    {
        patch_sd_set_error(PATCH_SD_BANK_ERR_READ_FAIL);
        (void)f_close(&fp);
        goto done;
    }
    (void)f_close(&fp);

    if (patch_sd_checksum((const uint8_t *)out_patch, sizeof(*out_patch)) != hdr.checksum)
    {
        patch_sd_meta_mark_invalid(slot);
        patch_sd_set_error(PATCH_SD_BANK_ERR_CHECKSUM_FAIL);
        goto done;
    }

    patch_sd_meta_store(slot, &hdr);
    patch_sd_set_error(PATCH_SD_BANK_ERR_NONE);
    ok = 1U;

done:
    sd_access_gate_release(SD_ACCESS_CLIENT_PATCH);
    return ok;
}

patch_sd_bank_error_t patch_sd_bank_get_last_error(void)
{
    return g_patch_sd_last_error;
}

const char *patch_sd_bank_error_to_string(patch_sd_bank_error_t err)
{
    switch (err)
    {
        case PATCH_SD_BANK_ERR_NONE: return "NONE";
        case PATCH_SD_BANK_ERR_INVALID_SLOT: return "INVALID_SLOT";
        case PATCH_SD_BANK_ERR_INVALID_ARG: return "INVALID_ARG";
        case PATCH_SD_BANK_ERR_GATE_BUSY: return "GATE_BUSY";
        case PATCH_SD_BANK_ERR_MOUNT_FAIL: return "MOUNT_FAIL";
        case PATCH_SD_BANK_ERR_PATH_FAIL: return "PATH_FAIL";
        case PATCH_SD_BANK_ERR_OPEN_FAIL: return "OPEN_FAIL";
        case PATCH_SD_BANK_ERR_READ_FAIL: return "READ_FAIL";
        case PATCH_SD_BANK_ERR_WRITE_FAIL: return "WRITE_FAIL";
        case PATCH_SD_BANK_ERR_SYNC_FAIL: return "SYNC_FAIL";
        case PATCH_SD_BANK_ERR_INVALID_HEADER: return "INVALID_HEADER";
        case PATCH_SD_BANK_ERR_CHECKSUM_FAIL: return "CHECKSUM_FAIL";
        default: return "UNKNOWN";
    }
}
