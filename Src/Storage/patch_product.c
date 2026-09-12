#include "Storage/patch_product.h"

#include <stdio.h>
#include <string.h>

#include "App/name_contract.h"
#include "Platform/memory_layout.h"
#include "SD/sd_scheduler_runtime.h"
#include "Sampler/sampler_ram_pool.h"
#include "Storage/persistent_fatfs_io.h"
#include "Storage/persistent_key_catalog.h"
#include "Storage/persistent_patch_control.h"
#include "Storage/project_control.h"
#include "Storage/project_load_quiesce.h"
#include "Storage/project_product.h"
#include "Storage/sd_access_gate.h"
#include "Track/track_catalog.h"
#include "Track/entity_topology.h"
#include "Track/track_state.h"
#include "ff.h"

static uint8_t g_present[PATCH_PRODUCT_SLOT_COUNT];
static uint8_t g_invalid[PATCH_PRODUCT_SLOT_COUNT];
STORAGE_STATE_SDRAM static patch_product_metadata_t g_meta[PATCH_PRODUCT_SLOT_COUNT];
static uint16_t g_current = PATCH_PRODUCT_INVALID_SLOT;
#define PATCH_PRODUCT_SECTION_BODY 0x3001U
#define PATCH_PRODUCT_IO_BUFFER_BYTES (16U * 1024U)

typedef enum
{
    PATCH_IO_IDLE = 0,
    PATCH_IO_MOUNT,
    PATCH_IO_MKDIR_BRICK,
    PATCH_IO_MKDIR_PATCH,
    PATCH_IO_RECOVER,
    PATCH_IO_OPEN_READ,
    PATCH_IO_READ,
    PATCH_IO_CLOSE_READ,
    PATCH_IO_DECODE,
    PATCH_IO_PREPARE_LOAD,
    PATCH_IO_WAIT_ASSET,
    PATCH_IO_ENCODE,
    PATCH_IO_OPEN_WRITE,
    PATCH_IO_WRITE,
    PATCH_IO_SYNC,
    PATCH_IO_CLOSE_WRITE,
    PATCH_IO_COMMIT,
    PATCH_IO_CLOSE_READ_FAILED,
    PATCH_IO_CLOSE_WRITE_FAILED,
    PATCH_IO_CLEAN_TEMP,
    PATCH_IO_DONE
} patch_io_state_t;

typedef struct
{
    patch_io_state_t state;
    patch_product_operation_t operation;
    patch_product_result_t result;
    uint8_t result_ready;
    uint8_t prepared;
    uint8_t read_open;
    uint8_t write_open;
    uint16_t slot;
    uint16_t target_mask;
    uint16_t prepared_slot;
    uint32_t media_epoch;
    uint32_t file_size;
    uint32_t file_offset;
    uint32_t encoded_size;
    char requested_name[NAME_CONTRACT_BUFFER_BYTES];
    char final_path[48];
    char temporary_path[56];
    char backup_path[56];
    persist_control_patch_t prepared_patch;
    persist_control_patch_t patch;
    persist_codec_patch_staging_t decoded;
    persistent_fatfs_file_t file;
    uint8_t io_buffer[PATCH_PRODUCT_IO_BUFFER_BYTES];
} patch_io_runtime_t;

STORAGE_STATE_SDRAM static patch_io_runtime_t g_patch_io;

typedef struct
{
    const uint8_t *data;
    uint32_t size;
    uint32_t offset;
} patch_memory_source_t;

static uint32_t crc32(uint32_t crc, const uint8_t *data, uint32_t length)
{
    for (uint32_t i = 0U; i < length; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit)
        {
            crc = (crc >> 1U)
                ^ (0xEDB88320UL & ((uint32_t)-(int32_t)(crc & 1U)));
        }
    }
    return crc;
}

static uint8_t path(char *out, uint32_t size, uint16_t slot)
{
    const int written = snprintf(out, size, "0:/BRICK/PATCH/P%04u.B6C", slot);
    return (written > 0) && ((uint32_t)written < size);
}

static uint8_t side_path(char *out, uint32_t size, const char *final_path,
                         const char *suffix)
{
    const int written = snprintf(out, size, "%s.%s", final_path, suffix);
    return (written > 0) && ((uint32_t)written < size);
}

static uint8_t acquire(void)
{
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PATCH) == 0U)
    {
        return 0U;
    }
    if (sd_access_fs_mount_if_needed() == 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PATCH);
        return 0U;
    }
    return 1U;
}

static void meta_from_patch(uint16_t slot, const persist_control_patch_t *patch)
{
    patch_product_metadata_t *const meta = &g_meta[slot];
    memset(meta, 0, sizeof(*meta));
    uint16_t length = patch->name_length;
    if (length > NAME_CONTRACT_MAX_CHARS)
    {
        length = NAME_CONTRACT_MAX_CHARS;
    }
    memcpy(meta->name, patch->name, length);

    track_family_t family;
    track_type_t type;
    if (persist_key_family_from_disk(patch->family, &family) != 0U)
    {
        meta->family = (uint8_t)family;
    }
    if (persist_key_type_from_disk(patch->type, &type) != 0U)
    {
        meta->type = (uint8_t)type;
    }
    meta->summary_family = meta->family;
    meta->summary_type = meta->type;
}

