#ifndef PATCH_PRODUCT_H
#define PATCH_PRODUCT_H
#include <stdint.h>
#include "Storage/persistent_control_model.h"
#define PATCH_PRODUCT_SLOT_COUNT 192U
#define PATCH_PRODUCT_INVALID_SLOT 0xFFFFU
typedef enum
{
    PATCH_PRODUCT_OK = 0,
    PATCH_PRODUCT_PENDING,
    PATCH_PRODUCT_INVALID,
    PATCH_PRODUCT_EMPTY,
    PATCH_PRODUCT_IO_BUSY,
    PATCH_PRODUCT_IO_ERROR,
    PATCH_PRODUCT_NO_SLOT,
    PATCH_PRODUCT_RESULT_INVALID_SLOT,
    PATCH_PRODUCT_RESULT_INVALID_NAME,
    PATCH_PRODUCT_RESULT_FILE_ABSENT,
    PATCH_PRODUCT_RESULT_DECODE_ERROR,
    PATCH_PRODUCT_RESULT_ENCODE_ERROR
} patch_product_result_t;
typedef enum
{
    PATCH_PRODUCT_OPERATION_NONE = 0,
    PATCH_PRODUCT_OPERATION_SAVE,
    PATCH_PRODUCT_OPERATION_RENAME,
    PATCH_PRODUCT_OPERATION_LOAD
} patch_product_operation_t;
typedef enum { PATCH_PRODUCT_SLOT_EMPTY=0,PATCH_PRODUCT_SLOT_VALID,PATCH_PRODUCT_SLOT_INVALID } patch_product_slot_state_t;
typedef struct { char name[33]; uint8_t family,type,source_track,summary_family,summary_type; } patch_product_metadata_t;
void patch_product_init(void);
patch_product_result_t patch_product_save(uint8_t entity,uint16_t*out_slot);
patch_product_result_t patch_product_save_prepare(uint8_t entity,
                                                   uint16_t slot,
                                                   const char *name);
patch_product_result_t patch_product_save_begin(uint16_t slot,
                                                const persist_control_patch_t *snapshot);
patch_product_result_t patch_product_save_submit(uint16_t slot, const char *name);
void patch_product_save_cancel_prepare(void);
patch_product_result_t patch_product_load_begin(uint16_t slot,uint16_t target_mask);
patch_product_result_t patch_product_apply(uint16_t slot,uint8_t entity);
void patch_product_apply_service(void);
patch_product_result_t patch_product_clear(uint8_t entity);
patch_product_result_t patch_product_rename(uint16_t slot,const char*name);
patch_product_result_t patch_product_rename_begin(uint16_t slot,const char *name);
void patch_product_service(void);
uint8_t patch_product_result_pending(patch_product_operation_t operation);
uint8_t patch_product_take_result(patch_product_operation_t *operation,
                                  uint16_t *slot,
                                  patch_product_result_t *result);
patch_product_result_t patch_product_delete(uint16_t slot,uint16_t*out_next);
patch_product_slot_state_t patch_product_slot_state(uint16_t slot);
uint8_t patch_product_metadata(uint16_t slot,patch_product_metadata_t*out);
uint16_t patch_product_first_empty(void);
void patch_product_set_current(uint16_t slot);
uint16_t patch_product_get_current(void);
const char*patch_product_result_label(patch_product_result_t result);
#endif
