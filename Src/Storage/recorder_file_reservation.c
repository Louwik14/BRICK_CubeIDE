#include "Storage/recorder_file_reservation.h"

#include <string.h>

#include "SD/bsp_driver_sd.h"
#include "SD/sd_block_device.h"
#include "Storage/sd_access_gate.h"
#include "main.h"
#include "stm32h7xx_hal.h"

#define RECORDER_FILE_RESERVATION_SECTOR_BYTES 512U

static volatile uint8_t g_recorder_file_reservation_operation_active;
static uint8_t recorder_file_copy_path(char *dst, const char *src)
{
    if((dst == 0) || (src == 0) || (src[0] == '\0'))
    {
        return 0U;
    }
    for(uint32_t i = 0U; i < RECORDER_FILE_RESERVATION_PATH_MAX; ++i)
    {
        dst[i] = src[i];
        if(src[i] == '\0')
        {
            return 1U;
        }
    }
    dst[0] = '\0';
    return 0U;
}

static recorder_file_reservation_result_t recorder_file_fs_result(FRESULT fr)
{
    if(fr == FR_OK)
    {
        return RECORDER_FILE_RESERVATION_OK;
    }
    if(fr == FR_DENIED)
    {
        return RECORDER_FILE_RESERVATION_NO_SPACE;
    }
    if(fr == FR_NOT_ENOUGH_CORE)
    {
        return RECORDER_FILE_RESERVATION_MAP_FULL;
    }
    return RECORDER_FILE_RESERVATION_FS_ERROR;
}

static uint8_t recorder_file_begin_storage_operation(void)
{
    if((__get_IPSR() != 0U) || (g_recorder_file_reservation_operation_active != 0U))
    {
        return 0U;
    }
    if(sd_block_device_async_pending_count() != 0U)
    {
        return 0U;
    }
    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_SCHEDULED_RECORDER) == 0U)
    {
        return 0U;
    }
    if((sd_block_device_async_pending_count() != 0U)
            || (BSP_SD_GetCardState() != SD_TRANSFER_OK))
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_SCHEDULED_RECORDER);
        return 0U;
    }
    g_recorder_file_reservation_operation_active = 1U;
    return 1U;
}

static void recorder_file_end_storage_operation(void)
{
    g_recorder_file_reservation_operation_active = 0U;
    sd_access_gate_release(SD_ACCESS_CLIENT_SCHEDULED_RECORDER);
}

static void recorder_file_publish(recorder_file_reservation_t *session)
{
    session->publish_sequence++;
    __DMB();
    session->published_extent_count = session->extent_count;
    session->published_reserved_file_bytes = session->fs_state.reserved_bytes;
    session->published_valid_file_bytes = session->fs_state.valid_bytes;
    session->published_media_epoch = sd_access_media_epoch();
    __DMB();
    session->publish_sequence++;
}

static uint8_t recorder_file_import_extents(recorder_file_reservation_t *session,
                                            uint16_t first,
                                            uint16_t count)
{
    if(((uint32_t)first + count) > RECORDER_FILE_RESERVATION_MAX_EXTENTS)
    {
        return 0U;
    }
    for(uint16_t i = first; i < (uint16_t)(first + count); ++i)
    {
        const FF_BRICK_REC_EXTENT *const src = &session->fs_extents[i];
        if((src->file_sector_start > UINT32_MAX) || (src->sector_count == 0U))
        {
            return 0U;
        }
        session->physical_extents[i].file_sector_start = (uint32_t)src->file_sector_start;
        session->physical_extents[i].lba_start = src->lba_start;
        session->physical_extents[i].sector_count = src->sector_count;
    }
    return 1U;
}

static void recorder_file_update_public_sizes(recorder_file_reservation_t *session)
{
    session->reserved_bytes = (session->fs_state.reserved_bytes > session->header_bytes)
        ? (session->fs_state.reserved_bytes - session->header_bytes) : 0U;
    session->valid_bytes = (session->fs_state.valid_bytes > session->header_bytes)
        ? (session->fs_state.valid_bytes - session->header_bytes) : 0U;
}

void recorder_file_reservation_init(recorder_file_reservation_t *session)
{
    if(session != 0)
    {
        memset(session, 0, sizeof(*session));
    }
}

