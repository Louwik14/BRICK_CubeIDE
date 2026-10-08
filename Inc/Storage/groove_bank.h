#ifndef GROOVE_BANK_H
#define GROOVE_BANK_H

#include <stdint.h>

#define GROOVE_BANK_MAX_SOURCES 127U
#define GROOVE_BANK_MAX_POINTS 128U
#define GROOVE_BANK_NAME_BYTES 64U
#define GROOVE_BANK_HEADER_BYTES 256U
#define GROOVE_BANK_CATALOG_ENTRY_BYTES 80U
#define GROOVE_BANK_RECORD_HEADER_BYTES 32U
#define GROOVE_BANK_POINT_BYTES 12U
#define GROOVE_BANK_RECORD_MAX_BYTES 1568U
#define GROOVE_BANK_DATA_OFFSET 10432U
#define GROOVE_BANK_WORST_CASE_BYTES 209568U
#define GROOVE_BANK_WORST_CASE_MARGIN_BYTES 52576U

typedef enum
{
    GROOVE_BANK_ENTRY_INVALID = 0,
    GROOVE_BANK_ENTRY_READY = 1
} groove_bank_entry_status_t;

typedef enum
{
    GROOVE_STATE_WAIT_MEDIA = 0,
    GROOVE_STATE_SCAN_OPEN,
    GROOVE_STATE_SCAN_NEXT,
    GROOVE_STATE_SCAN_CLOSE,
    GROOVE_STATE_DECIDE,
    GROOVE_STATE_ERASE,
    GROOVE_STATE_IMPORT_OPEN,
    GROOVE_STATE_IMPORT_READ,
    GROOVE_STATE_IMPORT_CLOSE,
    GROOVE_STATE_RECORD_PROGRAM,
    GROOVE_STATE_CATALOG_PROGRAM,
    GROOVE_STATE_HEADER_PROGRAM,
    GROOVE_STATE_COMMIT,
    GROOVE_STATE_REMOVE_MARKER,
    GROOVE_STATE_READY,
    GROOVE_STATE_FAILED
} groove_state_t;

typedef enum
{
    GROOVE_BOOT_STEP_INIT = 1U,
    GROOVE_BOOT_STEP_SERVICE,
    GROOVE_BOOT_STEP_WAIT_STORAGE_ADMISSION,
    GROOVE_BOOT_STEP_STORAGE_ADMITTED,
    GROOVE_BOOT_STEP_MOUNT,
    GROOVE_BOOT_STEP_DIRECTORY_OPEN,
    GROOVE_BOOT_STEP_DIRECTORY_READ,
    GROOVE_BOOT_STEP_DIRECTORY_CLOSE,
    GROOVE_BOOT_STEP_FILE_OPEN,
    GROOVE_BOOT_STEP_FILE_READ,
    GROOVE_BOOT_STEP_FILE_CLOSE,
    GROOVE_BOOT_STEP_FLASH,
    GROOVE_BOOT_STEP_REMOVE_MARKER,
    GROOVE_BOOT_STEP_COMPLETE
} groove_boot_step_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t state;
    uint32_t previous_state;
    uint32_t step;
    uint32_t error;
    int32_t fresult;
    uint32_t progress;
    uint32_t calls;
    uint32_t last_transition_cycles;
    uint32_t media_epoch;
    uint32_t storage_status;
    uint32_t background_admission;
    uint32_t scheduler_owner;
    uint32_t scheduler_class;
    uint32_t gate_owner;
    uint32_t gate_held_count;
    uint32_t streaming_critical;
} groove_boot_diag_t;

extern volatile groove_boot_diag_t g_groove_boot_diag;

typedef struct
{
    const char *name;
    const uint8_t *record;
    uint16_t record_bytes;
    uint8_t point_count;
    uint8_t runtime_index;
    uint32_t record_crc32;
    groove_bank_entry_status_t status;
} groove_bank_entry_view_t;

typedef struct
{
    const uint8_t *points;
    uint64_t period_q32;
    uint16_t signature_numerator;
    uint16_t signature_denominator;
    uint8_t point_count;
    uint8_t base;
    uint8_t quantize;
    uint8_t timing;
    uint8_t random;
    int8_t velocity;
    uint8_t loop_on;
} groove_bank_record_view_t;

void groove_bank_init(void);
void groove_bank_service(void);
uint8_t groove_bank_boot_complete(void);
uint8_t groove_bank_ready(void);
uint8_t groove_bank_overflow(void);
uint8_t groove_bank_count(void);
uint8_t groove_bank_get(uint8_t runtime_index,
                        groove_bank_entry_view_t *out_entry);
uint8_t groove_bank_find_name(const char *name, uint8_t *out_runtime_index);
uint8_t groove_bank_validate_record(uint8_t runtime_index);
uint8_t groove_bank_resolve(uint8_t runtime_index,
                            groove_bank_record_view_t *out_record);

#endif
