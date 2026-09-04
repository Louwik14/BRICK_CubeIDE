#ifndef PERSISTENT_CONTROL_CODEC_H
#define PERSISTENT_CONTROL_CODEC_H

#include <stdint.h>

#include "Storage/persistent_control_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PERSIST_CODEC_VERSION 5U
#define PERSIST_CODEC_HEADER_BYTES 24U
#define PERSIST_CODEC_SECTION_HEADER_BYTES 8U
#define PERSIST_CODEC_MAX_DOCUMENT_BYTES 0x3FFFFFFFUL

typedef enum
{
    PERSIST_CODEC_DOCUMENT_PROJECT = 1,
    PERSIST_CODEC_DOCUMENT_PATTERN,
    PERSIST_CODEC_DOCUMENT_PATCH
} persist_codec_document_kind_t;

typedef enum
{
    PERSIST_CODEC_OK = 0,
    PERSIST_CODEC_INVALID_ARGUMENT,
    PERSIST_CODEC_IO_ERROR,
    PERSIST_CODEC_BAD_MAGIC,
    PERSIST_CODEC_BAD_VERSION,
    PERSIST_CODEC_BAD_DOCUMENT_KIND,
    PERSIST_CODEC_BAD_LENGTH,
    PERSIST_CODEC_BAD_CRC,
    PERSIST_CODEC_BAD_SECTION,
    PERSIST_CODEC_DUPLICATE,
    PERSIST_CODEC_CAPACITY_EXCEEDED,
    PERSIST_CODEC_UNKNOWN_KEY,
    PERSIST_CODEC_INVALID_ENTITY,
    PERSIST_CODEC_INVALID_PLAY,
    PERSIST_CODEC_INVALID_PLOCK,
    PERSIST_CODEC_INVALID_MODULATION,
    PERSIST_CODEC_INVALID_ASSET
} persist_codec_result_t;

typedef uint8_t (*persist_codec_write_fn)(void *context,
                                          const uint8_t *data,
                                          uint32_t length);
typedef uint8_t (*persist_codec_read_fn)(void *context,
                                         uint8_t *data,
                                         uint32_t length);
typedef uint8_t (*persist_codec_reset_fn)(void *context);
typedef uint8_t (*persist_codec_size_fn)(void *context,uint32_t *out_size);

typedef struct
{
    persist_codec_write_fn write;
    void *context;
} persist_codec_sink_t;

typedef struct
{
    persist_codec_read_fn read;
    persist_codec_reset_fn reset;
    persist_codec_size_fn size;
    void *context;
} persist_codec_source_t;

/* The provider retains ownership; the pointed record must remain stable for
 * the duration of one encode call (count, CRC and write passes). */
typedef const persist_control_pattern_record_t *(*persist_codec_pattern_get_fn)(void *context,
                                                                                 uint16_t ordinal);
typedef uint8_t (*persist_codec_pattern_put_fn)(void *context,
                                                const persist_control_pattern_record_t *record);
typedef uint8_t (*persist_codec_pattern_begin_fn)(void *context);
typedef uint8_t (*persist_codec_pattern_finish_fn)(void *context);
typedef void (*persist_codec_pattern_abort_fn)(void *context);

typedef struct
{
    persist_codec_pattern_get_fn get;
    void *context;
} persist_codec_pattern_provider_t;

typedef struct
{
    /* The full document is prevalidated before this consumer is entered.
     * put handles one locally validated record immediately; commit is the
     * end-of-stream notification and abort does not imply global rollback. */
    persist_codec_pattern_begin_fn begin;
    persist_codec_pattern_put_fn put;
    persist_codec_pattern_finish_fn commit;
    persist_codec_pattern_abort_fn abort;
    void *context;
} persist_codec_pattern_consumer_t;

typedef struct
{
    uint8_t active_pattern_bank;
    uint8_t active_pattern;
    uint16_t pattern_count;
    uint16_t asset_count;
} persist_codec_project_metadata_t;

typedef const persist_control_asset_ref_t *(*persist_codec_asset_get_fn)(void *context,uint16_t ordinal);
typedef const persist_control_pattern_t *(*persist_codec_working_pattern_get_fn)(void *context);
typedef struct { uint16_t count; persist_codec_asset_get_fn get; void *context; } persist_codec_asset_provider_t;
typedef struct { persist_codec_working_pattern_get_fn get; void *context; } persist_codec_working_pattern_provider_t;
typedef struct
{
    persist_codec_project_metadata_t metadata;
    persist_codec_working_pattern_provider_t working_pattern;
    persist_codec_asset_provider_t assets;
    const persist_control_macros_t *macros;
    persist_codec_pattern_provider_t patterns;
} persist_codec_project_source_t;

