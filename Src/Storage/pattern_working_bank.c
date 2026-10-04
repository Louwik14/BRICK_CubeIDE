#include "Storage/pattern_working_bank.h"

#include <stdio.h>
#include <string.h>

#include "SD/sd_scheduler_runtime.h"
#include "Platform/memory_layout.h"
#include "Storage/persistent_fatfs_io.h"
#include "Storage/project_storage_paths.h"
#include "Storage/sd_access_gate.h"
#include "ff.h"

#define PATTERN_WORKING_BANK_COUNT 16U
#define PATTERN_WORKING_PATTERN_COUNT 16U
#define PATTERN_WORKING_INVALID_PROJECT 0xFFU

typedef enum
{
    WORKING_ASYNC_IDLE = 0,
    WORKING_ASYNC_MOUNT,
    WORKING_ASYNC_RECOVER,
    WORKING_ASYNC_OPEN_BASE,
    WORKING_ASYNC_COMPARE_BASE,
    WORKING_ASYNC_CLOSE_BASE,
    WORKING_ASYNC_DECIDE,
    WORKING_ASYNC_MKDIR_BRICK,
    WORKING_ASYNC_MKDIR_WORKING,
    WORKING_ASYNC_MKDIR_PATTERNS,
    WORKING_ASYNC_OPEN_WORKING,
    WORKING_ASYNC_ALLOCATE,
    WORKING_ASYNC_TRANSFER,
    WORKING_ASYNC_SYNC,
    WORKING_ASYNC_CLOSE_WORKING,
    WORKING_ASYNC_COMMIT,
    WORKING_ASYNC_DELETE,
    WORKING_ASYNC_DECODE,
    WORKING_ASYNC_CLEAN_CLOSE,
    WORKING_ASYNC_CLEAN_TEMP,
    WORKING_ASYNC_DONE
} working_async_state_t;

typedef struct
{
    persistent_fatfs_file_t file;
    persist_control_pattern_t *load_out;
    uint8_t *encoded;
    uint32_t encoded_capacity;
    uint32_t encoded_size;
    uint32_t offset;
    uint32_t media_epoch;
    working_async_state_t state;
    pattern_working_operation_t operation;
    uint8_t bank;
    uint8_t pattern;
    uint8_t file_open;
    uint8_t base_equal;
    uint8_t writing_ready;
    uint8_t result_ready;
    uint8_t success;
    char final_path[80];
    char temporary_path[84];
    char backup_path[84];
    char base_path[80];
    uint8_t compare[SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES];
} working_async_context_t;

typedef struct
{
    uint8_t *data;
    uint32_t capacity;
    uint32_t position;
    uint8_t equal;
} working_memory_io_t;

static uint32_t g_working_dirty[8];
static pattern_working_base_kind_t g_working_base_kind;
static uint8_t g_working_project_slot;
STORAGE_STATE_SDRAM static working_async_context_t g_working_async;

static uint8_t working_slot_valid(uint8_t bank, uint8_t pattern)
{
    return (bank < PATTERN_WORKING_BANK_COUNT
            && pattern < PATTERN_WORKING_PATTERN_COUNT) ? 1U : 0U;
}

static uint32_t working_bit_index(uint8_t bank, uint8_t pattern)
{
    return (uint32_t)bank * PATTERN_WORKING_PATTERN_COUNT + pattern;
}

static void working_dirty_set(uint8_t bank, uint8_t pattern, uint8_t present)
{
    const uint32_t index = working_bit_index(bank, pattern);
    const uint32_t mask = UINT32_C(1) << (index & 31U);
    if (present != 0U) g_working_dirty[index >> 5U] |= mask;
    else g_working_dirty[index >> 5U] &= ~mask;
}

static uint8_t working_side_path(char *out, uint32_t capacity,
                                 const char *base, const char *suffix)
{
    const int length = snprintf(out, capacity, "%s.%s", base, suffix);
    return (length > 0 && (uint32_t)length < capacity) ? 1U : 0U;
}

