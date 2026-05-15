#ifndef SD_ACCESS_GATE_H
#define SD_ACCESS_GATE_H

#include <stdint.h>

#include "ff.h"

typedef enum
{
    SD_ACCESS_CLIENT_NONE = 0,
    SD_ACCESS_CLIENT_RECORDER = 1,
    SD_ACCESS_CLIENT_SAMPLE_BOOT = 2,
    SD_ACCESS_CLIENT_PATTERN = 3,
    SD_ACCESS_CLIENT_PROJECT = 4,
    SD_ACCESS_CLIENT_SAMPLE_CACHE = 5,
    /* Preview is exclusive: no shared ownership with other SD clients. */
    SD_ACCESS_CLIENT_PREVIEW = 6,
    SD_ACCESS_CLIENT_WAV_CONVERT = 7
} sd_access_client_t;

void sd_access_gate_init(void);
uint8_t sd_access_gate_try_acquire(sd_access_client_t client);
void sd_access_gate_release(sd_access_client_t client);

uint8_t sd_access_fs_mount_if_needed(void);

void sd_access_trace_begin(const char *op);
void sd_access_trace_end(const char *op, int result, uint32_t elapsed_ms);
void sd_access_trace_timeout(const char *stage, uint32_t elapsed_ms);

#endif
