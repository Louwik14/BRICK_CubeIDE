#pragma once

#include <stdint.h>

typedef enum
{
    POWER_SHUTDOWN_IDLE = 0,
    POWER_SHUTDOWN_WAIT_STORAGE,
    POWER_SHUTDOWN_WAIT_QUIESCE,
    POWER_SHUTDOWN_CAPTURE_PATTERN,
    POWER_SHUTDOWN_WRITE_RESUME,
    POWER_SHUTDOWN_FINAL,
    POWER_SHUTDOWN_FAILED
} power_shutdown_phase_t;

typedef enum
{
    POWER_SHUTDOWN_FAILURE_NONE = 0,
    POWER_SHUTDOWN_FAILURE_STORAGE_NO_MEDIA,
    POWER_SHUTDOWN_FAILURE_STORAGE_FAULT,
    POWER_SHUTDOWN_FAILURE_RESUME_BEGIN,
    POWER_SHUTDOWN_FAILURE_RESUME_SAVE
} power_shutdown_failure_t;

typedef enum
{
    POWER_SHUTDOWN_RESUME_NONE = 0,
    POWER_SHUTDOWN_RESUME_STARTED,
    POWER_SHUTDOWN_RESUME_SUCCEEDED,
    POWER_SHUTDOWN_RESUME_FAILED
} power_shutdown_resume_result_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t current_phase;
    uint32_t last_phase_reached;
    uint32_t failure_reason;
    uint32_t failure_phase;
    uint32_t request_count;
    uint32_t abort_count;
    uint32_t last_storage_status;
    uint32_t resume_result;
    uint32_t resume_error;
    int32_t resume_detail;
    uint32_t quiesce_requested;
    uint32_t quiesce_panic_published;
    uint32_t quiesce_audio_acknowledged;
    uint32_t quiesce_reservations_cancelled;
} power_shutdown_debug_t;

extern volatile power_shutdown_debug_t g_power_shutdown_debug;

uint8_t power_shutdown_service(uint32_t now_ms);
power_shutdown_phase_t power_shutdown_phase(void);
power_shutdown_failure_t power_shutdown_failure(void);
uint8_t power_shutdown_mutations_frozen(void);