static uint16_t le16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static uint32_t le32(const uint8_t *data)
{
    return (uint32_t)data[0]
        | ((uint32_t)data[1] << 8U)
        | ((uint32_t)data[2] << 16U)
        | ((uint32_t)data[3] << 24U);
}

static uint8_t scan_meta(uint16_t slot)
{
    char final_path[48];
    FIL file;
    UINT transferred = 0U;
    uint8_t header[PERSIST_CODEC_HEADER_BYTES];
    if ((path(final_path, sizeof(final_path), slot) == 0U)
            || (f_open(&file, final_path, FA_READ) != FR_OK))
    {
        return 0U;
    }

    uint8_t valid = (f_read(&file, header, sizeof(header), &transferred) == FR_OK)
        && (transferred == sizeof(header));
    const uint32_t total = valid ? le32(&header[12]) : 0U;
    const uint32_t header_crc = valid ? le32(&header[20]) : 0U;
    valid = valid
        && (header[0] == 'B') && (header[1] == '6')
        && (header[2] == 'P') && (header[3] == 'C')
        && (header[4] == PERSIST_CODEC_VERSION) && (header[5] == 0U)
        && (header[6] == PERSIST_CODEC_DOCUMENT_PATCH) && (header[7] == 0U)
        && (le16(&header[8]) == 1U) && (header[10] == 0U) && (header[11] == 0U)
        && (total == (uint32_t)f_size(&file))
        && (total <= PERSIST_CODEC_MAX_DOCUMENT_BYTES)
        && (total >= PERSIST_CODEC_HEADER_BYTES + PERSIST_CODEC_SECTION_HEADER_BYTES + 10U)
        && (header_crc == ~crc32(0xFFFFFFFFUL, header, 20U));

    uint8_t section_header[PERSIST_CODEC_SECTION_HEADER_BYTES];
    if (valid)
    {
        valid = (f_read(&file, section_header, sizeof(section_header), &transferred) == FR_OK)
            && (transferred == sizeof(section_header));
    }
    const uint32_t section_length = valid ? le32(&section_header[4]) : 0U;
    valid = valid
        && (le16(section_header) == PATCH_PRODUCT_SECTION_BODY)
        && (le16(&section_header[2]) == 1U)
        && (section_length == total - PERSIST_CODEC_HEADER_BYTES
            - PERSIST_CODEC_SECTION_HEADER_BYTES)
        && (section_length >= 10U);

    uint8_t name_length_bytes[2];
    if (valid)
    {
        valid = (f_read(&file, name_length_bytes, sizeof(name_length_bytes), &transferred) == FR_OK)
            && (transferred == sizeof(name_length_bytes));
    }
    const uint16_t name_length = valid ? le16(name_length_bytes) : 0U;
    valid = valid
        && (name_length <= PERSIST_CONTROL_PATCH_NAME_BYTES)
        && (section_length >= (uint32_t)name_length + 10U);
    if (valid)
    {
        uint8_t prefix[PERSIST_CONTROL_PATCH_NAME_BYTES + 8U];
        valid = (f_read(&file, prefix, (UINT)(name_length + 8U), &transferred) == FR_OK)
            && (transferred == (UINT)(name_length + 8U));
        if (valid)
        {
            persist_control_patch_t patch;
            memset(&patch, 0, sizeof(patch));
            patch.name_length = name_length;
            memcpy(patch.name, prefix, name_length);
            patch.family = le32(&prefix[name_length]);
            patch.type = le32(&prefix[name_length + 4U]);
            track_family_t family;
            track_type_t type;
            valid = (persist_key_family_from_disk(patch.family, &family) != 0U)
                && (persist_key_type_from_disk(patch.type, &type) != 0U);
            if (valid)
            {
                meta_from_patch(slot, &patch);
            }
        }
    }
    (void)f_close(&file);
    g_present[slot] = 1U;
    g_invalid[slot] = (valid != 0U) ? 0U : 1U;
    return valid;
}

static uint8_t patch_name_normalize(const char *name,
                                    char output[NAME_CONTRACT_BUFFER_BYTES])
{
    return (name_contract_normalize(name, output) == NAME_CONTRACT_RESULT_OK) ? 1U : 0U;
}

static uint8_t patch_name_is_canonical(const persist_control_patch_t *patch)
{
    if ((patch == 0) || (patch->name_length == 0U)
            || (patch->name_length > PERSIST_CONTROL_PATCH_NAME_BYTES))
    {
        return 0U;
    }

    char source[NAME_CONTRACT_BUFFER_BYTES] = { 0 };
    memcpy(source, patch->name, patch->name_length);
    char normalized[NAME_CONTRACT_BUFFER_BYTES];
    if (name_contract_normalize(source, normalized) != NAME_CONTRACT_RESULT_OK)
    {
        return 0U;
    }
    return (strlen(normalized) == patch->name_length)
        && (memcmp(normalized, source, NAME_CONTRACT_BUFFER_BYTES) == 0);
}

static uint8_t patch_io_busy(void)
{
    return (g_patch_io.state != PATCH_IO_IDLE)
        || (g_patch_io.result_ready != 0U)
        || (g_patch_io.prepared != 0U);
}

static uint8_t patch_io_common_available(void)
{
    return (patch_io_busy() == 0U)
        && (project_product_save_busy() == 0U)
        && (project_product_load_busy() == 0U)
        && (project_replacement_is_active() == 0U);
}