static recorder_file_reservation_result_t recorder_file_extend_locked(
    recorder_file_reservation_t *session,
    uint64_t additional_bytes)
{
    if((additional_bytes == 0U)
            || (session->fs_state.reserved_bytes > (uint64_t)((FSIZE_t)-1) - additional_bytes))
    {
        return RECORDER_FILE_RESERVATION_INVALID_ARG;
    }
    const FSIZE_t before = session->fs_state.reserved_bytes;
    const FSIZE_t target = before + (FSIZE_t)additional_bytes;
    const uint16_t old_count = session->extent_count;
    UINT added = 0U;
    const FRESULT fr = f_brick_rec_reserve(&session->file,
                                           target,
                                           &session->fs_state,
                                           &session->fs_extents[old_count],
                                           RECORDER_FILE_RESERVATION_MAX_EXTENTS - old_count,
                                           &added,
                                           0);
    if(fr != FR_OK)
    {
        if((fr != FR_DENIED) && (fr != FR_NOT_ENOUGH_CORE))
        {
            session->failed = 1U;
        }
        return recorder_file_fs_result(fr);
    }
    if((added > UINT16_MAX) || ((uint32_t)old_count + added > RECORDER_FILE_RESERVATION_MAX_EXTENTS))
    {
        session->failed = 1U;
        return RECORDER_FILE_RESERVATION_MAP_FULL;
    }
    if(recorder_file_import_extents(session, old_count, (uint16_t)added) == 0U)
    {
        session->failed = 1U;
        return RECORDER_FILE_RESERVATION_MAP_FULL;
    }
    if((session->job_phase == RECORDER_FILE_JOB_EXTEND) && (old_count != 0U)
            && (added == 1U))
    {
        FF_BRICK_REC_EXTENT *const previous = &session->fs_extents[old_count - 1U];
        const FF_BRICK_REC_EXTENT *const next = &session->fs_extents[old_count];
        sample_stream_physical_extent_t *const physical =
            &session->physical_extents[old_count - 1U];
        if((previous->first_cluster + previous->cluster_count == next->first_cluster)
                && (previous->lba_start + previous->sector_count == next->lba_start)
                && (previous->file_sector_start + previous->sector_count
                    == next->file_sector_start))
        {
            previous->cluster_count += next->cluster_count;
            previous->sector_count += next->sector_count;
            physical->sector_count += next->sector_count;
            added = 0U;
        }
    }
    session->extent_count = (uint16_t)(old_count + added);
    recorder_file_update_public_sizes(session);
    recorder_file_publish(session);
    return (session->fs_state.reserved_bytes < target)
        ? RECORDER_FILE_RESERVATION_PARTIAL : RECORDER_FILE_RESERVATION_OK;
}

recorder_file_reservation_result_t recorder_file_reservation_extend_begin(
    recorder_file_reservation_t *session,
    uint64_t additional_bytes)
{
    if((session == 0) || (session->open == 0U) || (session->failed != 0U)
            || (session->finalizing != 0U) || (session->job_phase != RECORDER_FILE_JOB_NONE))
    {
        return RECORDER_FILE_RESERVATION_INVALID_STATE;
    }
    if((additional_bytes == 0U)
            || (session->fs_state.reserved_bytes
                > (uint64_t)((FSIZE_t)-1) - additional_bytes))
    {
        return RECORDER_FILE_RESERVATION_INVALID_ARG;
    }
    session->job_target_file_bytes = session->fs_state.reserved_bytes + additional_bytes;
    session->job_media_epoch = sd_access_media_epoch();
    session->job_old_extent_count = session->extent_count;
    session->job_added_extent_count = 0U;
    const FRESULT fr = f_brick_rec_reserve_begin(&session->job_cont.reserve,
        &session->file, (FSIZE_t)session->job_target_file_bytes,
        &session->fs_state, &session->fs_extents[session->extent_count],
        RECORDER_FILE_RESERVATION_MAX_EXTENTS - session->extent_count,
        &session->job_added_extent_count, 0, session->metadata_staging,
        sizeof(session->metadata_staging));
    if(fr != FR_OK)
    {
        return recorder_file_fs_result(fr);
    }
    session->job_result = RECORDER_FILE_RESERVATION_SD_BUSY;
    session->job_phase = RECORDER_FILE_JOB_EXTEND;
    sd_access_gate_set_recorder_fs_logical_active(1U);
    return RECORDER_FILE_RESERVATION_OK;
}

