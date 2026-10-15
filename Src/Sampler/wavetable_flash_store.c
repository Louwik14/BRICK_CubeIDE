#include "Sampler/wavetable_flash_store.h"

#include <stddef.h>
#include <string.h>

#include "Audio/audio.h"
#include "Platform/wavetable_flash_layout.h"
#include "Sampler/wavetable_flash_backend.h"
#include "Sampler/wavetable_prepared_format.h"

#define B6WF_MAGIC UINT32_C(0x46573642)
#define B6WF_COMMIT_MAGIC UINT32_C(0x43573642)
#define B6WF_DELETE_MAGIC UINT32_C(0x44573642)
#define B6WF_HEADER_BYTES UINT32_C(256)
#define B6WF_DIRECTORY_BYTES \
    (WAVETABLE_MIPMAP_MAX_BANDS * WAVETABLE_PREPARED_BAND_ENTRY_SIZE)
#define B6WF_PAYLOAD_OFFSET (B6WF_HEADER_BYTES + B6WF_DIRECTORY_BYTES)
#define B6WF_PATH_OFFSET UINT32_C(80)
#define B6WF_PATH_BYTES UINT32_C(160)
#define B6WF_COMMIT_BYTES UINT32_C(32)
#define B6WF_DELETE_BYTES UINT32_C(32)

typedef struct
{
    uint32_t offset;
    uint32_t record_bytes;
} wavetable_flash_record_t;

static wavetable_flash_record_t g_records[WAVETABLE_POOL_MAX_SLOTS];
static uint16_t g_record_count;
static uint32_t g_append_offset;

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U)
        | ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static uint64_t read_u64(const uint8_t *p)
{
    return (uint64_t)read_u32(p) | ((uint64_t)read_u32(p + 4U) << 32U);
}

static void write_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
}

static void write_u32(uint8_t *p, uint32_t value)
{
    for (uint8_t i = 0U; i < 4U; ++i) p[i] = (uint8_t)(value >> (8U * i));
}

static void write_u64(uint8_t *p, uint64_t value)
{
    write_u32(p, (uint32_t)value);
    write_u32(p + 4U, (uint32_t)(value >> 32U));
}

static uint32_t align32(uint32_t value)
{
    return (value + 31U) & ~UINT32_C(31);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data,
                             uint32_t bytes)
{
    crc = ~crc;
    for (uint32_t i = 0U; i < bytes; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U)
                ^ (UINT32_C(0xEDB88320) & (0U - (crc & 1U)));
    }
    return ~crc;
}

static uint32_t path_hash(const char *path)
{
    uint32_t hash = UINT32_C(2166136261);
    while ((path != NULL) && (*path != '\0'))
    {
        uint8_t value = (uint8_t)*path++;
        if ((value >= 'A') && (value <= 'Z')) value = (uint8_t)(value + 32U);
        if (value == '\\') value = '/';
        hash = (hash ^ value) * UINT32_C(16777619);
    }
    return hash;
}

static uint8_t delete_word_erased(const uint8_t *word)
{
    for (uint32_t i = 0U; i < B6WF_DELETE_BYTES; ++i)
        if (word[i] != 0xFFU) return 0U;
    return 1U;
}

static uint64_t stable_id(const char *path, uint32_t source_crc32)
{
    return ((uint64_t)source_crc32 << 32U) | path_hash(path);
}

static uint8_t flashword_write(uint32_t offset, const uint8_t *source,
                               uint32_t valid_bytes)
{
    uint8_t word[32];
    memset(word, 0xFF, sizeof(word));
    if (valid_bytes > sizeof(word)) return 0U;
    memcpy(word, source, valid_bytes);
    return wavetable_flash_backend_program(offset, word);
}

static uint8_t program_range(uint32_t offset, const uint8_t *source,
                             uint32_t bytes)
{
    while (bytes != 0U)
    {
        const uint32_t chunk = (bytes > 32U) ? 32U : bytes;
        if (flashword_write(offset, source, chunk) == 0U) return 0U;
        offset += 32U;
        source += chunk;
        bytes -= chunk;
    }
    return 1U;
}