static uint8_t patch_io_common_available_for_prepared_save(void)
{
    return (g_patch_io.state == PATCH_IO_IDLE)
        && (g_patch_io.result_ready == 0U)
        && (project_product_save_busy() == 0U)
        && (project_product_load_busy() == 0U)
        && (project_replacement_is_active() == 0U);
}

static uint8_t patch_io_prepare_paths(uint16_t slot)
{
    return path(g_patch_io.final_path, sizeof(g_patch_io.final_path), slot)
        && side_path(g_patch_io.temporary_path, sizeof(g_patch_io.temporary_path),
                     g_patch_io.final_path, "TMP")
        && side_path(g_patch_io.backup_path, sizeof(g_patch_io.backup_path),
                     g_patch_io.final_path, "BAK");
}

static void patch_io_start(patch_product_operation_t operation, uint16_t slot)
{
    const uint32_t media_epoch = sd_access_media_epoch();
    g_patch_io.state = PATCH_IO_MOUNT;
    g_patch_io.operation = operation;
    g_patch_io.result = PATCH_PRODUCT_PENDING;
    g_patch_io.result_ready = 0U;
    g_patch_io.slot = slot;
    g_patch_io.media_epoch = media_epoch;
    g_patch_io.file_size = 0U;
    g_patch_io.file_offset = 0U;
    g_patch_io.encoded_size = 0U;
    g_patch_io.read_open = 0U;
    g_patch_io.write_open = 0U;
}

static void patch_io_finish(patch_product_result_t result)
{
    g_patch_io.result = result;
    g_patch_io.result_ready = 1U;
    g_patch_io.state = PATCH_IO_DONE;

    if (result != PATCH_PRODUCT_OK)
    {
        return;
    }
    g_present[g_patch_io.slot] = 1U;
    g_invalid[g_patch_io.slot] = 0U;
    meta_from_patch(g_patch_io.slot, &g_patch_io.patch);
    if (g_patch_io.operation == PATCH_PRODUCT_OPERATION_SAVE)
    {
        g_current = g_patch_io.slot;
    }
}

static void patch_io_fail(patch_product_result_t result)
{
    if (g_patch_io.result != PATCH_PRODUCT_PENDING)
    {
        return;
    }
    g_patch_io.result = result;
    if ((g_patch_io.operation == PATCH_PRODUCT_OPERATION_LOAD)
            && (g_patch_io.read_open == 0U))
    {
        patch_io_finish(result);
        return;
    }
    if (g_patch_io.read_open != 0U)
    {
        g_patch_io.state = PATCH_IO_CLOSE_READ_FAILED;
    }
    else if (g_patch_io.write_open != 0U)
    {
        g_patch_io.state = PATCH_IO_CLOSE_WRITE_FAILED;
    }
    else
    {
        g_patch_io.state = PATCH_IO_CLEAN_TEMP;
    }
}

static uint8_t patch_memory_read(void *context, uint8_t *data, uint32_t length)
{
    patch_memory_source_t *const source = context;
    if ((source == 0) || (data == 0) || (length > source->size - source->offset))
    {
        return 0U;
    }
    memcpy(data, &source->data[source->offset], length);
    source->offset += length;
    return 1U;
}

static uint8_t patch_memory_reset(void *context)
{
    patch_memory_source_t *const source = context;
    if (source == 0)
    {
        return 0U;
    }
    source->offset = 0U;
    return 1U;
}

static uint8_t patch_memory_size(void *context, uint32_t *size)
{
    patch_memory_source_t *const source = context;
    if ((source == 0) || (size == 0))
    {
        return 0U;
    }
    *size = source->size;
    return 1U;
}

static uint8_t patch_memory_write(void *context, const uint8_t *data,
                                  uint32_t length)
{
    if ((context == 0) || (data == 0))
    {
        return 0U;
    }
    patch_io_runtime_t *const io = context;
    if (length > PATCH_PRODUCT_IO_BUFFER_BYTES - io->encoded_size)
    {
        return 0U;
    }
    memcpy(&io->io_buffer[io->encoded_size], data, length);
    io->encoded_size += length;
    return 1U;
}

static sd_scheduler_background_admission_t patch_io_admit(
    sd_scheduler_background_kind_t kind, uint32_t bytes)
{
    const sd_scheduler_background_request_t request = {
        bytes, g_patch_io.media_epoch, kind
    };
    return sd_scheduler_runtime_background_try_begin(&request);
}

static void patch_io_decode(void)
{
    patch_memory_source_t memory = {
        g_patch_io.io_buffer, g_patch_io.file_size, 0U
    };
    const persist_codec_source_t source = {
        patch_memory_read, patch_memory_reset, patch_memory_size, &memory
    };
    if (persist_codec_decode_patch(&source, &g_patch_io.decoded) != PERSIST_CODEC_OK)
    {
        patch_io_fail(PATCH_PRODUCT_RESULT_DECODE_ERROR);
        return;
    }
    g_patch_io.patch = g_patch_io.decoded.patch;
    if (g_patch_io.operation == PATCH_PRODUCT_OPERATION_LOAD)
    {
        if (persistent_patch_control_validate_mask(&g_patch_io.patch,
                g_patch_io.target_mask) != PERSIST_CODEC_OK)
        {
            patch_io_fail(PATCH_PRODUCT_INVALID);
            return;
        }
        g_patch_io.state = PATCH_IO_PREPARE_LOAD;
        return;
    }
    memcpy(g_patch_io.patch.name, g_patch_io.requested_name,
           sizeof(g_patch_io.patch.name));
    g_patch_io.patch.name_length = (uint16_t)strlen(g_patch_io.requested_name);
    g_patch_io.state = PATCH_IO_ENCODE;
}