static recorder_file_reservation_result_t recorder_file_sync_begin(
    recorder_file_reservation_t *session,
    recorder_file_job_phase_t phase,
    FSIZE_t data_file_bytes,
    FSIZE_t valid_file_bytes)
{
    if((session == 0) || (session->open == 0U) || (session->failed != 0U)
            || (session->job_phase != RECORDER_FILE_JOB_NONE)
            || (valid_file_bytes > data_file_bytes)
            || (data_file_bytes > session->fs_state.reserved_bytes))
    {
        return RECORDER_FILE_RESERVATION_INVALID_STATE;
    }
    const FRESULT fr = f_brick_meta_object_sync_begin(&session->job_cont.sync,
        &session->file, data_file_bytes, valid_file_bytes,
        session->metadata_staging, sizeof(session->metadata_staging));
    if(fr != FR_OK) return recorder_file_fs_result(fr);
    session->job_target_file_bytes = valid_file_bytes;
    session->job_media_epoch = sd_access_media_epoch();
    session->job_result = RECORDER_FILE_RESERVATION_SD_BUSY;
    session->job_phase = phase;
    sd_access_gate_set_recorder_fs_logical_active(1U);
    return RECORDER_FILE_RESERVATION_OK;
}

recorder_file_reservation_result_t recorder_file_reservation_commit_begin(
    recorder_file_reservation_t *session,
    uint64_t valid_bytes)
{
    if((session == 0) || (valid_bytes > session->reserved_bytes)
            || (valid_bytes > (uint64_t)((FSIZE_t)-1) - session->header_bytes))
    {
        return RECORDER_FILE_RESERVATION_INVALID_ARG;
    }
    return recorder_file_sync_begin(session, RECORDER_FILE_JOB_COMMIT,
        session->fs_state.reserved_bytes,
        (FSIZE_t)(session->header_bytes + valid_bytes));
}

recorder_file_reservation_result_t recorder_file_reservation_sync_begin(
    recorder_file_reservation_t *session)
{
    if(session == 0) return RECORDER_FILE_RESERVATION_INVALID_ARG;
    return recorder_file_sync_begin(session, RECORDER_FILE_JOB_SYNC,
        session->file.obj.objsize, session->file.obj.objsize);
}

static recorder_file_reservation_result_t recorder_file_release_targets(
    const recorder_file_reservation_t *session,
    FSIZE_t *keep_bytes,
    uint32_t *keep_clusters,
    uint32_t *keep_last,
    uint32_t *first_unused)
{
    if(session->fs_state.cluster_bytes == 0U)
        return RECORDER_FILE_RESERVATION_INVALID_STATE;
    const uint64_t keep = (uint64_t)session->header_bytes + session->valid_bytes;
    uint32_t clusters = (uint32_t)(keep / session->fs_state.cluster_bytes);
    if((keep % session->fs_state.cluster_bytes) != 0U) clusters++;
    uint32_t last = 0U;
    uint32_t first = session->fs_state.first_cluster;
    if(clusters != 0U)
    {
        const uint32_t keep_index = clusters - 1U;
        for(uint16_t i = 0U; i < session->extent_count; ++i)
        {
            const FF_BRICK_REC_EXTENT *const extent = &session->fs_extents[i];
            const uint32_t first_index = (uint32_t)(extent->file_sector_start
                / (session->fs_state.cluster_bytes / RECORDER_FILE_RESERVATION_SECTOR_BYTES));
            if((keep_index >= first_index)
                    && (keep_index < (first_index + extent->cluster_count)))
            {
                last = extent->first_cluster + (keep_index - first_index);
                if((keep_index + 1U) < (first_index + extent->cluster_count))
                {
                    first = last + 1U;
                }
                else if((uint16_t)(i + 1U) < session->extent_count)
                {
                    first = session->fs_extents[i + 1U].first_cluster;
                }
                break;
            }
        }
        if(last == 0U) return RECORDER_FILE_RESERVATION_INVALID_STATE;
    }
    *keep_bytes = (FSIZE_t)keep;
    *keep_clusters = clusters;
    *keep_last = last;
    *first_unused = first;
    return RECORDER_FILE_RESERVATION_OK;
}

static void recorder_file_retain_extents(
    recorder_file_reservation_t *session,
    uint32_t keep_clusters)
{
    uint16_t retained = 0U;
    uint32_t remaining = keep_clusters;
    while((retained < session->extent_count) && (remaining != 0U))
    {
        FF_BRICK_REC_EXTENT *const extent = &session->fs_extents[retained];
        sample_stream_physical_extent_t *const physical = &session->physical_extents[retained];
        if(remaining < extent->cluster_count)
        {
            extent->cluster_count = remaining;
            extent->sector_count = remaining
                * (session->fs_state.cluster_bytes / RECORDER_FILE_RESERVATION_SECTOR_BYTES);
            physical->sector_count = extent->sector_count;
            remaining = 0U;
            retained++;
            break;
        }
        remaining -= extent->cluster_count;
        retained++;
    }
    session->extent_count = retained;
}

