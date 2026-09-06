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
    SD_ACCESS_CLIENT_WAV_CONVERT = 7,
    SD_ACCESS_CLIENT_EDITOR_CACHE = 8,
    SD_ACCESS_CLIENT_WAVEFORM_CACHE = 9,
    SD_ACCESS_CLIENT_SAMPLE_STREAM = 10,
    SD_ACCESS_CLIENT_PATCH = 11,
    SD_ACCESS_CLIENT_SCHEDULED_RECORDER = 12,
    SD_ACCESS_CLIENT_BACKGROUND = 13,
    SD_ACCESS_CLIENT_MAX = SD_ACCESS_CLIENT_BACKGROUND
} sd_access_client_t;

typedef enum
{
    SD_STORAGE_STATUS_UNKNOWN = 0,
    SD_STORAGE_STATUS_READY,
    SD_STORAGE_STATUS_NO_MEDIA,
    SD_STORAGE_STATUS_FAULT
} sd_storage_status_t;

void sd_access_gate_init(void);
uint8_t sd_access_gate_try_acquire(sd_access_client_t client);
uint8_t sd_access_gate_try_acquire_for_owner(sd_access_client_t client,
                                             uint8_t storage_owner);
void sd_access_gate_release(sd_access_client_t client);
void sd_access_gate_set_streaming_critical(uint8_t active);
uint8_t sd_access_gate_streaming_critical_active(void);
sd_access_client_t sd_access_gate_current_owner(void);
sd_access_client_t sd_access_gate_last_owner(void);
uint32_t sd_access_gate_max_hold_ticks(void);
uint32_t sd_access_gate_acquire_fail_count(sd_access_client_t client);
uint32_t sd_access_gate_client_cycles(sd_access_client_t client);
const char *sd_access_gate_client_label(sd_access_client_t client);
const char *sd_access_gate_busy_label(void);

uint8_t sd_access_fs_mount_if_needed(void);
void sd_access_fs_invalidate_mount(void);
sd_storage_status_t sd_access_storage_status(void);
void sd_access_storage_report_init_failure(uint8_t no_media);
uint32_t sd_access_media_epoch(void);
void sd_access_media_epoch_advance(void);
void sd_access_media_set_present(uint8_t present);


#endif