static void patch_io_encode(void)
{
    g_patch_io.encoded_size = 0U;
    const persist_codec_sink_t sink = { patch_memory_write, &g_patch_io };
    if (persist_codec_encode_patch(&g_patch_io.patch, &sink, NULL) != PERSIST_CODEC_OK)
    {
        patch_io_fail(PATCH_PRODUCT_RESULT_ENCODE_ERROR);
        return;
    }
    g_patch_io.file_offset = 0U;
    g_patch_io.state = PATCH_IO_OPEN_WRITE;
}

patch_product_result_t patch_product_save_prepare(uint8_t entity,
                                                   uint16_t slot,
                                                   const char *name)
{
    if (slot >= PATCH_PRODUCT_SLOT_COUNT)
    {
        return PATCH_PRODUCT_RESULT_INVALID_SLOT;
    }
    if (patch_io_common_available() == 0U)
    {
        return PATCH_PRODUCT_IO_BUSY;
    }

    char normalized[NAME_CONTRACT_BUFFER_BYTES];
    if (patch_name_normalize(name, normalized) == 0U)
    {
        return PATCH_PRODUCT_RESULT_INVALID_NAME;
    }
    if (persistent_patch_control_capture(entity, normalized,
                                         &g_patch_io.prepared_patch) != PERSIST_CODEC_OK)
    {
        return PATCH_PRODUCT_INVALID;
    }
    g_patch_io.prepared = 1U;
    g_patch_io.prepared_slot = slot;
    return PATCH_PRODUCT_OK;
}

patch_product_result_t patch_product_save_begin(uint16_t slot,
                                                const persist_control_patch_t *snapshot)
{
    if (slot >= PATCH_PRODUCT_SLOT_COUNT)
    {
        return PATCH_PRODUCT_RESULT_INVALID_SLOT;
    }
    if (((snapshot != 0) && (patch_io_common_available() == 0U))
            || ((snapshot == 0)
                && (patch_io_common_available_for_prepared_save() == 0U)))
    {
        return PATCH_PRODUCT_IO_BUSY;
    }

    const persist_control_patch_t *source = snapshot;
    if (source == 0)
    {
        if ((g_patch_io.prepared == 0U) || (g_patch_io.prepared_slot != slot))
        {
            return PATCH_PRODUCT_INVALID;
        }
        source = &g_patch_io.prepared_patch;
    }
    if ((persist_codec_validate_patch(source) != PERSIST_CODEC_OK)
            || (patch_name_is_canonical(source) == 0U)
            || (patch_io_prepare_paths(slot) == 0U))
    {
        return PATCH_PRODUCT_INVALID;
    }

    memcpy(&g_patch_io.patch, source, sizeof(g_patch_io.patch));
    g_patch_io.prepared = 0U;
    patch_io_start(PATCH_PRODUCT_OPERATION_SAVE, slot);
    return PATCH_PRODUCT_PENDING;
}

patch_product_result_t patch_product_save_submit(uint16_t slot, const char *name)
{
    if (slot >= PATCH_PRODUCT_SLOT_COUNT)
    {
        return PATCH_PRODUCT_RESULT_INVALID_SLOT;
    }
    if ((g_patch_io.prepared == 0U) || (g_patch_io.prepared_slot != slot))
    {
        return PATCH_PRODUCT_INVALID;
    }
    if (patch_io_common_available_for_prepared_save() == 0U)
    {
        return PATCH_PRODUCT_IO_BUSY;
    }

    char normalized[NAME_CONTRACT_BUFFER_BYTES];
    if (patch_name_normalize(name, normalized) == 0U)
    {
        return PATCH_PRODUCT_RESULT_INVALID_NAME;
    }
    memset(g_patch_io.prepared_patch.name, 0,
           sizeof(g_patch_io.prepared_patch.name));
    memcpy(g_patch_io.prepared_patch.name, normalized,
           sizeof(g_patch_io.prepared_patch.name));
    g_patch_io.prepared_patch.name_length = (uint16_t)strlen(normalized);
    return patch_product_save_begin(slot, 0);
}

void patch_product_save_cancel_prepare(void)
{
    if ((g_patch_io.state == PATCH_IO_IDLE) && (g_patch_io.result_ready == 0U))
    {
        g_patch_io.prepared = 0U;
        g_patch_io.prepared_slot = PATCH_PRODUCT_INVALID_SLOT;
        memset(&g_patch_io.prepared_patch, 0, sizeof(g_patch_io.prepared_patch));
    }
}