recorder_file_reservation_result_t recorder_file_reservation_release_begin(
    recorder_file_reservation_t *session)
{
    if((session == 0) || (session->open == 0U) || (session->failed != 0U)
            || (session->job_phase != RECORDER_FILE_JOB_NONE))
    {
        return RECORDER_FILE_RESERVATION_INVALID_STATE;
    }
    FSIZE_t keep_bytes;
    uint32_t keep_clusters;
    uint32_t keep_last;
    uint32_t first_unused;
    const recorder_file_reservation_result_t targets = recorder_file_release_targets(
        session, &keep_bytes, &keep_clusters, &keep_last, &first_unused);
    if(targets != RECORDER_FILE_RESERVATION_OK) return targets;
    const FRESULT fr = f_brick_rec_release_begin(&session->job_cont.release,
        &session->file, keep_bytes, keep_last, first_unused, &session->fs_state,
        0, session->metadata_staging, sizeof(session->metadata_staging));
    if(fr != FR_OK) return recorder_file_fs_result(fr);
    session->finalizing = 1U;
    session->job_target_file_bytes = keep_clusters;
    session->job_media_epoch = sd_access_media_epoch();
    session->job_result = RECORDER_FILE_RESERVATION_SD_BUSY;
    session->job_phase = RECORDER_FILE_JOB_RELEASE;
    sd_access_gate_set_recorder_fs_logical_active(1U);
    return RECORDER_FILE_RESERVATION_OK;
}

recorder_file_reservation_result_t recorder_file_reservation_job_step(
    recorder_file_reservation_t *session)
{
    if((session == 0) || (session->job_phase == RECORDER_FILE_JOB_NONE))
    {
        return RECORDER_FILE_RESERVATION_INVALID_STATE;
    }
    if(session->job_phase == RECORDER_FILE_JOB_TERMINAL)
    {
        return session->job_result;
    }
    if(session->job_media_epoch != sd_access_media_epoch())
    {
        session->failed = 1U;
        session->job_result = RECORDER_FILE_RESERVATION_FS_ERROR;
        session->job_phase = RECORDER_FILE_JOB_TERMINAL;
        return session->job_result;
    }
    if(session->job_io_active != 0U)
    {
        return RECORDER_FILE_RESERVATION_IO_STARTED;
    }
    if(recorder_file_begin_storage_operation() == 0U)
    {
        return RECORDER_FILE_RESERVATION_SD_BUSY;
    }

    const recorder_file_job_phase_t active_phase = session->job_phase;
    const FF_META_REQUEST *request = 0;
    const FF_META_STEP_RESULT step = (active_phase == RECORDER_FILE_JOB_EXTEND)
        ? f_brick_rec_reserve_step(&session->job_cont.reserve, &request)
        : (active_phase == RECORDER_FILE_JOB_RELEASE)
            ? f_brick_rec_release_step(&session->job_cont.release, &request)
            : f_brick_meta_object_sync_step(&session->job_cont.sync, &request);
    if(step == FF_META_STEP_NEED_IO)
    {
        if((request == 0) || (request->count != 1U))
        {
            recorder_file_end_storage_operation();
            session->failed = 1U;
            session->job_result = RECORDER_FILE_RESERVATION_FS_ERROR;
            session->job_phase = RECORDER_FILE_JOB_TERMINAL;
            return session->job_result;
        }
        session->job_io_identity++;
        if(session->job_io_identity == 0U) session->job_io_identity = 1U;
        const sd_block_device_result_t submit = (request->operation == FF_META_IO_WRITE)
            ? sd_block_device_async_write_submit(request->sector, request->count,
                request->buffer, session->job_io_identity)
            : sd_block_device_async_read_submit(request->sector, request->count,
                request->buffer, session->job_io_identity);
        if((submit == SD_BLOCK_DEVICE_BUSY) || (submit == SD_BLOCK_DEVICE_QUEUE_FULL))
        {
            recorder_file_end_storage_operation();
            return RECORDER_FILE_RESERVATION_SD_BUSY;
        }
        if(submit != SD_BLOCK_DEVICE_OK)
        {
            recorder_file_end_storage_operation();
            session->failed = 1U;
            session->job_result = RECORDER_FILE_RESERVATION_FS_ERROR;
            session->job_phase = RECORDER_FILE_JOB_TERMINAL;
            return session->job_result;
        }
        session->job_io_lba = request->sector;
        session->job_io_sequence = request->sequence;
        session->job_io_operation = (uint8_t)request->operation;
        session->job_io_buffer = request->buffer;
        session->job_io_active = 1U;
        const FRESULT started = (active_phase == RECORDER_FILE_JOB_EXTEND)
            ? f_brick_rec_reserve_io_started(&session->job_cont.reserve,
                request->sequence)
            : (active_phase == RECORDER_FILE_JOB_RELEASE)
                ? f_brick_rec_release_io_started(&session->job_cont.release,
                    request->sequence)
                : f_brick_meta_object_sync_io_started(&session->job_cont.sync,
                    request->sequence);
        if(started != FR_OK)
        {
            Error_Handler();
        }
        recorder_file_end_storage_operation();
        return RECORDER_FILE_RESERVATION_IO_STARTED;
    }
    recorder_file_end_storage_operation();

    if(step == FF_META_STEP_ERROR)
    {
        const FRESULT result = (active_phase == RECORDER_FILE_JOB_EXTEND)
            ? session->job_cont.reserve.result
            : (active_phase == RECORDER_FILE_JOB_RELEASE)
                ? session->job_cont.release.result : session->job_cont.sync.result;
        session->job_result = recorder_file_fs_result(result);
        session->failed = 1U;
        session->job_phase = RECORDER_FILE_JOB_TERMINAL;
        return session->job_result;
    }
    if(step == FF_META_STEP_DONE)
    {
        if(active_phase == RECORDER_FILE_JOB_EXTEND)
        {
            uint16_t added = (uint16_t)session->job_added_extent_count;
            const uint16_t old_count = session->job_old_extent_count;
            if((session->job_added_extent_count > UINT16_MAX)
                    || ((uint32_t)old_count + added > RECORDER_FILE_RESERVATION_MAX_EXTENTS)
                    || (recorder_file_import_extents(session, old_count, added) == 0U))
            {
                session->failed = 1U;
                session->job_result = RECORDER_FILE_RESERVATION_MAP_FULL;
                session->job_phase = RECORDER_FILE_JOB_TERMINAL;
                return session->job_result;
            }
            session->extent_count = (uint16_t)(old_count + added);
        }
        else if(active_phase == RECORDER_FILE_JOB_COMMIT)
        {
            session->fs_state.valid_bytes = (FSIZE_t)session->job_target_file_bytes;
        }
        else if(active_phase == RECORDER_FILE_JOB_RELEASE)
        {
            recorder_file_retain_extents(session,
                (uint32_t)session->job_target_file_bytes);
        }
        if(active_phase != RECORDER_FILE_JOB_EXTEND)
        {
            session->fs_state.chain_status = session->file.obj.stat;
        }
        recorder_file_update_public_sizes(session);
        recorder_file_publish(session);
        session->job_result = RECORDER_FILE_RESERVATION_OK;
        session->job_phase = RECORDER_FILE_JOB_TERMINAL;
        return RECORDER_FILE_RESERVATION_OK;
    }
    return RECORDER_FILE_RESERVATION_PROGRESS;
}