static uint8_t working_memory_write(void *context, const uint8_t *data,
                                    uint32_t length)
{
    working_memory_io_t *const memory = context;
    if (memory == NULL || data == NULL
            || memory->position > memory->capacity
            || length > memory->capacity - memory->position) return 0U;
    memcpy(&memory->data[memory->position], data, length);
    memory->position += length;
    return 1U;
}

static uint8_t working_memory_compare(void *context, const uint8_t *data,
                                      uint32_t length)
{
    working_memory_io_t *const memory = context;
    if (memory == NULL || data == NULL
            || memory->position > memory->capacity
            || length > memory->capacity - memory->position) return 0U;
    if (memcmp(&memory->data[memory->position], data, length) != 0)
        memory->equal = 0U;
    memory->position += length;
    return 1U;
}

static uint8_t working_memory_read(void *context, uint8_t *data,
                                   uint32_t length)
{
    working_memory_io_t *const memory = context;
    if (memory == NULL || data == NULL
            || memory->position > memory->capacity
            || length > memory->capacity - memory->position) return 0U;
    memcpy(data, &memory->data[memory->position], length);
    memory->position += length;
    return 1U;
}

static uint8_t working_memory_reset(void *context)
{
    working_memory_io_t *const memory = context;
    if (memory == NULL) return 0U;
    memory->position = 0U;
    return 1U;
}

static uint8_t working_memory_size(void *context, uint32_t *out_size)
{
    working_memory_io_t *const memory = context;
    if (memory == NULL || out_size == NULL) return 0U;
    *out_size = memory->capacity;
    return 1U;
}

static uint8_t working_mkdir(uint8_t (*build)(char *, uint32_t))
{
    char path[64];
    if (build == NULL || build(path, sizeof(path)) == 0U) return 0U;
    const FRESULT result = f_mkdir(path);
    return (result == FR_OK || result == FR_EXIST) ? 1U : 0U;
}

static uint8_t working_unlink_optional(const char *path)
{
    const FRESULT result = f_unlink(path);
    return (result == FR_OK || result == FR_NO_FILE) ? 1U : 0U;
}

static uint8_t working_clear_mounted(void)
{
    char directory[64];
    if (!working_mkdir(project_storage_internal_root)
            || !working_mkdir(project_storage_working_root)
            || !working_mkdir(project_storage_working_patterns_dir)
            || !project_storage_working_patterns_dir(
                directory, sizeof(directory))) return 0U;
    DIR dir;
    FRESULT result = f_opendir(&dir, directory);
    if (result != FR_OK) return 0U;
    FILINFO info;
    uint8_t ok = 1U;
    for (;;)
    {
        result = f_readdir(&dir, &info);
        if (result != FR_OK) { ok = 0U; break; }
        if (info.fname[0] == '\0') break;
        if ((info.fattrib & AM_DIR) != 0U) { ok = 0U; break; }
        char child[96];
        const int length = snprintf(child, sizeof(child), "%s/%s",
                                    directory, info.fname);
        if (length <= 0 || (uint32_t)length >= sizeof(child)
                || f_unlink(child) != FR_OK) { ok = 0U; break; }
    }
    if (f_closedir(&dir) != FR_OK) ok = 0U;
    if (ok != 0U) memset(g_working_dirty, 0, sizeof(g_working_dirty));
    return ok;
}

void pattern_working_bank_init(void)
{
    memset(g_working_dirty, 0, sizeof(g_working_dirty));
    memset(&g_working_async, 0, sizeof(g_working_async));
    g_working_base_kind = PATTERN_WORKING_BASE_BLANK;
    g_working_project_slot = PATTERN_WORKING_INVALID_PROJECT;
}