patch_product_result_t patch_product_rename_begin(uint16_t slot, const char *name)
{
    if (slot >= PATCH_PRODUCT_SLOT_COUNT)
    {
        return PATCH_PRODUCT_RESULT_INVALID_SLOT;
    }
    if (g_present[slot] == 0U)
    {
        return PATCH_PRODUCT_EMPTY;
    }
    if (g_invalid[slot] != 0U)
    {
        return PATCH_PRODUCT_INVALID;
    }
    if (patch_io_common_available() == 0U)
    {
        return PATCH_PRODUCT_IO_BUSY;
    }
    if (patch_name_normalize(name, g_patch_io.requested_name) == 0U)
    {
        return PATCH_PRODUCT_RESULT_INVALID_NAME;
    }
    if (patch_io_prepare_paths(slot) == 0U)
    {
        return PATCH_PRODUCT_INVALID;
    }

    patch_io_start(PATCH_PRODUCT_OPERATION_RENAME, slot);
    return PATCH_PRODUCT_PENDING;
}

patch_product_result_t patch_product_save(uint8_t entity, uint16_t *out_slot)
{
    if (patch_io_common_available() == 0U)
    {
        return PATCH_PRODUCT_IO_BUSY;
    }
    const uint16_t slot = (g_current < PATCH_PRODUCT_SLOT_COUNT
            && g_invalid[g_current] == 0U)
        ? g_current : patch_product_first_empty();
    if (slot == PATCH_PRODUCT_INVALID_SLOT)
    {
        return PATCH_PRODUCT_NO_SLOT;
    }

    char generated[NAME_CONTRACT_BUFFER_BYTES];
    const char *name = (g_present[slot] != 0U && g_meta[slot].name[0] != '\0')
        ? g_meta[slot].name : generated;
    if (name == generated)
    {
        (void)snprintf(generated, sizeof(generated), "T%02u %s",
                       (unsigned)(entity + 1U),
                       track_catalog_family_short_name(track_state_get_family(entity)));
    }
    patch_product_result_t result = patch_product_save_prepare(entity, slot, name);
    if (result == PATCH_PRODUCT_OK)
    {
        result = patch_product_save_begin(slot, 0);
    }
    if (out_slot != 0)
    {
        *out_slot = slot;
    }
    return result;
}

patch_product_result_t patch_product_load_begin(uint16_t slot, uint16_t target_mask)
{
    if (slot >= PATCH_PRODUCT_SLOT_COUNT)
        return PATCH_PRODUCT_RESULT_INVALID_SLOT;
    if (target_mask == 0U) return PATCH_PRODUCT_INVALID;
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
        if (((target_mask & (uint16_t)(1UL << entity)) != 0U)
                && (entity_topology_is_active(entity) == 0U))
            return PATCH_PRODUCT_INVALID;
    if (g_present[slot] == 0U) return PATCH_PRODUCT_EMPTY;
    if (g_invalid[slot] != 0U) return PATCH_PRODUCT_INVALID;
    if (patch_io_common_available() == 0U) return PATCH_PRODUCT_IO_BUSY;
    if (!path(g_patch_io.final_path, sizeof(g_patch_io.final_path), slot))
        return PATCH_PRODUCT_INVALID;
    patch_io_start(PATCH_PRODUCT_OPERATION_LOAD, slot);
    g_patch_io.target_mask = target_mask;
    return PATCH_PRODUCT_PENDING;
}

patch_product_result_t patch_product_apply(uint16_t slot, uint8_t entity)
{
    if (entity >= BRICK_ENTITY_CAPACITY) return PATCH_PRODUCT_INVALID;
    return patch_product_load_begin(slot, (uint16_t)(1UL << entity));
}

static project_control_asset_result_t patch_product_prepare_assets(void)
{
    for (uint8_t asset_index = 0U;
         asset_index < g_patch_io.patch.asset_count; ++asset_index)
    {
        const persist_control_asset_ref_t *const selected =
            &g_patch_io.patch.assets[asset_index];
        char asset_path[PERSIST_CONTROL_ASSET_PATH_BYTES + 1U];
        uint16_t logical = 0U;
        memcpy(asset_path, selected->canonical_path, selected->path_length);
        asset_path[selected->path_length] = '\0';
        const project_control_asset_result_t result =
            project_control_ensure_asset(selected->kind, asset_path, &logical);
        if (result != PROJECT_CONTROL_ASSET_READY) return result;
    }
    return PROJECT_CONTROL_ASSET_READY;
}

void patch_product_apply_service(void)
{
    if ((g_patch_io.operation != PATCH_PRODUCT_OPERATION_LOAD)
            || ((g_patch_io.state != PATCH_IO_PREPARE_LOAD)
                && (g_patch_io.state != PATCH_IO_WAIT_ASSET)))
        return;
    sampler_ram_result_t ram_result;
    uint16_t backend, runtime;
    const char *path_value;
    if ((g_patch_io.state == PATCH_IO_WAIT_ASSET)
            && sampler_ram_pool_load_async_take_result(&ram_result, &backend,
                                                       &runtime, &path_value))
    {
        project_control_complete_ram_runtime(path_value, backend, runtime,
            (ram_result == SAMPLER_RAM_RESULT_OK) ? 1U : 0U);
        if (ram_result != SAMPLER_RAM_RESULT_OK)
        {
            patch_io_finish(PATCH_PRODUCT_INVALID);
            return;
        }
    }
    const project_control_asset_result_t assets = patch_product_prepare_assets();
    if (assets == PROJECT_CONTROL_ASSET_PENDING)
    {
        g_patch_io.state = PATCH_IO_WAIT_ASSET;
        return;
    }
    if ((assets == PROJECT_CONTROL_ASSET_FAILED)
            || (assets == PROJECT_CONTROL_ASSET_FAILED_INTERNAL)
            || (persistent_patch_control_apply_mask(&g_patch_io.patch,
                g_patch_io.target_mask) != PERSIST_CODEC_OK))
    {
        patch_io_finish(PATCH_PRODUCT_INVALID);
        return;
    }
    g_current = g_patch_io.slot;
    patch_io_finish(PATCH_PRODUCT_OK);
}