recorder_file_reservation_result_t recorder_file_reservation_job_poll(
    recorder_file_reservation_t *session)
{
    if((session == 0) || (session->job_io_active == 0U)
            || (session->job_phase == RECORDER_FILE_JOB_NONE)
            || (session->job_phase == RECORDER_FILE_JOB_TERMINAL))
    {
        return RECORDER_FILE_RESERVATION_INVALID_STATE;
    }
    sd_block_device_async_poll();
    sd_block_device_async_completion_t completion;
    if(sd_block_device_async_take_completion(&completion) == 0U)
    {
        return (sd_block_device_async_hardware_state() == SD_BLOCK_DEVICE_HW_ABORTING)
            ? RECORDER_FILE_RESERVATION_RECOVERY_ABORT
            : RECORDER_FILE_RESERVATION_IO_STARTED;
    }
    session->job_io_active = 0U;
    const sd_block_device_operation_t expected_operation =
        (session->job_io_operation == FF_META_IO_WRITE)
            ? SD_BLOCK_DEVICE_OPERATION_WRITE : SD_BLOCK_DEVICE_OPERATION_READ;
    const void *const completed_buffer = (expected_operation == SD_BLOCK_DEVICE_OPERATION_WRITE)
        ? completion.src : completion.dst;
    const FRESULT io_result = (completion.result == SD_BLOCK_DEVICE_OK)
        ? FR_OK : FR_DISK_ERR;
    if((completion.operation != expected_operation)
            || (completion.lba != session->job_io_lba)
            || (completion.sector_count != 1U)
            || (completed_buffer != session->job_io_buffer)
            || (completion.media_epoch != session->job_media_epoch)
            || (completion.owner_generation != session->job_io_identity)
            || (((session->job_phase == RECORDER_FILE_JOB_EXTEND)
                ? f_brick_rec_reserve_io_complete(&session->job_cont.reserve,
                    session->job_io_sequence, io_result)
                : (session->job_phase == RECORDER_FILE_JOB_RELEASE)
                    ? f_brick_rec_release_io_complete(&session->job_cont.release,
                        session->job_io_sequence, io_result)
                    : f_brick_meta_object_sync_io_complete(&session->job_cont.sync,
                        session->job_io_sequence, io_result)) != FR_OK))
    {
        session->failed = 1U;
        session->job_result = RECORDER_FILE_RESERVATION_FS_ERROR;
        session->job_phase = RECORDER_FILE_JOB_TERMINAL;
        return session->job_result;
    }
    if(io_result != FR_OK)
    {
        session->failed = 1U;
        session->job_result = RECORDER_FILE_RESERVATION_FS_ERROR;
        session->job_phase = RECORDER_FILE_JOB_TERMINAL;
        return session->job_result;
    }
    return RECORDER_FILE_RESERVATION_PROGRESS;
}