uint8_t pattern_working_bank_start_project_mounted(uint8_t project_slot)
{
    if (project_slot >= PROJECT_STORAGE_SLOT_COUNT
            || g_working_async.state != WORKING_ASYNC_IDLE
            || working_clear_mounted() == 0U) return 0U;
    g_working_base_kind = PATTERN_WORKING_BASE_PROJECT;
    g_working_project_slot = project_slot;
    return 1U;
}

void pattern_working_bank_rebase_project(uint8_t project_slot)
{
    if (project_slot >= PROJECT_STORAGE_SLOT_COUNT
            || g_working_async.state != WORKING_ASYNC_IDLE) return;
    g_working_base_kind = PATTERN_WORKING_BASE_PROJECT;
    g_working_project_slot = project_slot;
}

void pattern_working_bank_start_blank(void)
{
    g_working_base_kind = PATTERN_WORKING_BASE_BLANK;
    g_working_project_slot = PATTERN_WORKING_INVALID_PROJECT;
    memset(g_working_dirty, 0, sizeof(g_working_dirty));
    if (g_working_async.state != WORKING_ASYNC_IDLE) return;
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PATTERN) == 0U) return;
    if (sd_access_fs_mount_if_needed() != 0U) (void)working_clear_mounted();
    sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);
}

pattern_working_base_kind_t pattern_working_bank_base_kind(void)
{
    return g_working_base_kind;
}

uint8_t pattern_working_bank_present(uint8_t bank, uint8_t pattern)
{
    if (working_slot_valid(bank, pattern) == 0U) return 0U;
    const uint32_t index = working_bit_index(bank, pattern);
    return (g_working_dirty[index >> 5U] &
            (UINT32_C(1) << (index & 31U))) != 0U ? 1U : 0U;
}

void pattern_working_bank_copy_dirty(uint32_t out_words[8])
{
    if (out_words != NULL)
        memcpy(out_words, g_working_dirty, sizeof(g_working_dirty));
}

uint8_t pattern_working_bank_restore_mounted(
    pattern_working_base_kind_t base_kind, uint8_t project_slot,
    const uint32_t dirty_words[8])
{
    if (dirty_words == NULL || g_working_async.state != WORKING_ASYNC_IDLE
            || (base_kind != PATTERN_WORKING_BASE_BLANK
                && base_kind != PATTERN_WORKING_BASE_PROJECT)
            || (base_kind == PATTERN_WORKING_BASE_PROJECT
                && project_slot >= PROJECT_STORAGE_SLOT_COUNT)) return 0U;
    g_working_base_kind = base_kind;
    g_working_project_slot = (base_kind == PATTERN_WORKING_BASE_PROJECT)
        ? project_slot : PATTERN_WORKING_INVALID_PROJECT;
    memcpy(g_working_dirty, dirty_words, sizeof(g_working_dirty));
    return 1U;
}

static uint8_t working_prepare_paths(uint8_t bank, uint8_t pattern)
{
    return (uint8_t)(project_storage_working_pattern_file(
                g_working_async.final_path,
                sizeof(g_working_async.final_path), bank, pattern)
        && working_side_path(g_working_async.temporary_path,
                sizeof(g_working_async.temporary_path),
                g_working_async.final_path, "TMP")
        && working_side_path(g_working_async.backup_path,
                sizeof(g_working_async.backup_path),
                g_working_async.final_path, "BAK"));
}

uint8_t pattern_working_bank_load_async_begin(
    uint8_t bank, uint8_t pattern, uint8_t *encoded,
    uint32_t encoded_capacity, persist_control_pattern_t *out)
{
    if (!working_slot_valid(bank, pattern)
            || !pattern_working_bank_present(bank, pattern)
            || encoded == NULL || encoded_capacity == 0U || out == NULL
            || g_working_async.state != WORKING_ASYNC_IDLE) return 0U;
    memset(&g_working_async, 0, sizeof(g_working_async));
    g_working_async.operation = PATTERN_WORKING_OPERATION_LOAD;
    g_working_async.state = WORKING_ASYNC_MOUNT;
    g_working_async.bank = bank;
    g_working_async.pattern = pattern;
    g_working_async.encoded = encoded;
    g_working_async.encoded_capacity = encoded_capacity;
    g_working_async.load_out = out;
    g_working_async.media_epoch = sd_access_media_epoch();
    if (!working_prepare_paths(bank, pattern))
    {
        memset(&g_working_async, 0, sizeof(g_working_async));
        return 0U;
    }
    return 1U;
}