typedef uint8_t (*persist_codec_project_begin_assets_fn)(void *context);
typedef uint8_t (*persist_codec_project_put_asset_fn)(void *context,const persist_control_asset_ref_t *asset);
typedef uint8_t (*persist_codec_project_validate_asset_fn)(void *context,const persist_control_asset_ref_t *asset);
typedef uint8_t (*persist_codec_project_apply_working_fn)(void *context,const persist_codec_project_metadata_t *metadata,const persist_control_pattern_t *pattern);
typedef uint8_t (*persist_codec_project_apply_macros_fn)(void *context,const persist_control_macros_t *macros);
typedef struct
{
    persist_codec_project_begin_assets_fn begin_assets;
    persist_codec_project_validate_asset_fn validate_asset;
    persist_codec_project_put_asset_fn put_asset;
    persist_codec_project_apply_working_fn apply_working;
    persist_codec_project_apply_macros_fn apply_macros;
    void *context;
} persist_codec_project_consumer_t;

typedef struct
{
    union { persist_control_pattern_record_t pattern_record; persist_control_macros_t macros; } unit;
    persist_control_asset_ref_t assets[PERSIST_CONTROL_ASSET_COUNT];
} persist_codec_project_workspace_t;

typedef struct
{
    persist_control_pattern_t pattern;
} persist_codec_pattern_staging_t;

typedef struct
{
    persist_control_patch_t patch;
} persist_codec_patch_staging_t;

persist_codec_result_t persist_codec_validate_pattern(const persist_control_pattern_t *pattern);
persist_codec_result_t persist_codec_validate_patch(const persist_control_patch_t *patch);

persist_codec_result_t persist_codec_encode_pattern(const persist_control_pattern_t *pattern,
                                                     const persist_codec_sink_t *sink,
                                                     uint32_t *out_bytes);
persist_codec_result_t persist_codec_decode_pattern(const persist_codec_source_t *source,
                                                     persist_codec_pattern_staging_t *staging);
persist_codec_result_t persist_codec_encode_patch(const persist_control_patch_t *patch,
                                                   const persist_codec_sink_t *sink,
                                                   uint32_t *out_bytes);
persist_codec_result_t persist_codec_decode_patch(const persist_codec_source_t *source,
                                                   persist_codec_patch_staging_t *staging);
persist_codec_result_t persist_codec_encode_project(const persist_codec_project_source_t *project,
                                                     const persist_codec_sink_t *sink,
                                                     uint32_t *out_bytes);

typedef enum
{
    PERSIST_CODEC_PROJECT_SECTION_CORE = 0,
    PERSIST_CODEC_PROJECT_SECTION_ASSETS,
    PERSIST_CODEC_PROJECT_SECTION_MACROS,
    PERSIST_CODEC_PROJECT_SECTION_BANK,
    PERSIST_CODEC_PROJECT_SECTION_COUNT
} persist_codec_project_section_t;

persist_codec_result_t persist_codec_encode_project_core_payload(
    const persist_codec_project_metadata_t *metadata,
    const persist_control_pattern_t *working_pattern,
    const persist_codec_sink_t *sink,
    uint32_t *out_bytes);
persist_codec_result_t persist_codec_encode_project_assets_payload(
    const persist_control_asset_ref_t *assets,
    uint16_t asset_count,
    const persist_codec_sink_t *sink,
    uint32_t *out_bytes);
persist_codec_result_t persist_codec_encode_project_macros_payload(
    const persist_control_macros_t *macros,
    const persist_codec_sink_t *sink,
    uint32_t *out_bytes);
persist_codec_result_t persist_codec_encode_project_pattern_record_payload(
    const persist_control_pattern_record_t *record,
    const persist_codec_sink_t *sink,
    uint32_t *out_bytes);
uint8_t persist_codec_build_project_section_header(
    persist_codec_project_section_t section,
    uint32_t payload_bytes,
    uint8_t out_header[8]);
uint8_t persist_codec_build_project_document_header(
    uint32_t total_bytes,
    uint32_t payload_crc,
    uint8_t out_header[PERSIST_CODEC_HEADER_BYTES]);
uint32_t persist_codec_crc32_update(uint32_t crc,
                                    const uint8_t *data,
                                    uint32_t length);
persist_codec_result_t persist_codec_prevalidate_project(const persist_codec_source_t *source,
                                                          uint32_t *out_total_bytes);
persist_codec_result_t persist_codec_decode_project_progressive(
    const persist_codec_source_t *source,
    persist_codec_project_workspace_t *workspace,
    const persist_codec_project_consumer_t *project,
    const persist_codec_pattern_consumer_t *patterns);

#ifdef __cplusplus
}
#endif

#endif /* PERSISTENT_CONTROL_CODEC_H */