uint8_t recorder_file_reservation_job_active(
    const recorder_file_reservation_t *session)
{
    return ((session != 0) && (session->job_phase != RECORDER_FILE_JOB_NONE)) ? 1U : 0U;
}

void recorder_file_reservation_job_finish(recorder_file_reservation_t *session)
{
    if((session != 0) && (session->job_phase == RECORDER_FILE_JOB_TERMINAL))
    {
        session->job_phase = RECORDER_FILE_JOB_NONE;
        session->job_target_file_bytes = 0U;
        sd_access_gate_set_recorder_fs_logical_active(0U);
    }
}

void recorder_file_reservation_job_cancel(recorder_file_reservation_t *session)
{
    if((session != 0) && (session->job_phase == RECORDER_FILE_JOB_EXTEND))
    {
        f_brick_rec_reserve_request_stop(&session->job_cont.reserve);
        session->job_target_file_bytes = session->fs_state.reserved_bytes;
    }
}

recorder_file_reservation_result_t recorder_file_reservation_create(
    recorder_file_reservation_t *session,
    const char *temporary_path,
    uint32_t header_bytes,
    uint64_t initial_reserve_bytes)
{
    if((session == 0) || (temporary_path == 0) || (session->open != 0U)
            || (initial_reserve_bytes > (uint64_t)((FSIZE_t)-1) - header_bytes))
    {
        return RECORDER_FILE_RESERVATION_INVALID_ARG;
    }
    if(recorder_file_begin_storage_operation() == 0U)
    {
        return RECORDER_FILE_RESERVATION_SD_BUSY;
    }
    recorder_file_reservation_init(session);
    session->header_bytes = header_bytes;
    if(recorder_file_copy_path(session->path, temporary_path) == 0U)
    {
        recorder_file_end_storage_operation();
        return RECORDER_FILE_RESERVATION_INVALID_ARG;
    }
    FRESULT fr = f_open(&session->file, temporary_path,
                        FA_CREATE_NEW | FA_WRITE | FA_READ);
    if(fr != FR_OK)
    {
        recorder_file_end_storage_operation();
        return recorder_file_fs_result(fr);
    }
    session->open = 1U;
    UINT recovered_count = 0U;
    fr = f_brick_rec_recover(&session->file, &session->fs_state,
                             session->fs_extents,
                             RECORDER_FILE_RESERVATION_MAX_EXTENTS,
                             &recovered_count, 0);
    recorder_file_reservation_result_t result = recorder_file_fs_result(fr);
    if((fr == FR_OK) && (initial_reserve_bytes != 0U))
    {
        const uint64_t total = (uint64_t)header_bytes + initial_reserve_bytes;
        result = recorder_file_extend_locked(session, total);
    }
    if((result != RECORDER_FILE_RESERVATION_OK)
            && (result != RECORDER_FILE_RESERVATION_PARTIAL))
    {
        (void)f_close(&session->file);
        session->open = 0U;
        session->failed = 1U;
    }
    recorder_file_end_storage_operation();
    return result;
}