patch_product_result_t patch_product_clear(uint8_t entity)
{
    if (patch_io_common_available() == 0U) return PATCH_PRODUCT_IO_BUSY;
    persist_control_patch_t patch;
    if ((persistent_patch_control_make_default(entity, &patch) != PERSIST_CODEC_OK)
            || (persistent_patch_control_apply(&patch, entity) != PERSIST_CODEC_OK))
        return PATCH_PRODUCT_INVALID;
    return PATCH_PRODUCT_OK;
}

void patch_product_service(void)
{
    if ((g_patch_io.state == PATCH_IO_IDLE) || (g_patch_io.state == PATCH_IO_DONE)
            || (g_patch_io.state == PATCH_IO_PREPARE_LOAD)
            || (g_patch_io.state == PATCH_IO_WAIT_ASSET))
    {
        return;
    }

    /* Failure cleanup must still terminate if the media epoch changed. */
    if (g_patch_io.result != PATCH_PRODUCT_PENDING)
    {
        if (g_patch_io.state == PATCH_IO_CLOSE_READ_FAILED)
        {
            (void)persistent_fatfs_close_result(&g_patch_io.file);
            g_patch_io.read_open = 0U;
            patch_io_finish(g_patch_io.result);
            return;
        }
        if (g_patch_io.state == PATCH_IO_CLOSE_WRITE_FAILED)
        {
            (void)persistent_fatfs_close_result(&g_patch_io.file);
            g_patch_io.write_open = 0U;
            g_patch_io.state = PATCH_IO_CLEAN_TEMP;
            return;
        }
        if (g_patch_io.state == PATCH_IO_CLEAN_TEMP)
        {
            (void)f_unlink(g_patch_io.temporary_path);
            patch_io_finish(g_patch_io.result);
            return;
        }
    }

    uint32_t bytes = 0U;
    sd_scheduler_background_kind_t kind = SD_SCHEDULER_BACKGROUND_METADATA;
    if ((g_patch_io.state == PATCH_IO_READ)
            || (g_patch_io.state == PATCH_IO_WRITE))
    {
        kind = SD_SCHEDULER_BACKGROUND_DATA;
        bytes = (g_patch_io.state == PATCH_IO_READ)
            ? (g_patch_io.file_size - g_patch_io.file_offset)
            : (g_patch_io.encoded_size - g_patch_io.file_offset);
        if (bytes > SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES)
        {
            bytes = SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES;
        }
    }

    const sd_scheduler_background_admission_t admission = patch_io_admit(kind, bytes);
    if (admission == SD_SCHEDULER_BACKGROUND_NOT_NOW)
    {
        return;
    }
    if (admission != SD_SCHEDULER_BACKGROUND_GO)
    {
        patch_io_fail(PATCH_PRODUCT_IO_ERROR);
        return;
    }

    FRESULT file_result = FR_OK;
    UINT transferred = 0U;
    switch (g_patch_io.state)
    {
        case PATCH_IO_MOUNT:
            if (sd_access_fs_mount_if_needed() == 0U)
            {
                patch_io_fail(PATCH_PRODUCT_IO_ERROR);
            }
            else
            {
                g_patch_io.state = (g_patch_io.operation == PATCH_PRODUCT_OPERATION_LOAD)
                    ? PATCH_IO_OPEN_READ : PATCH_IO_MKDIR_BRICK;
            }
            break;
        case PATCH_IO_MKDIR_BRICK:
            file_result = f_mkdir("0:/BRICK");
            if ((file_result != FR_OK) && (file_result != FR_EXIST))
            {
                patch_io_fail(PATCH_PRODUCT_IO_ERROR);
            }
            else
            {
                g_patch_io.state = PATCH_IO_MKDIR_PATCH;
            }
            break;
        case PATCH_IO_MKDIR_PATCH:
            file_result = f_mkdir("0:/BRICK/PATCH");
            if ((file_result != FR_OK) && (file_result != FR_EXIST))
            {
                patch_io_fail(PATCH_PRODUCT_IO_ERROR);
            }
            else
            {
                g_patch_io.state = PATCH_IO_RECOVER;
            }
            break;
        case PATCH_IO_RECOVER:
            file_result = persistent_fatfs_recover_replace(
                g_patch_io.final_path, g_patch_io.temporary_path,
                g_patch_io.backup_path);
            if (file_result != FR_OK)
            {
                patch_io_fail(PATCH_PRODUCT_IO_ERROR);
            }
            else
            {
                g_patch_io.state = (g_patch_io.operation == PATCH_PRODUCT_OPERATION_RENAME)
                    ? PATCH_IO_OPEN_READ : PATCH_IO_ENCODE;
            }
            break;
        case PATCH_IO_OPEN_READ:
            if (persistent_fatfs_open_read(&g_patch_io.file, g_patch_io.final_path) == 0U)
            {
                patch_io_fail((g_patch_io.file.last_result == FR_NO_FILE)
                                  ? PATCH_PRODUCT_RESULT_FILE_ABSENT : PATCH_PRODUCT_IO_ERROR);
            }
            else if (g_patch_io.file.size > PATCH_PRODUCT_IO_BUFFER_BYTES)
            {
                g_patch_io.read_open = 1U;
                patch_io_fail(PATCH_PRODUCT_RESULT_DECODE_ERROR);
            }
            else
            {
                g_patch_io.file_size = g_patch_io.file.size;
                g_patch_io.file_offset = 0U;
                g_patch_io.read_open = 1U;
                g_patch_io.state = PATCH_IO_READ;
            }
            break;
        case PATCH_IO_READ:
            file_result = f_read(&g_patch_io.file.file,
                                 &g_patch_io.io_buffer[g_patch_io.file_offset],
                                 (UINT)bytes, &transferred);
            if ((file_result != FR_OK) || (transferred != (UINT)bytes))
            {
                patch_io_fail(PATCH_PRODUCT_IO_ERROR);
            }
            else
            {
                g_patch_io.file_offset += bytes;
                if (g_patch_io.file_offset == g_patch_io.file_size)
                {
                    g_patch_io.state = PATCH_IO_CLOSE_READ;
                }
            }
            break;
        case PATCH_IO_CLOSE_READ:
            file_result = persistent_fatfs_close_result(&g_patch_io.file);
            g_patch_io.read_open = 0U;
            if (file_result != FR_OK)
            {
                patch_io_fail(PATCH_PRODUCT_IO_ERROR);
            }
            else
            {
                g_patch_io.state = PATCH_IO_DECODE;
            }
            break;
        case PATCH_IO_DECODE:
            patch_io_decode();
            break;
        case PATCH_IO_ENCODE:
            patch_io_encode();
            break;
        case PATCH_IO_OPEN_WRITE:
            if (persistent_fatfs_open_write(&g_patch_io.file,
                                            g_patch_io.temporary_path) == 0U)
            {
                patch_io_fail(PATCH_PRODUCT_IO_ERROR);
            }
            else
            {
                g_patch_io.write_open = 1U;
                g_patch_io.file_offset = 0U;
                g_patch_io.state = PATCH_IO_WRITE;
            }
            break;
        case PATCH_IO_WRITE:
            file_result = f_write(&g_patch_io.file.file,
                                  &g_patch_io.io_buffer[g_patch_io.file_offset],
                                  (UINT)bytes, &transferred);
            if ((file_result != FR_OK) || (transferred != (UINT)bytes))
            {
                patch_io_fail(PATCH_PRODUCT_IO_ERROR);
            }
            else
            {
                g_patch_io.file_offset += bytes;
                if (g_patch_io.file_offset == g_patch_io.encoded_size)
                {
                    g_patch_io.state = PATCH_IO_SYNC;
                }
            }
            break;
        case PATCH_IO_SYNC:
            file_result = f_sync(&g_patch_io.file.file);
            if (file_result != FR_OK)
            {
                patch_io_fail(PATCH_PRODUCT_IO_ERROR);
            }
            else
            {
                g_patch_io.state = PATCH_IO_CLOSE_WRITE;
            }
            break;
        case PATCH_IO_CLOSE_WRITE:
            file_result = persistent_fatfs_close_result(&g_patch_io.file);
            g_patch_io.write_open = 0U;
            if (file_result != FR_OK)
            {
                patch_io_fail(PATCH_PRODUCT_IO_ERROR);
            }
            else
            {
                g_patch_io.state = PATCH_IO_COMMIT;
            }
            break;
        case PATCH_IO_COMMIT:
            file_result = persistent_fatfs_commit_replace(
                g_patch_io.final_path, g_patch_io.temporary_path,
                g_patch_io.backup_path);
            if (file_result != FR_OK)
            {
                patch_io_fail(PATCH_PRODUCT_IO_ERROR);
            }
            else
            {
                patch_io_finish(PATCH_PRODUCT_OK);
            }
            break;
        case PATCH_IO_CLOSE_READ_FAILED:
            break;
        case PATCH_IO_CLOSE_WRITE_FAILED:
            break;
        case PATCH_IO_CLEAN_TEMP:
            break;
        default:
            patch_io_fail(PATCH_PRODUCT_IO_ERROR);
            break;
    }
    sd_scheduler_runtime_background_end();
}

