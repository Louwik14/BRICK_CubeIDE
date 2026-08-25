#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SD_SCHEDULER_SECTOR_BYTES (512U)
#define SD_SCHEDULER_MARGIN_UNKNOWN (UINT32_MAX)

typedef enum
{
    SD_SCHEDULER_CLASS_NONE = 0,
    SD_SCHEDULER_CLASS_READ,
    SD_SCHEDULER_CLASS_WRITE,
    SD_SCHEDULER_CLASS_FILESYSTEM,
    SD_SCHEDULER_CLASS_COUNT
} sd_scheduler_class_t;

typedef enum
{
    SD_SCHEDULER_OWNER_IDLE = 0,
    SD_SCHEDULER_OWNER_READ_DMA,
    SD_SCHEDULER_OWNER_WRITE_DMA,
    SD_SCHEDULER_OWNER_FILESYSTEM,
    SD_SCHEDULER_OWNER_BACKGROUND,
    SD_SCHEDULER_OWNER_RECOVERY_ABORT
} sd_scheduler_owner_t;

typedef enum
{
    SD_SCHEDULER_RESERVATION_SAFE = 0,
    SD_SCHEDULER_RESERVATION_LOW,
    SD_SCHEDULER_RESERVATION_CRITICAL
} sd_scheduler_reservation_t;

typedef struct
{
    sd_scheduler_class_t type;
    uint32_t margin_us;
    uint32_t estimated_cost_us;
    uint32_t lba;
    uint32_t sector_count;
    void *read_buffer;
    const void *write_buffer;
    uint32_t owner_generation;
    uint32_t media_epoch;
    sd_scheduler_reservation_t reservation;
    uint8_t ready;
} sd_scheduler_candidate_t;

typedef enum
{
    SD_SCHEDULER_START_STARTED = 0,
    SD_SCHEDULER_START_COMPLETED,
    SD_SCHEDULER_START_BUSY,
    SD_SCHEDULER_START_ERROR
} sd_scheduler_start_result_t;

typedef enum
{
    SD_SCHEDULER_POLL_ACTIVE = 0,
    SD_SCHEDULER_POLL_COMPLETED,
    SD_SCHEDULER_POLL_ERROR,
    SD_SCHEDULER_POLL_RECOVERY_ABORT
} sd_scheduler_poll_result_t;

typedef uint8_t (*sd_scheduler_peek_fn)(void *context,
                                        sd_scheduler_candidate_t *candidate);
typedef sd_scheduler_start_result_t (*sd_scheduler_start_fn)(
    void *context,
    const sd_scheduler_candidate_t *candidate,
    uint32_t granted_sector_count);
typedef sd_scheduler_poll_result_t (*sd_scheduler_poll_fn)(void *context);

typedef struct
{
    void *context;
    sd_scheduler_peek_fn peek;
    sd_scheduler_start_fn start;
    sd_scheduler_poll_fn poll;
} sd_scheduler_provider_t;

typedef struct
{
    uint32_t critical_margin_us;
    uint32_t reservation_low_margin_us;
    uint32_t reservation_critical_margin_us;
    uint32_t transaction_guard_us;
    uint32_t worst_case_us_per_sector;
    uint32_t max_write_sectors;
    uint32_t starvation_limit_us;
} sd_scheduler_config_t;

typedef struct
{
    uint32_t read_transactions;
    uint32_t write_transactions;
    uint32_t filesystem_slots;
    uint32_t read_to_write_switches;
    uint32_t write_to_read_switches;
    uint32_t max_read_wait_us;
    uint32_t max_write_wait_us;
    uint32_t max_filesystem_wait_us;
    uint32_t min_read_margin_us;
    uint32_t min_write_margin_us;
    uint32_t min_reservation_margin_us;
    uint32_t urgent_read_decisions;
    uint32_t urgent_write_decisions;
    uint32_t critical_ties;
    uint32_t starvation_prevented;
    uint32_t busy_rejects;
    uint32_t errors;
    uint32_t reservation_policy_failures;
    uint32_t write_burst_limits;
} sd_scheduler_metrics_t;

typedef struct
{
    sd_scheduler_config_t config;
    sd_scheduler_provider_t providers[SD_SCHEDULER_CLASS_COUNT];
    sd_scheduler_metrics_t metrics;
    uint32_t wait_since_us[SD_SCHEDULER_CLASS_COUNT];
    uint32_t active_media_epoch;
    sd_scheduler_owner_t owner;
    sd_scheduler_class_t active_class;
    sd_scheduler_class_t last_dma_class;
    uint8_t wait_active[SD_SCHEDULER_CLASS_COUNT];
    uint8_t round_robin_cursor;
} sd_scheduler_t;

void sd_scheduler_default_config(sd_scheduler_config_t *config);
void sd_scheduler_init(sd_scheduler_t *scheduler,
                       const sd_scheduler_config_t *config);
uint8_t sd_scheduler_bind_provider(sd_scheduler_t *scheduler,
                                   sd_scheduler_class_t type,
                                   const sd_scheduler_provider_t *provider);
void sd_scheduler_service(sd_scheduler_t *scheduler,
                          uint32_t now_us,
                          uint32_t media_epoch);
uint8_t sd_scheduler_background_can_start(sd_scheduler_t *scheduler,
                                          uint32_t media_epoch);
sd_scheduler_owner_t sd_scheduler_owner(const sd_scheduler_t *scheduler);
void sd_scheduler_metrics_get(const sd_scheduler_t *scheduler,
                              sd_scheduler_metrics_t *metrics);

#ifdef __cplusplus
}
#endif