uint8_t pattern_working_bank_reconcile_async_begin(
    uint8_t bank, uint8_t pattern,
    const persist_control_pattern_t *working,
    const persist_control_pattern_t *base_default,
    uint8_t saved_base_present,
    uint8_t *encoded, uint32_t encoded_capacity)
{
    if (!working_slot_valid(bank, pattern) || working == NULL
            || encoded == NULL || encoded_capacity == 0U
            || g_working_async.state != WORKING_ASYNC_IDLE) return 0U;
    working_memory_io_t memory = {
        .data = encoded, .capacity = encoded_capacity, .equal = 1U };
    const persist_codec_sink_t sink = { working_memory_write, &memory };
    uint32_t size = 0U;
    if (persist_codec_encode_pattern(working, &sink, &size) != PERSIST_CODEC_OK)
        return 0U;

    memset(&g_working_async, 0, sizeof(g_working_async));
    g_working_async.operation = PATTERN_WORKING_OPERATION_RECONCILE;
    g_working_async.bank = bank;
    g_working_async.pattern = pattern;
    g_working_async.encoded = encoded;
    g_working_async.encoded_capacity = encoded_capacity;
    g_working_async.encoded_size = size;
    g_working_async.media_epoch = sd_access_media_epoch();
    if (!working_prepare_paths(bank, pattern))
    {
        memset(&g_working_async, 0, sizeof(g_working_async));
        return 0U;
    }

    if (g_working_base_kind == PATTERN_WORKING_BASE_PROJECT
            && g_working_project_slot < PROJECT_STORAGE_SLOT_COUNT
            && saved_base_present != 0U)
    {
        if (!project_storage_pattern_file(g_working_async.base_path,
                sizeof(g_working_async.base_path), g_working_project_slot,
                bank, pattern))
        {
            memset(&g_working_async, 0, sizeof(g_working_async));
            return 0U;
        }
        g_working_async.base_equal = 1U;
        g_working_async.state = WORKING_ASYNC_MOUNT;
        return 1U;
    }

    if (base_default == NULL)
    {
        memset(&g_working_async, 0, sizeof(g_working_async));
        return 0U;
    }
    memory.position = 0U;
    memory.capacity = size;
    memory.equal = 1U;
    const persist_codec_sink_t compare = { working_memory_compare, &memory };
    uint32_t default_size = 0U;
    const persist_codec_result_t result = persist_codec_encode_pattern(
        base_default, &compare, &default_size);
    g_working_async.base_equal = (uint8_t)(result == PERSIST_CODEC_OK
        && default_size == size && memory.position == size
        && memory.equal != 0U);
    g_working_async.state = WORKING_ASYNC_MOUNT;
    return 1U;
}

uint8_t pattern_working_bank_discard_async_begin(uint8_t bank,
                                                 uint8_t pattern)
{
    if (!working_slot_valid(bank, pattern)
            || g_working_async.state != WORKING_ASYNC_IDLE) return 0U;
    memset(&g_working_async, 0, sizeof(g_working_async));
    g_working_async.operation = PATTERN_WORKING_OPERATION_DISCARD;
    g_working_async.state = WORKING_ASYNC_MOUNT;
    g_working_async.bank = bank;
    g_working_async.pattern = pattern;
    g_working_async.media_epoch = sd_access_media_epoch();
    if (!working_prepare_paths(bank, pattern))
    {
        memset(&g_working_async, 0, sizeof(g_working_async));
        return 0U;
    }
    return 1U;
}

