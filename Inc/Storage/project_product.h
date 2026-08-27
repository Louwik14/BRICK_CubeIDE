#ifndef PROJECT_PRODUCT_H
#define PROJECT_PRODUCT_H
#include <stdint.h>
#define PROJECT_PRODUCT_SLOT_COUNT 16U
typedef struct { uint8_t active,complete; uint32_t done,total; } project_product_progress_t;
typedef enum {
    PROJECT_PRODUCT_SAVE_ERROR_NONE=0,
    PROJECT_PRODUCT_SAVE_ERROR_ARGUMENT,
    PROJECT_PRODUCT_SAVE_ERROR_WORKSPACE_BUSY,
    PROJECT_PRODUCT_SAVE_ERROR_SNAPSHOT,
    PROJECT_PRODUCT_SAVE_ERROR_SD_BUSY,
    PROJECT_PRODUCT_SAVE_ERROR_DIRECTORY,
    PROJECT_PRODUCT_SAVE_ERROR_OPEN,
    PROJECT_PRODUCT_SAVE_ERROR_CODEC,
    PROJECT_PRODUCT_SAVE_ERROR_SYNC,
    PROJECT_PRODUCT_SAVE_ERROR_CLOSE,
    PROJECT_PRODUCT_SAVE_ERROR_REPLACE,
    PROJECT_PRODUCT_SAVE_ERROR_PATTERN,
    PROJECT_PRODUCT_SAVE_ERROR_MEDIA_CHANGED
} project_product_save_error_t;
typedef enum {
    PROJECT_PRODUCT_BOOT_RESTORE_FAILED = 0,
    PROJECT_PRODUCT_BOOT_RESTORE_DEFAULTS_READY,
    PROJECT_PRODUCT_BOOT_RESTORE_PROJECT_READY
} project_product_boot_restore_result_t;
void project_product_init(void);
void project_product_refresh_slots(void);
uint8_t project_product_list_slots(uint8_t*out,uint8_t capacity);
uint8_t project_product_slot_present(uint8_t slot);
uint8_t project_product_save(uint8_t slot);
void project_product_save_service(void);
uint8_t project_product_save_busy(void);
uint8_t project_product_save_take_result(uint8_t *slot,uint8_t *success);
project_product_save_error_t project_product_save_last_error(void);
int32_t project_product_save_last_detail(void);
uint8_t project_product_load(uint8_t slot);
uint8_t project_product_delete(uint8_t slot);
uint8_t project_product_blank(void);
project_product_boot_restore_result_t project_product_restore_boot(void);
uint8_t project_product_get_progress(project_product_progress_t*out);
#endif
