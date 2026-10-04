#ifndef PATTERN_WORKING_BANK_H
#define PATTERN_WORKING_BANK_H

#include <stdint.h>

#include "Storage/persistent_control_codec.h"

typedef enum
{
    PATTERN_WORKING_BASE_BLANK = 0,
    PATTERN_WORKING_BASE_PROJECT
} pattern_working_base_kind_t;

typedef enum
{
    PATTERN_WORKING_OPERATION_NONE = 0,
    PATTERN_WORKING_OPERATION_LOAD,
    PATTERN_WORKING_OPERATION_RECONCILE,
    PATTERN_WORKING_OPERATION_DISCARD
} pattern_working_operation_t;

void pattern_working_bank_init(void);
uint8_t pattern_working_bank_start_project_mounted(uint8_t project_slot);
void pattern_working_bank_publish_empty_project(uint8_t project_slot);
void pattern_working_bank_start_blank(void);
pattern_working_base_kind_t pattern_working_bank_base_kind(void);
uint8_t pattern_working_bank_present(uint8_t bank, uint8_t pattern);
void pattern_working_bank_copy_dirty(uint32_t out_words[8]);

uint8_t pattern_working_bank_load_async_begin(
    uint8_t bank, uint8_t pattern, uint8_t *encoded,
    uint32_t encoded_capacity, persist_control_pattern_t *out);
uint8_t pattern_working_bank_reconcile_async_begin(
    uint8_t bank, uint8_t pattern,
    const persist_control_pattern_t *working,
    const persist_control_pattern_t *base_default,
    uint8_t saved_base_present,
    uint8_t *encoded, uint32_t encoded_capacity);
uint8_t pattern_working_bank_discard_async_begin(uint8_t bank,
                                                 uint8_t pattern);
void pattern_working_bank_mark_clean(uint8_t bank, uint8_t pattern);
void pattern_working_bank_async_service(void);
uint8_t pattern_working_bank_async_busy(void);
uint8_t pattern_working_bank_async_take_result(
    pattern_working_operation_t *operation, uint8_t *bank, uint8_t *pattern,
    uint8_t *success);

#endif /* PATTERN_WORKING_BANK_H */