recorder_file_reservation_result_t recorder_file_reservation_recover(
    recorder_file_reservation_t *session,
    const char *temporary_path,
    uint32_t header_bytes)
{
    if((session == 0) || (temporary_path == 0) || (session->open != 0U))
    {
        return RECORDER_FILE_RESERVATION_INVALID_ARG;
    }
    if(recorder_file_begin_storage_operation() == 0U)
    {
        return RECORDER_FILE_RESERVATION_SD_BUSY;
    }
    recorder_file_reservation_init(session);
    session->header_bytes = header_bytes;
    if(recorder_file_copy_path(session->path, temporary_path) == 0U)
    {
        recorder_file_end_storage_operation();
        return RECORDER_FILE_RESERVATION_INVALID_ARG;
    }
    FRESULT fr = f_open(&session->file, temporary_path, FA_WRITE | FA_READ);
    if(fr == FR_OK)
    {
        session->open = 1U;
        UINT count = 0U;
        fr = f_brick_rec_recover(&session->file, &session->fs_state,
                                 session->fs_extents,
                                 RECORDER_FILE_RESERVATION_MAX_EXTENTS,
                                 &count, 0);
        if((fr == FR_OK) && (count <= RECORDER_FILE_RESERVATION_MAX_EXTENTS)
                && (recorder_file_import_extents(session, 0U, (uint16_t)count) != 0U))
        {
            session->extent_count = (uint16_t)count;
            recorder_file_update_public_sizes(session);
            recorder_file_publish(session);
        }
        else
        {
            if(fr == FR_OK) fr = FR_NOT_ENOUGH_CORE;
            (void)f_close(&session->file);
            session->open = 0U;
            session->failed = 1U;
        }
    }
    else
    {
    }
    recorder_file_end_storage_operation();
    return recorder_file_fs_result(fr);
}

recorder_file_reservation_result_t recorder_file_reservation_extend(
    recorder_file_reservation_t *session,
    uint64_t additional_bytes)
{
    if((session == 0) || (session->open == 0U) || (session->failed != 0U)
            || (session->finalizing != 0U))
    {
        return RECORDER_FILE_RESERVATION_INVALID_STATE;
    }
    if(recorder_file_begin_storage_operation() == 0U)
    {
        return RECORDER_FILE_RESERVATION_SD_BUSY;
    }
    const recorder_file_reservation_result_t result =
        recorder_file_extend_locked(session, additional_bytes);
    recorder_file_end_storage_operation();
    return result;
}

recorder_file_reservation_result_t recorder_file_reservation_commit_valid(
    recorder_file_reservation_t *session,
    uint64_t valid_bytes)
{
    if((session == 0) || (session->open == 0U) || (session->failed != 0U)
            || (valid_bytes > session->reserved_bytes)
            || (valid_bytes > (uint64_t)((FSIZE_t)-1) - session->header_bytes))
    {
        return RECORDER_FILE_RESERVATION_INVALID_ARG;
    }
    if(recorder_file_begin_storage_operation() == 0U)
    {
        return RECORDER_FILE_RESERVATION_SD_BUSY;
    }
    const FRESULT fr = f_brick_rec_commit(&session->file,
                                          (FSIZE_t)(session->header_bytes + valid_bytes),
                                          &session->fs_state, 0);
    if(fr == FR_OK)
    {
        session->valid_bytes = valid_bytes;
        recorder_file_publish(session);
    }
    else
    {
        session->failed = 1U;
    }
    recorder_file_end_storage_operation();
    return recorder_file_fs_result(fr);
}

recorder_file_reservation_result_t recorder_file_reservation_release_unused(
    recorder_file_reservation_t *session)
{
    if((session == 0) || (session->open == 0U) || (session->failed != 0U))
    {
        return RECORDER_FILE_RESERVATION_INVALID_STATE;
    }
    session->finalizing = 1U;
    FSIZE_t keep_bytes;
    uint32_t keep_clusters;
    uint32_t keep_last;
    uint32_t first_unused;
    const recorder_file_reservation_result_t targets = recorder_file_release_targets(
        session, &keep_bytes, &keep_clusters, &keep_last, &first_unused);
    if(targets != RECORDER_FILE_RESERVATION_OK) return targets;
    if(recorder_file_begin_storage_operation() == 0U)
    {
        return RECORDER_FILE_RESERVATION_SD_BUSY;
    }
    const FRESULT fr = f_brick_rec_release_tail(&session->file,
                                                (FSIZE_t)keep_bytes,
                                                keep_last,
                                                first_unused,
                                                &session->fs_state,
                                                0);
    if(fr == FR_OK)
    {
        recorder_file_retain_extents(session, keep_clusters);
        recorder_file_update_public_sizes(session);
        recorder_file_publish(session);
    }
    else
    {
        session->failed = 1U;
    }
    recorder_file_end_storage_operation();
    return recorder_file_fs_result(fr);
}