static uint8_t decode_record(uint32_t offset, wavetable_slot_t *out,
                             uint32_t *out_record_bytes)
{
    const uint8_t *const h = wavetable_flash_backend_map(offset,
                                                         B6WF_HEADER_BYTES);
    if ((h == NULL) || (read_u32(h) != B6WF_MAGIC)
        || (read_u16(h + 4U) != WAVETABLE_FLASH_STORE_FORMAT_VERSION)
        || (read_u16(h + 6U) != B6WF_HEADER_BYTES)) return 0U;
    const uint32_t record_bytes = read_u32(h + 8U);
    const uint32_t frame_count = read_u32(h + 28U);
    const uint16_t band_count = read_u16(h + 32U);
    const uint32_t data_offset = read_u32(h + 36U);
    const uint32_t data_bytes = read_u32(h + 40U);
    const uint16_t path_length = read_u16(h + 64U);
    const uint32_t directory_offset = read_u32(h + 68U);
    if ((record_bytes < B6WF_PAYLOAD_OFFSET + 64U)
        || (record_bytes > WAVETABLE_FLASH_SIZE - offset)
        || ((record_bytes & 31U) != 0U)
        || (frame_count == 0U)
        || (frame_count > WAVETABLE_FLASH_MAX_FRAMES)
        || (band_count == 0U) || (band_count > WAVETABLE_MIPMAP_MAX_BANDS)
        || (directory_offset != B6WF_HEADER_BYTES)
        || (data_offset != B6WF_PAYLOAD_OFFSET)
        || (data_bytes > record_bytes - data_offset - 64U)
        || (path_length == 0U) || (path_length >= B6WF_PATH_BYTES)) return 0U;
    const uint32_t commit_offset = align32(data_offset + data_bytes);
    if (record_bytes != commit_offset + B6WF_COMMIT_BYTES + B6WF_DELETE_BYTES)
        return 0U;
    const uint8_t *const record = wavetable_flash_backend_map(offset,
                                                              record_bytes);
    if (record == NULL) return 0U;
    const uint8_t *const commit = &record[commit_offset];
    const uint8_t *const deleted = &record[commit_offset + B6WF_COMMIT_BYTES];
    if ((read_u32(commit) != B6WF_COMMIT_MAGIC)
        || (read_u64(commit + 8U) != read_u64(h + 12U))
        || (read_u32(commit + 4U)
            != crc32_update(0U, record, commit_offset))) return 0U;
    if (out_record_bytes != NULL) *out_record_bytes = record_bytes;
    if (read_u32(deleted) == B6WF_DELETE_MAGIC) return 2U;
    if (delete_word_erased(deleted) == 0U) return 0U;
    if ((read_u32(h + 56U)
            != crc32_update(0U, &record[data_offset], data_bytes))
        || (read_u32(h + 60U)
            != crc32_update(0U, &record[data_offset],
                            frame_count * WAVETABLE_FRAME_SAMPLE_COUNT
                                * sizeof(float)))) return 0U;
    if (out == NULL) return 1U;
    memset(out, 0, sizeof(*out));
    out->state = WAVETABLE_SLOT_READY;
    out->global_slot = WAVETABLE_POOL_INVALID_SLOT;
    out->format = WAVETABLE_FORMAT_FLOAT32_MONO;
    out->frame_sample_count = WAVETABLE_FRAME_SAMPLE_COUNT;
    out->frame_count = frame_count;
    out->data_bytes = frame_count * WAVETABLE_FRAME_SAMPLE_COUNT
        * sizeof(float);
    out->cost_bytes_aligned = record_bytes;
    out->stable_id = read_u64(h + 12U);
    out->flash_offset = offset;
    out->persistent = 1U;
    memcpy(out->path, h + B6WF_PATH_OFFSET, path_length);
    out->path[path_length] = '\0';
    out->first_page_slot = UINT16_MAX;
    out->mipmap.first_page_slot = UINT16_MAX;
    out->mipmap.band_count = band_count;
    out->mipmap.cycle_count = frame_count;
    out->mipmap.cycle_transition_magnitude = h[44U];
    out->mipmap.wave_index_multiplier = (int32_t)read_u32(h + 48U);
    out->mipmap.data = (float *)&record[data_offset];
    out->mipmap.data_bytes = data_bytes;
    out->mipmap.cost_bytes_aligned = data_bytes;
    uint32_t payload_offset = 0U;
    for (uint16_t i = 0U; i < band_count; ++i)
    {
        const uint8_t *const entry = &record[directory_offset
            + (uint32_t)i * WAVETABLE_PREPARED_BAND_ENTRY_SIZE];
        wavetable_mipmap_band_t *const band = &out->mipmap.bands[i];
        band->max_phase_increment = read_u32(entry);
        band->cycle_sample_count = read_u32(entry + 12U);
        band->cycle_magnitude = read_u16(entry + 16U);
        band->flags = read_u16(entry + 18U);
        const uint32_t encoded_offset = read_u32(entry + 20U);
        band->sample_count = read_u32(entry + 24U);
        const uint32_t band_bytes = band->sample_count * sizeof(float);
        if ((encoded_offset != payload_offset)
            || (band_bytes > data_bytes - payload_offset)) return 0U;
        band->data = (float *)&record[data_offset + payload_offset];
        payload_offset += band_bytes;
    }
    if ((payload_offset != data_bytes)
        || (out->mipmap.bands[0].cycle_sample_count
            != WAVETABLE_FRAME_SAMPLE_COUNT)
        || (out->mipmap.bands[0].sample_count
            < frame_count * WAVETABLE_FRAME_SAMPLE_COUNT)) return 0U;
    out->data = out->mipmap.bands[0].data;
    return 1U;
}

