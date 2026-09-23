#ifndef BOOT_CONTEXT_SD_H
#define BOOT_CONTEXT_SD_H
#include <stdint.h>
typedef struct { uint8_t active_project_slot; } boot_context_sd_data_t;
uint8_t boot_context_sd_load(boot_context_sd_data_t *out_ctx);
uint8_t boot_context_sd_commit(uint8_t active_project_slot);
void boot_context_sd_clear(void);
#endif
