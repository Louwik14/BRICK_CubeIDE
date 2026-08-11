#ifndef LOOPER_STORAGE_H
#define LOOPER_STORAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOOPER_STORAGE_PATH_MAX 96U
#define LOOPER_STORAGE_SAVE_PATH_TRIES 10000U

typedef enum
{
    LOOPER_STORAGE_PATH_OK = 0,
    LOOPER_STORAGE_PATH_BUSY,
    LOOPER_STORAGE_PATH_FAIL
} looper_storage_path_result_t;

looper_storage_path_result_t looper_storage_make_next_path(uint8_t track_id,
                                                           char *out_path,
                                                           uint32_t out_len);
uint8_t looper_storage_copy_wav_path_as_rec(const char *wav_path,
                                            char *out_path,
                                            uint32_t out_len);

#ifdef __cplusplus
}
#endif

#endif /* LOOPER_STORAGE_H */