void wavetable_flash_store_init(void)
{
    memset(g_records, 0, sizeof(g_records));
    g_record_count = 0U;
    g_append_offset = 0U;
    while (g_append_offset + B6WF_HEADER_BYTES <= WAVETABLE_FLASH_SIZE)
    {
        if (wavetable_flash_backend_is_erased(g_append_offset, 32U) != 0U)
            break;
        uint32_t record_bytes = 0U;
        wavetable_slot_t slot;
        const uint8_t status = decode_record(g_append_offset, &slot,
                                             &record_bytes);
        if ((status == 0U) || (record_bytes == 0U))
        {
            g_append_offset = WAVETABLE_FLASH_SIZE;
            break;
        }
        if ((status == 1U) && (g_record_count < WAVETABLE_POOL_MAX_SLOTS))
        {
            g_records[g_record_count].offset = g_append_offset;
            g_records[g_record_count].record_bytes = record_bytes;
            ++g_record_count;
        }
        g_append_offset += record_bytes;
    }
}

uint8_t wavetable_flash_store_scan_slot(uint16_t index,
                                        wavetable_slot_t *out_slot)
{
    if ((index >= g_record_count) || (out_slot == NULL)) return 0U;
    return (decode_record(g_records[index].offset, out_slot, NULL) == 1U)
        ? 1U : 0U;
}

