#include "Storage/generic_recorder_adapters.h"

#include "SD/sd_block_device.h"

static uint8_t generic_recorder_fatfs_snapshot(
    void *context,
    recorder_file_reservation_map_snapshot_t *snapshot)
{
    return recorder_file_reservation_map_snapshot(context, snapshot);
}

static uint8_t generic_recorder_fatfs_resolve(
    void *context,
    const recorder_file_reservation_map_snapshot_t *snapshot,
    uint64_t file_byte_offset,
    uint32_t requested_bytes,
    sample_stream_physical_span_t *span)
{
    (void)context;
    return recorder_file_reservation_map_resolve(
        snapshot, file_byte_offset, requested_bytes, span);
}

static recorder_file_reservation_result_t generic_recorder_fatfs_extend(
    void *context,
    uint64_t additional_bytes)
{
    return recorder_file_reservation_extend(context, additional_bytes);
}

generic_recorder_reservation_t generic_recorder_fatfs_reservation_adapter(
    recorder_file_reservation_t *session)
{
    const generic_recorder_reservation_t adapter = {
        .context = session,
        .snapshot = generic_recorder_fatfs_snapshot,
        .resolve = generic_recorder_fatfs_resolve,
        .extend = generic_recorder_fatfs_extend,
    };
    return adapter;
}

static generic_recorder_transport_start_t generic_recorder_sd_start(
    void *context,
    uint32_t lba,
    uint32_t sector_count,
    const void *buffer,
    uint32_t owner_generation,
    uint32_t media_epoch)
{
    (void)context;
    (void)media_epoch;
    const sd_block_device_result_t result = sd_block_device_async_write_submit(
        lba, sector_count, buffer, owner_generation);
    if (result == SD_BLOCK_DEVICE_OK)
    {
        return GENERIC_RECORDER_TRANSPORT_STARTED;
    }
    if ((result == SD_BLOCK_DEVICE_BUSY)
        || (result == SD_BLOCK_DEVICE_QUEUE_FULL))
    {
        return GENERIC_RECORDER_TRANSPORT_BUSY;
    }
    return GENERIC_RECORDER_TRANSPORT_ERROR;
}

static generic_recorder_transport_poll_t generic_recorder_sd_poll(
    void *context,
    generic_recorder_transport_completion_t *completion)
{
    (void)context;
    sd_block_device_async_poll();
    sd_block_device_async_completion_t block_completion;
    if (sd_block_device_async_take_completion(&block_completion) == 0U)
    {
        return (sd_block_device_async_hardware_state() == SD_BLOCK_DEVICE_HW_ABORTING)
                   ? GENERIC_RECORDER_TRANSPORT_RECOVERY_ABORT
                   : GENERIC_RECORDER_TRANSPORT_ACTIVE;
    }
    completion->lba = block_completion.lba;
    completion->sector_count = block_completion.sector_count;
    completion->buffer = block_completion.src;
    completion->owner_generation = block_completion.owner_generation;
    completion->media_epoch = block_completion.media_epoch;
    if (block_completion.result == SD_BLOCK_DEVICE_OK)
    {
        return GENERIC_RECORDER_TRANSPORT_COMPLETED;
    }
    if ((block_completion.result == SD_BLOCK_DEVICE_MEDIA_CHANGED)
        || (block_completion.result == SD_BLOCK_DEVICE_CARD_REMOVED))
    {
        return GENERIC_RECORDER_TRANSPORT_MEDIA_CHANGED;
    }
    return GENERIC_RECORDER_TRANSPORT_FAILED;
}

generic_recorder_transport_t generic_recorder_sd_block_device_adapter(void)
{
    const generic_recorder_transport_t adapter = {
        .context = 0,
        .start = generic_recorder_sd_start,
        .poll = generic_recorder_sd_poll,
    };
    return adapter;
}