recorder_file_reservation_result_t recorder_file_reservation_close(
    recorder_file_reservation_t *session)
{
    if((session == 0) || (session->open == 0U))
    {
        return RECORDER_FILE_RESERVATION_INVALID_STATE;
    }
    if(recorder_file_begin_storage_operation() == 0U)
    {
        return RECORDER_FILE_RESERVATION_SD_BUSY;
    }
    const FRESULT fr = f_close(&session->file);
    if(fr == FR_OK) session->open = 0U;
    else session->failed = 1U;
    recorder_file_end_storage_operation();
    return recorder_file_fs_result(fr);
}

recorder_file_reservation_result_t recorder_file_reservation_rename_closed(
    recorder_file_reservation_t *session,
    const char *final_path)
{
    if((session == 0) || (final_path == 0) || (session->open != 0U)
            || (session->path[0] == '\0'))
    {
        return RECORDER_FILE_RESERVATION_INVALID_STATE;
    }
    if(recorder_file_begin_storage_operation() == 0U)
    {
        return RECORDER_FILE_RESERVATION_SD_BUSY;
    }
    const FRESULT fr = f_rename(session->path, final_path);
    if((fr == FR_OK) && (recorder_file_copy_path(session->path, final_path) == 0U))
    {
        session->path[0] = '\0';
    }
    recorder_file_end_storage_operation();
    return recorder_file_fs_result(fr);
}

uint8_t recorder_file_reservation_map_snapshot(
    const recorder_file_reservation_t *session,
    recorder_file_reservation_map_snapshot_t *out_snapshot)
{
    if((session == 0) || (out_snapshot == 0))
    {
        return 0U;
    }
    for(uint8_t attempt = 0U; attempt < 3U; ++attempt)
    {
        const uint32_t before = session->publish_sequence;
        if((before == 0U) || ((before & 1U) != 0U)) continue;
        out_snapshot->extents = session->physical_extents;
        out_snapshot->reserved_file_bytes = session->published_reserved_file_bytes;
        out_snapshot->valid_file_bytes = session->published_valid_file_bytes;
        out_snapshot->media_epoch = session->published_media_epoch;
        out_snapshot->extent_count = session->published_extent_count;
        out_snapshot->sector_size = RECORDER_FILE_RESERVATION_SECTOR_BYTES;
        __DMB();
        if(before == session->publish_sequence)
        {
            return 1U;
        }
    }
    return 0U;
}

uint8_t recorder_file_reservation_map_resolve(
    const recorder_file_reservation_map_snapshot_t *snapshot,
    uint64_t file_byte_offset,
    uint32_t requested_bytes,
    sample_stream_physical_span_t *out_span)
{
    if((snapshot == 0) || (out_span == 0) || (requested_bytes == 0U)
            || (file_byte_offset >= snapshot->reserved_file_bytes))
    {
        return 0U;
    }
    const uint64_t sector64 = file_byte_offset / RECORDER_FILE_RESERVATION_SECTOR_BYTES;
    if(sector64 > UINT32_MAX) return 0U;
    const uint32_t file_sector = (uint32_t)sector64;
    for(uint16_t i = 0U; i < snapshot->extent_count; ++i)
    {
        const sample_stream_physical_extent_t *const extent = &snapshot->extents[i];
        const uint64_t extent_end = (uint64_t)extent->file_sector_start
            + extent->sector_count;
        if((file_sector < extent->file_sector_start)
                || ((uint64_t)file_sector >= extent_end))
        {
            continue;
        }
        const uint32_t in_extent = file_sector - extent->file_sector_start;
        const uint16_t skip = (uint16_t)(file_byte_offset
            & (RECORDER_FILE_RESERVATION_SECTOR_BYTES - 1U));
        const uint64_t available = ((uint64_t)(extent->sector_count - in_extent)
            * RECORDER_FILE_RESERVATION_SECTOR_BYTES) - skip;
        uint32_t logical = (available < requested_bytes) ? (uint32_t)available : requested_bytes;
        const uint64_t reserved_left = snapshot->reserved_file_bytes - file_byte_offset;
        if(logical > reserved_left) logical = (uint32_t)reserved_left;
        out_span->lba = extent->lba_start + in_extent;
        out_span->logical_bytes = logical;
        out_span->first_sector_skip = skip;
        out_span->sector_count = (uint32_t)(((uint64_t)skip + logical
            + RECORDER_FILE_RESERVATION_SECTOR_BYTES - 1U)
            / RECORDER_FILE_RESERVATION_SECTOR_BYTES);
        out_span->extent_index = i;
        return (logical != 0U) ? 1U : 0U;
    }
    return 0U;
}
