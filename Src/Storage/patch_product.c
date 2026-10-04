#include "Storage/patch_product.h"

#include <stdio.h>
#include <string.h>

#include "App/name_contract.h"
#include "Import/dx7_import.h"
#include "Import/dx7_sysex.h"
#include "Platform/memory_layout.h"
#include "SD/sd_scheduler_runtime.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/multi_sample_loader.h"
#include "Sampler/wavetable_pool.h"
#include "Storage/persistent_fatfs_io.h"
#include "Storage/persistent_key_catalog.h"
#include "Storage/persistent_patch_control.h"
#include "Storage/patch_preview.h"
#include "Storage/project_control.h"
#include "Storage/project_load_quiesce.h"
#include "Storage/project_product.h"
#include "Storage/project_storage_paths.h"
#include "Storage/sd_access_gate.h"
#include "Track/track_catalog.h"
#include "Track/entity_topology.h"
#include "Track/track_state.h"
#include "ff.h"

static uint8_t g_present[PATCH_PRODUCT_SLOT_COUNT];
static uint8_t g_invalid[PATCH_PRODUCT_SLOT_COUNT];
STORAGE_STATE_SDRAM static patch_product_metadata_t g_meta[PATCH_PRODUCT_SLOT_COUNT];
static uint16_t g_current = PATCH_PRODUCT_INVALID_SLOT;
#define PATCH_PRODUCT_IO_BUFFER_BYTES (16U * 1024U)
#define PATCH_DX7_FILE_BUFFER_BYTES (32U * 1024U)
#define PATCH_DX7_FILE_COUNT_MAX PATCH_PRODUCT_SLOT_COUNT
#define PATCH_DX7_FILENAME_BYTES 256U

typedef struct
{
    char filenames[PATCH_DX7_FILE_COUNT_MAX][PATCH_DX7_FILENAME_BYTES];
    uint8_t file_bytes[PATCH_DX7_FILE_BUFFER_BYTES];
    dx7_voice_t voices[PATCH_PRODUCT_SLOT_COUNT];
    uint16_t slots[PATCH_PRODUCT_SLOT_COUNT];
    persist_control_patch_t patch;
    persist_codec_patch_staging_t decoded;
} patch_dx7_boot_workspace_t;

STORAGE_STATE_SDRAM static patch_dx7_boot_workspace_t g_patch_dx7_boot;

