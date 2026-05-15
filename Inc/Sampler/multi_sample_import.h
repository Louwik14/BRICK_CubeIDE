#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    MULTI_SAMPLE_IMPORT_OK = 0,
    MULTI_SAMPLE_IMPORT_INVALID_ARG,
    MULTI_SAMPLE_IMPORT_SD_BUSY,
    MULTI_SAMPLE_IMPORT_SD_MOUNT_FAIL,
    MULTI_SAMPLE_IMPORT_OPEN_DIR_FAIL,
    MULTI_SAMPLE_IMPORT_NO_WAV,
    MULTI_SAMPLE_IMPORT_TOO_MANY_SAMPLES,
    MULTI_SAMPLE_IMPORT_PATH_TOO_LONG,
    MULTI_SAMPLE_IMPORT_WAV_OPEN_FAIL,
    MULTI_SAMPLE_IMPORT_WAV_PARSE_FAIL,
    MULTI_SAMPLE_IMPORT_WAV_UNSUPPORTED,
    MULTI_SAMPLE_IMPORT_DUPLICATE_ZONE,
    MULTI_SAMPLE_IMPORT_ZONE_LIMIT,
    MULTI_SAMPLE_IMPORT_INDEX_WRITE_FAIL
} multi_sample_import_result_t;

multi_sample_import_result_t multi_sample_import_folder(const char *instrument_dir);
multi_sample_import_result_t multi_sample_import_get_last_result(void);
const char *multi_sample_import_get_last_diagnostic(void);
uint16_t multi_sample_import_get_last_sample_count(void);
uint16_t multi_sample_import_get_last_zone_count(void);

#ifdef __cplusplus
}
#endif
