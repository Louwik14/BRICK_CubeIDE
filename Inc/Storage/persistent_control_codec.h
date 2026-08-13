#ifndef PERSISTENT_CONTROL_CODEC_H
#define PERSISTENT_CONTROL_CODEC_H

#include <stdint.h>

#include "Storage/persistent_control_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PERSIST_CODEC_VERSION 1U
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

typedef struct
{
    persist_codec_write_fn write;
    void *context;
} persist_codec_sink_t;

typedef struct
{
    persist_codec_read_fn read;
    void *context;
} persist_codec_source_t;

/* The provider retains ownership; the pointed record must remain stable for
 * the duration of one encode call (count, CRC and write passes). */
typedef const persist_control_pattern_record_t *(*persist_codec_pattern_get_fn)(void *context,
                                                                                 uint16_t ordinal);
typedef uint8_t (*persist_codec_pattern_put_fn)(void *context,
                                                const persist_control_pattern_record_t *record);
typedef uint8_t (*persist_codec_pattern_finish_fn)(void *context);
typedef void (*persist_codec_pattern_abort_fn)(void *context);

typedef struct
{
    persist_codec_pattern_get_fn get;
    void *context;
} persist_codec_pattern_provider_t;

typedef struct
{
    /* put writes only to caller-owned transactional staging. commit publishes
     * after full-document CRC/validation; abort discards staged records. */
    persist_codec_pattern_put_fn put;
    persist_codec_pattern_finish_fn commit;
    persist_codec_pattern_abort_fn abort;
    void *context;
} persist_codec_pattern_consumer_t;

/* Caller-owned staging: one project header/working Pattern and one streamed
 * bank record. No codec-owned static buffer or dynamic allocation exists. */
typedef struct
{
    persist_control_project_t project;
    persist_control_pattern_record_t pattern_record;
} persist_codec_project_staging_t;

typedef struct
{
    persist_control_pattern_t pattern;
} persist_codec_pattern_staging_t;

typedef struct
{
    persist_control_patch_t patch;
} persist_codec_patch_staging_t;

persist_codec_result_t persist_codec_validate_pattern(const persist_control_pattern_t *pattern);
persist_codec_result_t persist_codec_validate_project(const persist_control_project_t *project);
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
persist_codec_result_t persist_codec_encode_project(const persist_control_project_t *project,
                                                     const persist_codec_pattern_provider_t *patterns,
                                                     const persist_codec_sink_t *sink,
                                                     uint32_t *out_bytes);
persist_codec_result_t persist_codec_decode_project(const persist_codec_source_t *source,
                                                     persist_codec_project_staging_t *staging,
                                                     const persist_codec_pattern_consumer_t *patterns);

#ifdef __cplusplus
}
#endif

#endif /* PERSISTENT_CONTROL_CODEC_H */
