#ifndef PROJECT_PRODUCT_H
#define PROJECT_PRODUCT_H
#include <stdint.h>
#define PROJECT_PRODUCT_SLOT_COUNT 16U
typedef struct { uint8_t active,complete; uint32_t done,total; } project_product_progress_t;
void project_product_init(void);
void project_product_refresh_slots(void);
uint8_t project_product_list_slots(uint8_t*out,uint8_t capacity);
uint8_t project_product_slot_present(uint8_t slot);
uint8_t project_product_save(uint8_t slot);
uint8_t project_product_load(uint8_t slot);
uint8_t project_product_delete(uint8_t slot);
uint8_t project_product_blank(void);
uint8_t project_product_restore_boot(void);
uint8_t project_product_get_progress(project_product_progress_t*out);
#endif
