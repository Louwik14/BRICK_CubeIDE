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