typedef enum
{
    PATCH_IO_IDLE = 0,
    PATCH_IO_MOUNT,
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

static uint8_t path(char *out, uint32_t size, uint16_t slot)
{
    return project_storage_patch_file(out,size,slot);
}

static FRESULT patch_mkdir(void)
{
    char directory[32];
    if(project_storage_patches_root(directory,sizeof(directory))==0U)
        return FR_INVALID_NAME;
    return f_mkdir(directory);
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

static uint8_t scan_meta(uint16_t slot)
{
    char final_path[48];
    persistent_fatfs_file_t file;
    if ((path(final_path, sizeof(final_path), slot) == 0U)
            || (persistent_fatfs_open_read(&file, final_path) == 0U))
    {
        return 0U;
    }
    const persist_codec_source_t source = persistent_fatfs_source(&file);
    uint8_t valid = (uint8_t)(persist_codec_decode_patch(
        &source, &g_patch_io.decoded) == PERSIST_CODEC_OK);
    valid = (uint8_t)(valid && (f_tell(&file.file) == f_size(&file.file)));
    if (valid)
    {
        meta_from_patch(slot, &g_patch_io.decoded.patch);
    }
    valid = (uint8_t)(valid
        && (persistent_fatfs_close_result(&file) == FR_OK));
    g_present[slot] = 1U;
    g_invalid[slot] = (valid != 0U) ? 0U : 1U;
    return valid;
}

static uint8_t patch_dx7_has_extension(const char *name)
{
    if (name == NULL) return 0U;
    const size_t length = strlen(name);
    if (length < 4U) return 0U;
    const char *const extension = name + length - 4U;
    return (uint8_t)(extension[0] == '.'
        && (extension[1] == 's' || extension[1] == 'S')
        && (extension[2] == 'y' || extension[2] == 'Y')
        && (extension[3] == 'x' || extension[3] == 'X'));
}

static uint16_t patch_dx7_collect_sources(void)
{
    char root[32];
    DIR directory;
    FILINFO info;
    uint16_t count = 0U;
    if (!project_storage_patches_root(root, sizeof(root))
            || f_opendir(&directory, root) != FR_OK) return 0U;
    for (;;)
    {
        memset(&info, 0, sizeof(info));
        const FRESULT result = f_readdir(&directory, &info);
        if (result != FR_OK || info.fname[0] == '\0') break;
        if ((info.fattrib & AM_DIR) != 0U || !patch_dx7_has_extension(info.fname)) continue;
        const size_t length = strlen(info.fname);
        if (length >= PATCH_DX7_FILENAME_BYTES || count >= PATCH_DX7_FILE_COUNT_MAX) continue;
        memcpy(g_patch_dx7_boot.filenames[count], info.fname, length + 1U);
        ++count;
    }
    (void)f_closedir(&directory);
    return count;
}

static uint8_t patch_dx7_source_path(char *out, size_t capacity, const char *filename)
{
    const int length = snprintf(out, capacity, "0:/PATCHES/%s", filename);
    return (uint8_t)(length > 0 && (size_t)length < capacity);
}

static uint8_t patch_dx7_read_source(const char *source_path, uint32_t *out_size)
{
    persistent_fatfs_file_t source;
    if (out_size == NULL || !persistent_fatfs_open_read(&source, source_path)) return 0U;
    uint8_t valid = (uint8_t)(source.size > 0U
        && source.size <= PATCH_DX7_FILE_BUFFER_BYTES);
    if (valid)
    {
        UINT read = 0U;
        valid = (uint8_t)(f_read(&source.file, g_patch_dx7_boot.file_bytes,
            (UINT)source.size, &read) == FR_OK && read == (UINT)source.size);
    }
    const uint32_t size = source.size;
    if (persistent_fatfs_close_result(&source) != FR_OK) valid = 0U;
    if (valid) *out_size = size;
    return valid;
}

static uint8_t patch_dx7_name_used(const char *name)
{
    for (uint16_t slot = 0U; slot < PATCH_PRODUCT_SLOT_COUNT; ++slot)
        if (g_present[slot] != 0U && g_invalid[slot] == 0U
                && strcmp(g_meta[slot].name, name) == 0) return 1U;
    return 0U;
}

static uint8_t patch_dx7_make_unique_name(persist_control_patch_t *patch)
{
    char base[NAME_CONTRACT_BUFFER_BYTES] = {0};
    char candidate[NAME_CONTRACT_BUFFER_BYTES] = {0};
    if (patch == NULL || patch->name_length == 0U
            || patch->name_length > NAME_CONTRACT_MAX_CHARS) return 0U;
    memcpy(base, patch->name, patch->name_length);
    memcpy(candidate, base, patch->name_length + 1U);
    for (uint16_t ordinal = 1U; ordinal < 1000U; ++ordinal)
    {
        if (!patch_dx7_name_used(candidate))
        {
            memset(patch->name, 0, sizeof(patch->name));
            patch->name_length = (uint16_t)strlen(candidate);
            memcpy(patch->name, candidate, patch->name_length);
            return 1U;
        }
        const uint16_t suffix_number = (uint16_t)(ordinal + 1U);
        char suffix[8];
        const int suffix_length = snprintf(suffix, sizeof(suffix), "_%u",
                                           (unsigned)suffix_number);
        if (suffix_length <= 0 || (size_t)suffix_length >= sizeof(suffix)) return 0U;
        size_t base_length = strlen(base);
        if (base_length + (size_t)suffix_length > NAME_CONTRACT_MAX_CHARS)
            base_length = NAME_CONTRACT_MAX_CHARS - (size_t)suffix_length;
        memcpy(candidate, base, base_length);
        memcpy(candidate + base_length, suffix, (size_t)suffix_length + 1U);
    }
    return 0U;
}

static uint8_t patch_dx7_allocate_slots(size_t voice_count)
{
    size_t found = 0U;
    for (uint16_t slot = 0U; slot < PATCH_PRODUCT_SLOT_COUNT && found < voice_count; ++slot)
        if (g_present[slot] == 0U) g_patch_dx7_boot.slots[found++] = slot;
    return (uint8_t)(found == voice_count);
}

static uint8_t patch_dx7_write_one(uint16_t slot,
                                   const persist_control_patch_t *patch)
{
    char final_path[48];
    char temporary_path[56];
    persistent_fatfs_file_t file;
    if (!path(final_path, sizeof(final_path), slot)
            || !side_path(temporary_path, sizeof(temporary_path), final_path, "TMP")) return 0U;
    (void)f_unlink(temporary_path);
    if (!persistent_fatfs_open_write(&file, temporary_path)) return 0U;
    const persist_codec_sink_t sink = persistent_fatfs_sink(&file);
    uint8_t valid = (uint8_t)(persist_codec_encode_patch(patch, &sink, NULL)
        == PERSIST_CODEC_OK && f_sync(&file.file) == FR_OK);
    if (persistent_fatfs_close_result(&file) != FR_OK) valid = 0U;
    if (valid && persistent_fatfs_open_read(&file, temporary_path))
    {
        const persist_codec_source_t source = persistent_fatfs_source(&file);
        valid = (uint8_t)(persist_codec_decode_patch(&source,
            &g_patch_dx7_boot.decoded) == PERSIST_CODEC_OK
            && f_tell(&file.file) == f_size(&file.file));
        if (persistent_fatfs_close_result(&file) != FR_OK) valid = 0U;
    }
    else valid = 0U;
    FILINFO info;
    if (valid && f_stat(final_path, &info) == FR_NO_FILE
            && f_rename(temporary_path, final_path) == FR_OK
            && scan_meta(slot)) return 1U;
    (void)f_unlink(temporary_path);
    return 0U;
}

static void patch_dx7_import_source(const char *filename)
{
    char source_path[PATCH_DX7_FILENAME_BYTES + 16U];
    uint32_t file_size = 0U;
    size_t voice_count = 0U;
    if (!patch_dx7_source_path(source_path, sizeof(source_path), filename)
            || !patch_dx7_read_source(source_path, &file_size)
            || dx7_sysex_parse(g_patch_dx7_boot.file_bytes, file_size,
                g_patch_dx7_boot.voices, PATCH_PRODUCT_SLOT_COUNT, &voice_count)
                != DX7_SYSEX_OK
            || voice_count == 0U
            || !patch_dx7_allocate_slots(voice_count)) return;

    for (size_t i = 0U; i < voice_count; ++i)
        if (dx7_import_voice(&g_patch_dx7_boot.voices[i], 0U,
                &g_patch_dx7_boot.patch) != DX7_IMPORT_OK) return;

    size_t published = 0U;
    for (; published < voice_count; ++published)
    {
        const uint16_t slot = g_patch_dx7_boot.slots[published];
        if (dx7_import_voice(&g_patch_dx7_boot.voices[published], 0U,
                &g_patch_dx7_boot.patch) != DX7_IMPORT_OK
                || !patch_dx7_make_unique_name(&g_patch_dx7_boot.patch)
                || !patch_dx7_write_one(slot, &g_patch_dx7_boot.patch)) break;
    }
    if (published == voice_count) (void)f_unlink(source_path);
}

static void patch_dx7_import_sources(void)
{
    const uint16_t count = patch_dx7_collect_sources();
    for (uint16_t i = 0U; i < count; ++i)
        patch_dx7_import_source(g_patch_dx7_boot.filenames[i]);
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
        && (memcmp(normalized, source, patch->name_length) == 0);
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
    if (g_patch_io.operation != PATCH_PRODUCT_OPERATION_PREVIEW)
    {
        g_present[g_patch_io.slot] = 1U;
        g_invalid[g_patch_io.slot] = 0U;
        meta_from_patch(g_patch_io.slot, &g_patch_io.patch);
    }
    if (g_patch_io.operation == PATCH_PRODUCT_OPERATION_SAVE
        || g_patch_io.operation == PATCH_PRODUCT_OPERATION_OVERWRITE)
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
    if (g_patch_io.operation == PATCH_PRODUCT_OPERATION_PREVIEW)
    {
        if (patch_preview_prepare(&g_patch_io.patch) == 0U)
        {
            patch_io_finish(PATCH_PRODUCT_INVALID);
            return;
        }
        if (patch_preview_note_on(PATCH_PREVIEW_DEFAULT_NOTE,
                                  PATCH_PREVIEW_DEFAULT_VELOCITY) == 0U)
        {
            (void)patch_preview_stop();
            patch_io_finish(PATCH_PRODUCT_IO_BUSY);
            return;
        }
        patch_io_finish(PATCH_PRODUCT_OK);
        return;
    }
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
    if (g_present[slot] != 0U)
    {
        return PATCH_PRODUCT_NO_SLOT;
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
    if (g_present[slot] != 0U)
    {
        return PATCH_PRODUCT_NO_SLOT;
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

patch_product_result_t patch_product_overwrite(uint8_t entity, uint16_t slot)
{
    if (slot >= PATCH_PRODUCT_SLOT_COUNT || g_present[slot] == 0U
        || g_invalid[slot] != 0U) return PATCH_PRODUCT_EMPTY;
    if (patch_io_common_available() == 0U) return PATCH_PRODUCT_IO_BUSY;
    patch_product_metadata_t meta;
    if (patch_product_metadata(slot, &meta) == 0U || meta.name[0] == '\0')
        return PATCH_PRODUCT_INVALID;
    if (persistent_patch_control_capture(entity, meta.name, &g_patch_io.prepared_patch)
        != PERSIST_CODEC_OK) return PATCH_PRODUCT_INVALID;
    if (persist_codec_validate_patch(&g_patch_io.prepared_patch) != PERSIST_CODEC_OK
        || patch_io_prepare_paths(slot) == 0U) return PATCH_PRODUCT_INVALID;
    memcpy(&g_patch_io.patch, &g_patch_io.prepared_patch, sizeof(g_patch_io.patch));
    patch_io_start(PATCH_PRODUCT_OPERATION_OVERWRITE, slot);
    return PATCH_PRODUCT_PENDING;
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
    const uint16_t slot = patch_product_first_empty();
    if (slot == PATCH_PRODUCT_INVALID_SLOT)
    {
        return PATCH_PRODUCT_NO_SLOT;
    }

    char generated[NAME_CONTRACT_BUFFER_BYTES];
    (void)snprintf(generated, sizeof(generated), "T%02u %s",
                   (unsigned)(entity + 1U),
                   track_catalog_family_short_name(track_state_get_family(entity)));
    patch_product_result_t result = patch_product_save_prepare(
        entity, slot, generated);
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
    if ((project_control_asset_loads_pending() != 0U)
            || (sampler_ram_pool_load_async_busy() != 0U)
            || (wavetable_pool_load_async_busy() != 0U)
            || (multi_sample_load_has_pending() != 0U))
        return PATCH_PRODUCT_IO_BUSY;
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

patch_product_result_t patch_product_preview_begin(uint16_t slot)
{
    if (slot >= PATCH_PRODUCT_SLOT_COUNT)
        return PATCH_PRODUCT_RESULT_INVALID_SLOT;
    if (g_present[slot] == 0U) return PATCH_PRODUCT_EMPTY;
    if (g_invalid[slot] != 0U) return PATCH_PRODUCT_INVALID;
    if (patch_io_common_available() == 0U) return PATCH_PRODUCT_IO_BUSY;
    if (!path(g_patch_io.final_path, sizeof(g_patch_io.final_path), slot))
        return PATCH_PRODUCT_INVALID;
    if (patch_preview_stop() == 0U) return PATCH_PRODUCT_IO_BUSY;
    patch_io_start(PATCH_PRODUCT_OPERATION_PREVIEW, slot);
    return PATCH_PRODUCT_PENDING;
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
            project_control_prepare_asset(selected->kind, asset_path, &logical);
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
                g_patch_io.state = ((g_patch_io.operation == PATCH_PRODUCT_OPERATION_LOAD)
                        || (g_patch_io.operation == PATCH_PRODUCT_OPERATION_PREVIEW))
                    ? PATCH_IO_OPEN_READ : PATCH_IO_MKDIR_PATCH;
            }
            break;
        case PATCH_IO_MKDIR_PATCH:
            file_result = patch_mkdir();
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
    patch_preview_init();
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
    (void)patch_mkdir();
    for (uint16_t slot = 0U; slot < PATCH_PRODUCT_SLOT_COUNT; ++slot)
    {
        char final_path[48];
        char temporary_path[56];
        char backup_path[56];
        FILINFO info;
        if (path(final_path, sizeof(final_path), slot)
                && side_path(temporary_path, sizeof(temporary_path),
                             final_path, "TMP")
                && side_path(backup_path, sizeof(backup_path),
                             final_path, "BAK"))
        {
            (void)persistent_fatfs_recover_replace(
                final_path, temporary_path, backup_path);
        }
        if (path(final_path, sizeof(final_path), slot)
                && (f_stat(final_path, &info) == FR_OK))
        {
            (void)scan_meta(slot);
        }
    }
    patch_dx7_import_sources();
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
    char temporary_path[56];
    char backup_path[56];
    FRESULT file_result = FR_INVALID_NAME;
    if (path(final_path, sizeof(final_path), slot)
            && side_path(temporary_path, sizeof(temporary_path),
                         final_path, "TMP")
            && side_path(backup_path, sizeof(backup_path),
                         final_path, "BAK"))
    {
        const FRESULT temporary_result = f_unlink(temporary_path);
        const FRESULT backup_result = f_unlink(backup_path);
        if ((temporary_result != FR_OK) && (temporary_result != FR_NO_FILE))
            file_result = temporary_result;
        else if ((backup_result != FR_OK) && (backup_result != FR_NO_FILE))
            file_result = backup_result;
        else
            file_result = f_unlink(final_path);
    }
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