uint8_t patch_product_result_pending(patch_product_operation_t operation)
{
    return (g_patch_io.state == PATCH_IO_DONE)
        && (g_patch_io.result_ready != 0U)
        && (g_patch_io.operation == operation);
}

uint8_t patch_product_take_result(patch_product_operation_t *operation,
                                  uint16_t *slot,
                                  patch_product_result_t *result)
{
    if ((g_patch_io.state != PATCH_IO_DONE) || (g_patch_io.result_ready == 0U))
    {
        return 0U;
    }
    if (operation != 0)
    {
        *operation = g_patch_io.operation;
    }
    if (slot != 0)
    {
        *slot = g_patch_io.slot;
    }
    if (result != 0)
    {
        *result = g_patch_io.result;
    }
    memset(&g_patch_io, 0, sizeof(g_patch_io));
    g_patch_io.state = PATCH_IO_IDLE;
    return 1U;
}

void patch_product_init(void)
{
    memset(g_present, 0, sizeof(g_present));
    memset(g_invalid, 0, sizeof(g_invalid));
    memset(g_meta, 0, sizeof(g_meta));
    memset(&g_patch_io, 0, sizeof(g_patch_io));
    g_patch_io.state = PATCH_IO_IDLE;
    g_current = PATCH_PRODUCT_INVALID_SLOT;
    if (acquire() == 0U)
    {
        return;
    }
    (void)f_mkdir("0:/BRICK");
    (void)f_mkdir("0:/BRICK/PATCH");
    for (uint16_t slot = 0U; slot < PATCH_PRODUCT_SLOT_COUNT; ++slot)
    {
        char final_path[48];
        FILINFO info;
        if (path(final_path, sizeof(final_path), slot)
                && (f_stat(final_path, &info) == FR_OK))
        {
            (void)scan_meta(slot);
        }
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_PATCH);
}

