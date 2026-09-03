#ifndef PATCH_PRODUCT_H
#define PATCH_PRODUCT_H
#include <stdint.h>
#define PATCH_PRODUCT_SLOT_COUNT 192U
#define PATCH_PRODUCT_INVALID_SLOT 0xFFFFU
typedef enum { PATCH_PRODUCT_OK=0,PATCH_PRODUCT_PENDING,PATCH_PRODUCT_INVALID,PATCH_PRODUCT_EMPTY,PATCH_PRODUCT_IO_BUSY,PATCH_PRODUCT_IO_ERROR,PATCH_PRODUCT_NO_SLOT } patch_product_result_t;
typedef enum { PATCH_PRODUCT_SLOT_EMPTY=0,PATCH_PRODUCT_SLOT_VALID,PATCH_PRODUCT_SLOT_INVALID } patch_product_slot_state_t;
typedef struct { char name[33]; uint8_t family,type,source_track,summary_family,summary_type; } patch_product_metadata_t;
void patch_product_init(void);
void patch_product_storage_init(void);
patch_product_result_t patch_product_save(uint8_t entity,uint16_t*out_slot);
void patch_product_save_service(void);
patch_product_result_t patch_product_apply(uint16_t slot,uint8_t entity);
void patch_product_apply_service(void);
void patch_product_storage_request_service(void);
void patch_product_control_process_intent(uint8_t operation, uint16_t slot,
                                           uint16_t target_mask, uint8_t entity,
                                           const char *name);
void patch_product_control_process(void);
patch_product_result_t patch_product_rename(uint16_t slot,const char*name);
patch_product_result_t patch_product_delete(uint16_t slot,uint16_t*out_next);
patch_product_slot_state_t patch_product_slot_state(uint16_t slot);
uint8_t patch_product_metadata(uint16_t slot,patch_product_metadata_t*out);
uint16_t patch_product_first_empty(void);
void patch_product_set_current(uint16_t slot);
uint16_t patch_product_get_current(void);
const char*patch_product_result_label(patch_product_result_t result);
#endif
