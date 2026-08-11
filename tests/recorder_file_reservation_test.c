#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Storage/recorder_file_reservation.h"
#include "Storage/audio_recorder_wav.h"
#include "Storage/wav_parser.h"
#include "Storage/sd_access_gate.h"
#include "SD/bsp_driver_sd.h"
#include "diskio.h"

#define TEST_SECTOR_SIZE 512U
#define TEST_SECTOR_COUNT 196608U
#define TEST_HEADER_BYTES 512U
#define TEST_INITIAL_BYTES (256U * 1024U)
#define TEST_NEIGHBOR_BYTES (64U * 1024U)

static BYTE *g_disk;
static uint32_t g_tick;
static uint32_t g_disk_read_sectors;
static uint32_t g_disk_write_sectors;
static uint8_t g_gate_owned;
static uint32_t g_media_epoch = 1U;

#define CHECK(expr) do { \
    if(!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 0; \
    } \
} while(0)

DSTATUS disk_initialize(BYTE pdrv)
{
    return (pdrv == 0U) ? 0U : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv)
{
    return (pdrv == 0U) ? 0U : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    if((pdrv != 0U) || (buff == NULL) || (count == 0U)
            || ((uint64_t)sector + count > TEST_SECTOR_COUNT))
    {
        return RES_PARERR;
    }
    memcpy(buff, &g_disk[(size_t)sector * TEST_SECTOR_SIZE],
           (size_t)count * TEST_SECTOR_SIZE);
    g_disk_read_sectors += count;
    g_tick += count;
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    if((pdrv != 0U) || (buff == NULL) || (count == 0U)
            || ((uint64_t)sector + count > TEST_SECTOR_COUNT))
    {
        return RES_PARERR;
    }
    memcpy(&g_disk[(size_t)sector * TEST_SECTOR_SIZE], buff,
           (size_t)count * TEST_SECTOR_SIZE);
    g_disk_write_sectors += count;
    g_tick += count;
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if(pdrv != 0U) return RES_PARERR;
    switch(cmd)
    {
        case CTRL_SYNC:
        case CTRL_TRIM:
            return RES_OK;
        case GET_SECTOR_COUNT:
            *(DWORD *)buff = TEST_SECTOR_COUNT;
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD *)buff = TEST_SECTOR_SIZE;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD *)buff = 1U;
            return RES_OK;
        default:
            return RES_PARERR;
    }
}

DWORD get_fattime(void)
{
    return ((DWORD)(2026U - 1980U) << 25) | (8U << 21) | (11U << 16);
}

uint32_t HAL_GetTick(void)
{
    return g_tick;
}

uint32_t sd_block_device_async_pending_count(void)
{
    return 0U;
}

uint8_t BSP_SD_GetCardState(void)
{
    return SD_TRANSFER_OK;
}

uint8_t sd_access_gate_try_acquire(sd_access_client_t client)
{
    if((client != SD_ACCESS_CLIENT_SCHEDULED_RECORDER) || g_gate_owned) return 0U;
    g_gate_owned = 1U;
    return 1U;
}

void sd_access_gate_release(sd_access_client_t client)
{
    if(client == SD_ACCESS_CLIENT_SCHEDULED_RECORDER) g_gate_owned = 0U;
}

uint32_t sd_access_media_epoch(void)
{
    return g_media_epoch;
}

static int format_volume(BYTE format, DWORD allocation_unit, FATFS *fs)
{
    BYTE work[4096];
    CHECK(f_mount(NULL, "0:", 0U) == FR_OK);
    memset(g_disk, 0, (size_t)TEST_SECTOR_COUNT * TEST_SECTOR_SIZE);
    CHECK(f_mkfs("0:", (BYTE)(format | FM_SFD), allocation_unit,
                 work, sizeof(work)) == FR_OK);
    memset(fs, 0, sizeof(*fs));
    CHECK(f_mount(fs, "0:", 1U) == FR_OK);
    return 1;
}

static int create_neighbor(void)
{
    FIL file;
    BYTE block[4096];
    memset(block, 0xA5, sizeof(block));
    CHECK(f_open(&file, "0:/neighbor.bin", FA_CREATE_NEW | FA_WRITE) == FR_OK);
    for(UINT offset = 0U; offset < TEST_NEIGHBOR_BYTES; offset += sizeof(block))
    {
        UINT written = 0U;
        CHECK(f_write(&file, block, sizeof(block), &written) == FR_OK);
        CHECK(written == sizeof(block));
    }
    CHECK(f_close(&file) == FR_OK);
    return 1;
}

static int verify_neighbor(void)
{
    FIL file;
    BYTE block[4096];
    CHECK(f_open(&file, "0:/neighbor.bin", FA_READ) == FR_OK);
    for(UINT offset = 0U; offset < TEST_NEIGHBOR_BYTES; offset += sizeof(block))
    {
        UINT read = 0U;
        CHECK(f_read(&file, block, sizeof(block), &read) == FR_OK);
        CHECK(read == sizeof(block));
        for(UINT i = 0U; i < read; ++i) CHECK(block[i] == 0xA5U);
    }
    CHECK(f_close(&file) == FR_OK);
    return 1;
}

