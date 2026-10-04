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

typedef enum
{
    PATTERN_CONTROL_BANK_PROJECT_LOAD_ERROR = 0,
    PATTERN_CONTROL_BANK_PROJECT_LOAD_OK,
    PATTERN_CONTROL_BANK_PROJECT_LOAD_EMPTY
} pattern_control_bank_project_load_result_t;

typedef enum
{
    PATTERN_CONTROL_BANK_STORE_BEGIN_OK = 0,
    PATTERN_CONTROL_BANK_STORE_BEGIN_POLICY,
    PATTERN_CONTROL_BANK_STORE_BEGIN_NO_PROJECT,
    PATTERN_CONTROL_BANK_STORE_BEGIN_ARGUMENT,
    PATTERN_CONTROL_BANK_STORE_BEGIN_BUSY,
    PATTERN_CONTROL_BANK_STORE_BEGIN_CODEC,
    PATTERN_CONTROL_BANK_STORE_BEGIN_PATH
} pattern_control_bank_store_begin_result_t;

typedef enum
{
    PATTERN_CONTROL_BANK_ASYNC_ERROR_NONE = 0,
    PATTERN_CONTROL_BANK_ASYNC_ERROR_MEDIA,
    PATTERN_CONTROL_BANK_ASYNC_ERROR_MOUNT,
    PATTERN_CONTROL_BANK_ASYNC_ERROR_RECOVER,
    PATTERN_CONTROL_BANK_ASYNC_ERROR_OPEN,
    PATTERN_CONTROL_BANK_ASYNC_ERROR_TRANSFER,
    PATTERN_CONTROL_BANK_ASYNC_ERROR_SYNC,
    PATTERN_CONTROL_BANK_ASYNC_ERROR_CLOSE,
    PATTERN_CONTROL_BANK_ASYNC_ERROR_REPLACE,
    PATTERN_CONTROL_BANK_ASYNC_ERROR_DECODE
} pattern_control_bank_async_error_t;

void pattern_control_bank_init(void);
uint8_t pattern_control_bank_activate_project(uint8_t slot);
uint8_t pattern_control_bank_activate_resume_project(
    uint8_t slot, const uint32_t dirty_words[8]);
uint8_t pattern_control_bank_activate_resume_blank(
    const uint32_t dirty_words[8]);
uint8_t pattern_control_bank_validate_project(uint8_t slot);
void pattern_control_bank_deactivate_project(void);
uint8_t pattern_control_bank_active_project(uint8_t *out_slot);
void pattern_control_bank_publish_project(uint8_t slot,
                                          const uint32_t present_words[8]);
uint8_t pattern_control_bank_delete(uint8_t bank,uint8_t pattern);
uint8_t pattern_control_bank_present(uint8_t bank,uint8_t pattern);
void pattern_control_bank_mark_present(uint8_t bank,uint8_t pattern);
uint16_t pattern_control_bank_count(void);
pattern_control_bank_project_load_result_t pattern_control_bank_load_project(
    uint8_t slot,uint8_t bank,uint8_t pattern,persist_control_pattern_t *out);
pattern_control_bank_store_begin_result_t pattern_control_bank_store_async_begin(
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
    uint8_t *success,
    pattern_control_bank_async_error_t *error,
    int32_t *filesystem_result,
    uint32_t *offset);
#endif