uint16_t patch_product_first_empty(void)
{
    for (uint16_t slot = 0U; slot < PATCH_PRODUCT_SLOT_COUNT; ++slot)
    {
        if (g_present[slot] == 0U)
        {
            return slot;
        }
    }
    return PATCH_PRODUCT_INVALID_SLOT;
}

patch_product_result_t patch_product_rename(uint16_t slot, const char *name)
{
    return patch_product_rename_begin(slot, name);
}

patch_product_result_t patch_product_delete(uint16_t slot, uint16_t *out_next)
{
    if (patch_io_busy() != 0U)
    {
        return PATCH_PRODUCT_IO_BUSY;
    }
    if (slot >= PATCH_PRODUCT_SLOT_COUNT)
    {
        return PATCH_PRODUCT_RESULT_INVALID_SLOT;
    }
    if (g_present[slot] == 0U)
    {
        return PATCH_PRODUCT_EMPTY;
    }
    if (acquire() == 0U)
    {
        return PATCH_PRODUCT_IO_BUSY;
    }
    char final_path[48];
    FRESULT file_result = path(final_path, sizeof(final_path), slot)
        ? f_unlink(final_path) : FR_INVALID_NAME;
    sd_access_gate_release(SD_ACCESS_CLIENT_PATCH);
    if ((file_result != FR_OK) && (file_result != FR_NO_FILE))
    {
        return PATCH_PRODUCT_IO_ERROR;
    }
    g_present[slot] = 0U;
    g_invalid[slot] = 0U;
    memset(&g_meta[slot], 0, sizeof(g_meta[slot]));
    uint16_t next = PATCH_PRODUCT_INVALID_SLOT;
    for (uint16_t offset = 1U; offset <= PATCH_PRODUCT_SLOT_COUNT; ++offset)
    {
        const uint16_t candidate = (uint16_t)((slot + offset) % PATCH_PRODUCT_SLOT_COUNT);
        if ((g_present[candidate] != 0U) && (g_invalid[candidate] == 0U))
        {
            next = candidate;
            break;
        }
    }
    g_current = next;
    if (out_next != 0)
    {
        *out_next = next;
    }
    return PATCH_PRODUCT_OK;
}

patch_product_slot_state_t patch_product_slot_state(uint16_t slot)
{
    if ((slot >= PATCH_PRODUCT_SLOT_COUNT) || (g_invalid[slot] != 0U))
    {
        return PATCH_PRODUCT_SLOT_INVALID;
    }
    return (g_present[slot] != 0U)
        ? PATCH_PRODUCT_SLOT_VALID : PATCH_PRODUCT_SLOT_EMPTY;
}

uint8_t patch_product_metadata(uint16_t slot, patch_product_metadata_t *out)
{
    if ((out == 0) || (patch_product_slot_state(slot) != PATCH_PRODUCT_SLOT_VALID))
    {
        return 0U;
    }
    *out = g_meta[slot];
    return 1U;
}

void patch_product_set_current(uint16_t slot)
{
    if (slot < PATCH_PRODUCT_SLOT_COUNT)
    {
        g_current = slot;
    }
}

uint16_t patch_product_get_current(void)
{
    return g_current;
}

const char *patch_product_result_label(patch_product_result_t result)
{
    switch (result)
    {
        case PATCH_PRODUCT_OK: return "OK";
        case PATCH_PRODUCT_PENDING: return "LOADING";
        case PATCH_PRODUCT_EMPTY: return "EMPTY";
        case PATCH_PRODUCT_IO_BUSY: return "SD BUSY";
        case PATCH_PRODUCT_NO_SLOT: return "BANK FULL";
        case PATCH_PRODUCT_RESULT_INVALID_NAME: return "NAME INVALID";
        case PATCH_PRODUCT_RESULT_FILE_ABSENT: return "FILE ABSENT";
        case PATCH_PRODUCT_RESULT_DECODE_ERROR: return "DECODE ERROR";
        case PATCH_PRODUCT_RESULT_ENCODE_ERROR: return "ENCODE ERROR";
        case PATCH_PRODUCT_RESULT_INVALID_SLOT: return "BAD SLOT";
        case PATCH_PRODUCT_IO_ERROR: return "SD ERROR";
        default: return "INVALID";
    }
}