uint8_t wavetable_flash_store_append(const wavetable_slot_t *prepared,
                                     uint32_t source_crc32,
                                     uint32_t base_crc32,
                                     uint32_t payload_crc32,
                                     wavetable_slot_t *out_persistent)
{
    if ((prepared == NULL) || (out_persistent == NULL)
        || (prepared->frame_count == 0U)
        || (prepared->frame_count > WAVETABLE_FLASH_MAX_FRAMES)
        || (prepared->mipmap.band_count == 0U)
        || (prepared->mipmap.band_count > WAVETABLE_MIPMAP_MAX_BANDS)
        || (prepared->mipmap.data == NULL)) return 0U;
    const size_t path_length = strnlen(prepared->path, B6WF_PATH_BYTES);
    if ((path_length == 0U) || (path_length >= B6WF_PATH_BYTES)) return 0U;
    const uint32_t commit_offset = align32(B6WF_PAYLOAD_OFFSET
                                           + prepared->mipmap.data_bytes);
    const uint32_t record_bytes = commit_offset + 64U;
    if ((g_record_count >= WAVETABLE_POOL_MAX_SLOTS)
        || (g_append_offset > WAVETABLE_FLASH_SIZE - record_bytes)
        || (wavetable_flash_backend_is_erased(g_append_offset,
                                               record_bytes) == 0U)) return 0U;

    uint8_t header[B6WF_HEADER_BYTES];
    memset(header, 0xFF, sizeof(header));
    write_u32(header, B6WF_MAGIC);
    write_u16(header + 4U, WAVETABLE_FLASH_STORE_FORMAT_VERSION);
    write_u16(header + 6U, B6WF_HEADER_BYTES);
    write_u32(header + 8U, record_bytes);
    const uint64_t id = stable_id(prepared->path, source_crc32);
    write_u64(header + 12U, id);
    write_u32(header + 20U, source_crc32);
    write_u32(header + 24U, path_hash(prepared->path));
    write_u32(header + 28U, prepared->frame_count);
    write_u16(header + 32U, prepared->mipmap.band_count);
    write_u32(header + 36U, B6WF_PAYLOAD_OFFSET);
    write_u32(header + 40U, prepared->mipmap.data_bytes);
    header[44U] = prepared->mipmap.cycle_transition_magnitude;
    write_u32(header + 48U,
              (uint32_t)prepared->mipmap.wave_index_multiplier);
    write_u32(header + 52U, WAVETABLE_MIPMAP_PREP_REVISION);
    write_u32(header + 56U, payload_crc32);
    write_u32(header + 60U, base_crc32);
    write_u16(header + 64U, (uint16_t)path_length);
    write_u32(header + 68U, B6WF_HEADER_BYTES);
    memcpy(header + B6WF_PATH_OFFSET, prepared->path, path_length);

    uint8_t directory[B6WF_DIRECTORY_BYTES];
    memset(directory, 0xFF, sizeof(directory));
    uint32_t payload_offset = 0U;
    for (uint16_t i = 0U; i < prepared->mipmap.band_count; ++i)
    {
        const wavetable_mipmap_band_t *const band = &prepared->mipmap.bands[i];
        uint8_t *const entry = &directory[
            (uint32_t)i * WAVETABLE_PREPARED_BAND_ENTRY_SIZE];
        write_u32(entry, band->max_phase_increment);
        write_u32(entry + 12U, band->cycle_sample_count);
        write_u16(entry + 16U, band->cycle_magnitude);
        write_u16(entry + 18U, band->flags);
        write_u32(entry + 20U, payload_offset);
        write_u32(entry + 24U, band->sample_count);
        write_u32(entry + 28U, band->cycle_sample_count == 8U
            ? 16U : band->cycle_sample_count);
        payload_offset += band->sample_count * sizeof(float);
    }
    if (payload_offset != prepared->mipmap.data_bytes) return 0U;

    audio_stop();
    uint8_t ok = program_range(g_append_offset, header, sizeof(header));
    if (ok != 0U) ok = program_range(g_append_offset + B6WF_HEADER_BYTES,
                                      directory, sizeof(directory));
    if (ok != 0U) ok = program_range(g_append_offset + B6WF_PAYLOAD_OFFSET,
                                      (const uint8_t *)prepared->mipmap.data,
                                      prepared->mipmap.data_bytes);
    if (ok != 0U)
    {
        const uint8_t *const record = wavetable_flash_backend_map(
            g_append_offset, commit_offset);
        uint8_t commit[32];
        memset(commit, 0xFF, sizeof(commit));
        write_u32(commit, B6WF_COMMIT_MAGIC);
        write_u32(commit + 4U, crc32_update(0U, record, commit_offset));
        write_u64(commit + 8U, id);
        ok = wavetable_flash_backend_program(g_append_offset + commit_offset,
                                             commit);
    }
    const uint8_t restarted = audio_start();
    if ((ok == 0U) || (restarted == 0U)) return 0U;
    wavetable_flash_backend_publish_barrier();
    const uint32_t stored_offset = g_append_offset;
    g_records[g_record_count++] = (wavetable_flash_record_t){
        .offset = stored_offset, .record_bytes = record_bytes };
    g_append_offset += record_bytes;
    return (decode_record(stored_offset, out_persistent, NULL) == 1U)
        ? 1U : 0U;
}

uint8_t wavetable_flash_store_delete(uint32_t flash_offset)
{
    uint32_t record_bytes = 0U;
    if (decode_record(flash_offset, NULL, &record_bytes) != 1U) return 0U;
    uint8_t marker[32];
    memset(marker, 0xFF, sizeof(marker));
    write_u32(marker, B6WF_DELETE_MAGIC);
    audio_stop();
    const uint8_t ok = wavetable_flash_backend_program(
        flash_offset + record_bytes - B6WF_DELETE_BYTES, marker);
    const uint8_t restarted = audio_start();
    wavetable_flash_backend_publish_barrier();
    if ((ok == 0U) || (restarted == 0U)) return 0U;
    wavetable_flash_store_init();
    return 1U;
}

uint32_t wavetable_flash_store_used_bytes(void)
{
    return g_append_offset;
}

uint32_t wavetable_flash_store_free_bytes(void)
{
    return (g_append_offset <= WAVETABLE_FLASH_SIZE)
        ? WAVETABLE_FLASH_SIZE - g_append_offset : 0U;
}