static int run_contract(BYTE format, DWORD allocation_unit)
{
    FATFS fs;
    recorder_file_reservation_t session;
    recorder_file_reservation_t recovered;
    recorder_file_reservation_map_snapshot_t snapshot;
    sample_stream_physical_span_t span;
    FILINFO info;
    FATFS *free_fs;
    DWORD free_before;
    DWORD free_after;

    CHECK(format_volume(format, allocation_unit, &fs));
    recorder_file_reservation_init(&session);

    /* A: create and reserve; the physical map immediately covers the file. */
    CHECK(recorder_file_reservation_create(&session, "0:/take.rec",
                                           TEST_HEADER_BYTES,
                                           TEST_INITIAL_BYTES)
          == RECORDER_FILE_RESERVATION_OK);
    CHECK(session.open != 0U);
    CHECK(session.reserved_bytes >= TEST_INITIAL_BYTES);
    CHECK(recorder_file_reservation_map_snapshot(&session, &snapshot));
    CHECK(snapshot.extent_count == 1U);
    CHECK(snapshot.media_epoch == g_media_epoch);
    CHECK(recorder_file_reservation_map_resolve(&snapshot,
                                                TEST_HEADER_BYTES + 37U,
                                                8192U, &span));
    CHECK(span.logical_bytes != 0U);
    const sample_stream_physical_extent_t immutable_first = snapshot.extents[0];

    /* H/I: force a neighbour between two recorder allocations. */
    CHECK(create_neighbor());

    /* B: repeated extensions preserve all already-published extents. */
    for(unsigned i = 0U; i < 10U; ++i)
    {
        CHECK(recorder_file_reservation_extend(&session, 32U * 1024U)
              == RECORDER_FILE_RESERVATION_OK);
    }
    CHECK(session.extent_count > 1U);
    CHECK(memcmp(&immutable_first, &session.physical_extents[0],
                 sizeof(immutable_first)) == 0);
    CHECK(verify_neighbor());

    /* C: valid length is committed independently from the reservation. */
    CHECK(recorder_file_reservation_commit_valid(&session, 96U * 1024U)
          == RECORDER_FILE_RESERVATION_OK);
    CHECK(session.valid_bytes == 96U * 1024U);
    /* E/F/G: close/reopen recovery rebuilds FAT32 and exFAT maps. */
    CHECK(recorder_file_reservation_close(&session)
          == RECORDER_FILE_RESERVATION_OK);
    CHECK(f_stat("0:/take.rec", &info) == FR_OK);
    if(format == FM_FAT32)
    {
        CHECK(info.fsize == (FSIZE_t)(TEST_HEADER_BYTES + 96U * 1024U));
    }
    else
    {
        CHECK(info.fsize > (FSIZE_t)(TEST_HEADER_BYTES + 96U * 1024U));
    }

    recorder_file_reservation_init(&recovered);
    CHECK(recorder_file_reservation_recover(&recovered, "0:/take.rec",
                                            TEST_HEADER_BYTES)
          == RECORDER_FILE_RESERVATION_OK);
    CHECK(recovered.valid_bytes == 96U * 1024U);
    CHECK(recovered.extent_count > 1U);
    CHECK(verify_neighbor());

    /* J: long progressive run, then release only two clusters of slack. */
    for(unsigned i = 0U; i < 80U; ++i)
    {
        CHECK(recorder_file_reservation_extend(&recovered, 16U * 1024U)
              == RECORDER_FILE_RESERVATION_OK);
    }
    CHECK(recovered.extent_count < RECORDER_FILE_RESERVATION_MAX_EXTENTS);
    const uint32_t cluster_bytes = recovered.fs_state.cluster_bytes;
    CHECK(recovered.reserved_bytes > (uint64_t)cluster_bytes * 2U);
    const uint64_t final_valid = ((recovered.reserved_bytes
        - (uint64_t)cluster_bytes * 2U) / 6U) * 6U;
    CHECK(recorder_file_reservation_commit_valid(&recovered, final_valid)
          == RECORDER_FILE_RESERVATION_OK);
    CHECK(f_getfree("0:", &free_before, &free_fs) == FR_OK);
    const uint32_t released_before = recovered.metrics.clusters_released;
    g_disk_read_sectors = 0U;
    g_disk_write_sectors = 0U;

    /* D: metadata-only finalization frees exactly the unused tail. */
    CHECK(recorder_file_reservation_release_unused(&recovered)
          == RECORDER_FILE_RESERVATION_OK);
    const uint32_t release_reads = g_disk_read_sectors;
    const uint32_t release_writes = g_disk_write_sectors;
    CHECK(recovered.metrics.clusters_released - released_before == 2U);
    CHECK(release_reads < 32U);
    CHECK(release_writes < 32U);
    CHECK(f_getfree("0:", &free_after, &free_fs) == FR_OK);
    CHECK(free_after == free_before + 2U);
    CHECK(recovered.reserved_bytes >= recovered.valid_bytes);
    CHECK((recovered.reserved_bytes - recovered.valid_bytes) < cluster_bytes);
    CHECK(verify_neighbor());
    CHECK(final_valid <= UINT32_MAX);
    uint8_t wav_header[AUDIO_RECORDER_WAV_HEADER_BYTES];
    UINT header_written = 0U;
    wav_info_t wav_info;
    CHECK(audio_recorder_wav_build_header(
        wav_header, (uint32_t)final_valid, 48000U, 2U));
    CHECK(f_lseek(&recovered.file, 0U) == FR_OK);
    CHECK(f_write(&recovered.file, wav_header, sizeof(wav_header),
                  &header_written) == FR_OK);
    CHECK(header_written == sizeof(wav_header));
    CHECK(wav_parser_parse_info(&recovered.file, &wav_info));
    CHECK(wav_info.audio_format == 1U);
    CHECK(wav_info.sample_rate == 48000U);
    CHECK(wav_info.channels == 2U);
    CHECK(wav_info.bits_per_sample == 24U);
    CHECK(wav_info.block_align == 6U);
    CHECK(wav_info.data_offset == TEST_HEADER_BYTES);
    CHECK(wav_info.data_size == (uint32_t)final_valid);
    CHECK(recorder_file_reservation_close(&recovered)
          == RECORDER_FILE_RESERVATION_OK);
    CHECK(f_stat("0:/take.rec", &info) == FR_OK);
    if(format == FM_FAT32)
    {
        CHECK(info.fsize == (FSIZE_t)(TEST_HEADER_BYTES + final_valid));
    }
    else CHECK(info.fsize == (FSIZE_t)(TEST_HEADER_BYTES + final_valid));
    CHECK(recorder_file_reservation_rename_closed(&recovered, "0:/take.done")
          == RECORDER_FILE_RESERVATION_OK);
    CHECK(f_stat("0:/take.done", &info) == FR_OK);

    /* Publishing the exact size must also happen when no full cluster is free. */
    CHECK(f_unlink("0:/take.done") == FR_OK);
    recorder_file_reservation_init(&session);
    CHECK(recorder_file_reservation_create(&session, "0:/tight.rec",
                                           TEST_HEADER_BYTES,
                                           cluster_bytes)
          == RECORDER_FILE_RESERVATION_OK);
    CHECK(session.reserved_bytes > 1U);
    const uint64_t tight_valid = session.reserved_bytes - 1U;
    CHECK(recorder_file_reservation_commit_valid(&session, tight_valid)
          == RECORDER_FILE_RESERVATION_OK);
    CHECK(recorder_file_reservation_release_unused(&session)
          == RECORDER_FILE_RESERVATION_OK);
    CHECK(recorder_file_reservation_close(&session)
          == RECORDER_FILE_RESERVATION_OK);
    CHECK(f_stat("0:/tight.rec", &info) == FR_OK);
    CHECK(info.fsize == (FSIZE_t)(TEST_HEADER_BYTES + tight_valid));
    printf("%s create_max=%lu extend_max=%lu commit_max=%lu release_max=%lu "
           "metadata_read=%lu metadata_write=%lu extents_added=%lu\n",
           (format == FM_FAT32) ? "FAT32" : "exFAT",
           (unsigned long)session.metrics.max_create_ms,
           (unsigned long)((session.metrics.max_extend_ms > recovered.metrics.max_extend_ms)
               ? session.metrics.max_extend_ms : recovered.metrics.max_extend_ms),
           (unsigned long)((session.metrics.max_commit_ms > recovered.metrics.max_commit_ms)
               ? session.metrics.max_commit_ms : recovered.metrics.max_commit_ms),
           (unsigned long)recovered.metrics.max_release_ms,
           (unsigned long)(session.metrics.metadata_sectors_read
               + recovered.metrics.metadata_sectors_read),
           (unsigned long)(session.metrics.metadata_sectors_written
               + recovered.metrics.metadata_sectors_written),
           (unsigned long)(session.metrics.extents_added
               + recovered.metrics.extents_added));
    return 1;
}

int main(void)
{
    g_disk = (BYTE *)calloc(TEST_SECTOR_COUNT, TEST_SECTOR_SIZE);
    if(g_disk == NULL)
    {
        fprintf(stderr, "Unable to allocate test disk\n");
        return 1;
    }
    if(!run_contract(FM_FAT32, TEST_SECTOR_SIZE)) return 1;
    if(!run_contract(FM_EXFAT, 4096U)) return 1;
    free(g_disk);
    printf("PASS recorder reservation A-J: FAT32 + exFAT\n");
    return 0;
}
