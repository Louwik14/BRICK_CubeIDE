#ifndef PATTERN_CONTROL_BANK_H
#define PATTERN_CONTROL_BANK_H
#include <stdint.h>
#include "Storage/persistent_control_codec.h"

typedef enum
{
    PATTERN_CONTROL_BANK_ASYNC_NONE = 0,
    PATTERN_CONTROL_BANK_ASYNC_SAVE,
    PATTERN_CONTROL_BANK_ASYNC_LOAD
} pattern_control_bank_async_operation_t;

void pattern_control_bank_init(void);
uint8_t pattern_control_bank_delete(uint8_t bank,uint8_t pattern);
uint8_t pattern_control_bank_clear(void);
uint8_t pattern_control_bank_present(uint8_t bank,uint8_t pattern);
uint16_t pattern_control_bank_count(void);
uint8_t pattern_control_bank_get_ordinal_project(uint16_t ordinal,persist_control_pattern_record_t*out);
uint8_t pattern_control_bank_get_ordinal_project_path(uint16_t ordinal,
                                                      char *out_path,
                                                      uint32_t path_capacity,
                                                      uint8_t *out_bank,
                                                      uint8_t *out_pattern);
uint8_t pattern_control_bank_put_record_project(const persist_control_pattern_record_t*record);
uint8_t pattern_control_bank_begin_project(void);
uint8_t pattern_control_bank_commit(void*context);
void pattern_control_bank_abort(void*context);
uint8_t pattern_control_bank_store_async_begin(
    uint8_t bank,
    uint8_t pattern,
    const persist_control_pattern_t *in,
    uint8_t *encoded,
    uint32_t encoded_capacity);
uint8_t pattern_control_bank_load_async_begin(
    uint8_t bank,
    uint8_t pattern,
    uint8_t *encoded,
    uint32_t encoded_capacity,
    persist_control_pattern_t *out);
void pattern_control_bank_async_service(void);
uint8_t pattern_control_bank_async_busy(void);
uint8_t pattern_control_bank_async_take_result(
    pattern_control_bank_async_operation_t *operation,
    uint8_t *bank,
    uint8_t *pattern,
    uint8_t *success);
#endif