void pattern_working_bank_mark_clean(uint8_t bank, uint8_t pattern)
{
    if (working_slot_valid(bank, pattern) != 0U)
        working_dirty_set(bank, pattern, 0U);
}

static void working_finish(uint8_t success)
{
    g_working_async.file_open = 0U;
    g_working_async.success = success ? 1U : 0U;
    g_working_async.result_ready = 1U;
    g_working_async.state = WORKING_ASYNC_DONE;
}

static void working_fail(void)
{
    if (g_working_async.file_open != 0U)
        g_working_async.state = WORKING_ASYNC_CLEAN_CLOSE;
    else if (g_working_async.operation == PATTERN_WORKING_OPERATION_RECONCILE)
        g_working_async.state = WORKING_ASYNC_CLEAN_TEMP;
    else working_finish(0U);
}

static sd_scheduler_background_admission_t working_admit(
    sd_scheduler_background_kind_t kind, uint32_t bytes)
{
    const sd_scheduler_background_request_t request = {
        bytes, g_working_async.media_epoch, kind };
    return sd_scheduler_runtime_background_try_begin(&request);
}

void pattern_working_bank_async_service(void)
{
    if (g_working_async.state == WORKING_ASYNC_IDLE
            || g_working_async.state == WORKING_ASYNC_DONE) return;
    if (g_working_async.state == WORKING_ASYNC_DECODE)
    {
        working_memory_io_t memory = {
            g_working_async.encoded, g_working_async.encoded_size, 0U, 1U };
        const persist_codec_source_t source = { working_memory_read,
            working_memory_reset, working_memory_size, &memory };
        working_finish(persist_codec_decode_pattern(&source,
            (persist_codec_pattern_staging_t *)g_working_async.load_out)
                == PERSIST_CODEC_OK);
        return;
    }

    uint32_t chunk = 0U;
    sd_scheduler_background_kind_t kind = SD_SCHEDULER_BACKGROUND_METADATA;
    if (g_working_async.state == WORKING_ASYNC_COMPARE_BASE
            || g_working_async.state == WORKING_ASYNC_TRANSFER)
    {
        chunk = g_working_async.encoded_size - g_working_async.offset;
        if (chunk > SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES)
            chunk = SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES;
        kind = SD_SCHEDULER_BACKGROUND_DATA;
    }
    const sd_scheduler_background_admission_t admission =
        working_admit(kind, chunk);
    if (admission == SD_SCHEDULER_BACKGROUND_NOT_NOW) return;
    if (admission != SD_SCHEDULER_BACKGROUND_GO)
    {
        working_fail();
        return;
    }

    FRESULT result = FR_OK;
    UINT transferred = 0U;
    switch (g_working_async.state)
    {
        case WORKING_ASYNC_MOUNT:
            if (!sd_access_fs_mount_if_needed()) working_fail();
            else if (g_working_async.operation == PATTERN_WORKING_OPERATION_LOAD)
                g_working_async.state = WORKING_ASYNC_RECOVER;
            else if (g_working_async.operation == PATTERN_WORKING_OPERATION_DISCARD)
                g_working_async.state = WORKING_ASYNC_DELETE;
            else if (g_working_async.base_path[0] != '\0')
                g_working_async.state = WORKING_ASYNC_OPEN_BASE;
            else g_working_async.state = WORKING_ASYNC_DECIDE;
            break;
        case WORKING_ASYNC_RECOVER:
            result = persistent_fatfs_recover_replace(
                g_working_async.final_path, g_working_async.temporary_path,
                g_working_async.backup_path);
            if (result != FR_OK) working_fail();
            else if (g_working_async.operation == PATTERN_WORKING_OPERATION_LOAD)
                g_working_async.state = WORKING_ASYNC_OPEN_WORKING;
            else if (g_working_async.writing_ready != 0U)
                g_working_async.state = WORKING_ASYNC_OPEN_WORKING;
            else g_working_async.state = WORKING_ASYNC_MKDIR_BRICK;
            break;
        case WORKING_ASYNC_OPEN_BASE:
            if (!persistent_fatfs_open_read(&g_working_async.file,
                    g_working_async.base_path)) { working_fail(); break; }
            g_working_async.file_open = 1U;
            if (g_working_async.file.size != g_working_async.encoded_size)
            {
                g_working_async.base_equal = 0U;
                g_working_async.state = WORKING_ASYNC_CLOSE_BASE;
            }
            else
            {
                g_working_async.offset = 0U;
                g_working_async.state = WORKING_ASYNC_COMPARE_BASE;
            }
            break;
        case WORKING_ASYNC_COMPARE_BASE:
            result = f_read(&g_working_async.file.file,
                            g_working_async.compare, chunk,
                            &transferred);
            if (result != FR_OK || transferred != chunk)
            { working_fail(); break; }
            if (memcmp(g_working_async.compare,
                    &g_working_async.encoded[g_working_async.offset],
                    chunk) != 0) g_working_async.base_equal = 0U;
            g_working_async.offset += chunk;
            if (g_working_async.offset == g_working_async.encoded_size)
                g_working_async.state = WORKING_ASYNC_CLOSE_BASE;
            break;
        case WORKING_ASYNC_CLOSE_BASE:
            result = persistent_fatfs_close_result(&g_working_async.file);
            g_working_async.file_open = 0U;
            if (result != FR_OK) working_fail();
            else g_working_async.state = WORKING_ASYNC_DECIDE;
            break;
        case WORKING_ASYNC_DECIDE:
            g_working_async.offset = 0U;
            if (g_working_async.base_equal != 0U)
                g_working_async.state = WORKING_ASYNC_DELETE;
            else g_working_async.state = WORKING_ASYNC_MKDIR_BRICK;
            break;
        case WORKING_ASYNC_MKDIR_BRICK:
            if (!working_mkdir(project_storage_internal_root)) working_fail();
            else g_working_async.state = WORKING_ASYNC_MKDIR_WORKING;
            break;
        case WORKING_ASYNC_MKDIR_WORKING:
            if (!working_mkdir(project_storage_working_root)) working_fail();
            else g_working_async.state = WORKING_ASYNC_MKDIR_PATTERNS;
            break;
        case WORKING_ASYNC_MKDIR_PATTERNS:
            if (!working_mkdir(project_storage_working_patterns_dir))
                working_fail();
            else
            {
                g_working_async.writing_ready = 1U;
                g_working_async.state = WORKING_ASYNC_RECOVER;
            }
            break;
        case WORKING_ASYNC_OPEN_WORKING:
            if (g_working_async.operation == PATTERN_WORKING_OPERATION_LOAD)
            {
                if (!persistent_fatfs_open_read(&g_working_async.file,
                        g_working_async.final_path)) { working_fail(); break; }
                g_working_async.file_open = 1U;
                g_working_async.encoded_size = g_working_async.file.size;
                if (g_working_async.encoded_size < PERSIST_CODEC_HEADER_BYTES
                        || g_working_async.encoded_size
                            > g_working_async.encoded_capacity)
                { working_fail(); break; }
                g_working_async.offset = 0U;
                g_working_async.state = WORKING_ASYNC_TRANSFER;
            }
            else
            {
                result = persistent_fatfs_open_write_result(
                    &g_working_async.file, g_working_async.temporary_path);
                if (result != FR_OK) { working_fail(); break; }
                g_working_async.file_open = 1U;
                g_working_async.state = WORKING_ASYNC_ALLOCATE;
            }
            break;
        case WORKING_ASYNC_ALLOCATE:
            result = f_lseek(&g_working_async.file.file,
                (FSIZE_t)(g_working_async.encoded_size - 1U));
            if (result == FR_OK)
                result = f_write(&g_working_async.file.file,
                    &g_working_async.encoded[g_working_async.encoded_size - 1U],
                    1U, &transferred);
            if (result == FR_OK && transferred != 1U) result = FR_DISK_ERR;
            if (result == FR_OK)
                result = f_lseek(&g_working_async.file.file, 0U);
            if (result != FR_OK) working_fail();
            else { g_working_async.offset = 0U;
                g_working_async.state = WORKING_ASYNC_TRANSFER; }
            break;
        case WORKING_ASYNC_TRANSFER:
            if (g_working_async.operation == PATTERN_WORKING_OPERATION_LOAD)
                result = f_read(&g_working_async.file.file,
                    &g_working_async.encoded[g_working_async.offset], chunk,
                    &transferred);
            else
                result = f_write(&g_working_async.file.file,
                    &g_working_async.encoded[g_working_async.offset], chunk,
                    &transferred);
            if (result != FR_OK || transferred != chunk)
            { working_fail(); break; }
            g_working_async.offset += chunk;
            if (g_working_async.offset == g_working_async.encoded_size)
                g_working_async.state =
                    (g_working_async.operation == PATTERN_WORKING_OPERATION_LOAD)
                    ? WORKING_ASYNC_CLOSE_WORKING : WORKING_ASYNC_SYNC;
            break;
        case WORKING_ASYNC_SYNC:
            if (f_sync(&g_working_async.file.file) != FR_OK) working_fail();
            else g_working_async.state = WORKING_ASYNC_CLOSE_WORKING;
            break;
        case WORKING_ASYNC_CLOSE_WORKING:
            result = persistent_fatfs_close_result(&g_working_async.file);
            g_working_async.file_open = 0U;
            if (result != FR_OK) working_fail();
            else g_working_async.state =
                (g_working_async.operation == PATTERN_WORKING_OPERATION_LOAD)
                ? WORKING_ASYNC_DECODE : WORKING_ASYNC_COMMIT;
            break;
        case WORKING_ASYNC_COMMIT:
            result = persistent_fatfs_commit_replace(
                g_working_async.final_path, g_working_async.temporary_path,
                g_working_async.backup_path);
            if (result != FR_OK) working_fail();
            else
            {
                working_dirty_set(g_working_async.bank,
                                  g_working_async.pattern, 1U);
                working_finish(1U);
            }
            break;
        case WORKING_ASYNC_DELETE:
            if (working_unlink_optional(g_working_async.final_path)
                    && working_unlink_optional(g_working_async.temporary_path)
                    && working_unlink_optional(g_working_async.backup_path))
            {
                working_dirty_set(g_working_async.bank,
                                  g_working_async.pattern, 0U);
                working_finish(1U);
            }
            else working_fail();
            break;
        case WORKING_ASYNC_CLEAN_CLOSE:
            (void)persistent_fatfs_close_result(&g_working_async.file);
            g_working_async.file_open = 0U;
            g_working_async.state = WORKING_ASYNC_CLEAN_TEMP;
            break;
        case WORKING_ASYNC_CLEAN_TEMP:
            (void)f_unlink(g_working_async.temporary_path);
            working_finish(0U);
            break;
        default:
            working_fail();
            break;
    }
    sd_scheduler_runtime_background_end();
}

uint8_t pattern_working_bank_async_busy(void)
{
    return (g_working_async.state != WORKING_ASYNC_IDLE) ? 1U : 0U;
}

uint8_t pattern_working_bank_async_take_result(
    pattern_working_operation_t *operation, uint8_t *bank, uint8_t *pattern,
    uint8_t *success)
{
    if (g_working_async.state != WORKING_ASYNC_DONE
            || g_working_async.result_ready == 0U) return 0U;
    if (operation != NULL) *operation = g_working_async.operation;
    if (bank != NULL) *bank = g_working_async.bank;
    if (pattern != NULL) *pattern = g_working_async.pattern;
    if (success != NULL) *success = g_working_async.success;
    memset(&g_working_async, 0, sizeof(g_working_async));
    return 1U;
}
