#ifndef PROJECT_STORAGE_PATHS_H
#define PROJECT_STORAGE_PATHS_H

#include <stdint.h>

#define PROJECT_STORAGE_SLOT_COUNT 16U

uint8_t project_storage_projects_root(char *out, uint32_t capacity);
uint8_t project_storage_project_dir(char *out, uint32_t capacity, uint8_t slot);
uint8_t project_storage_project_file(char *out, uint32_t capacity, uint8_t slot);
uint8_t project_storage_patterns_dir(char *out, uint32_t capacity, uint8_t slot);
uint8_t project_storage_pattern_file(char *out, uint32_t capacity, uint8_t slot,
                                     uint8_t bank, uint8_t pattern);
uint8_t project_storage_internal_root(char *out, uint32_t capacity);
uint8_t project_storage_transactions_root(char *out, uint32_t capacity);
uint8_t project_storage_project_transaction_root(char *out, uint32_t capacity);
uint8_t project_storage_project_transaction_dir(char *out, uint32_t capacity,
                                                uint8_t slot);
uint8_t project_storage_project_transaction_file(char *out, uint32_t capacity,
                                                 uint8_t slot);
uint8_t project_storage_project_transaction_patterns_dir(char *out,
                                                         uint32_t capacity,
                                                         uint8_t slot);
uint8_t project_storage_project_delete_transaction_dir(char *out,
                                                       uint32_t capacity,
                                                       uint8_t slot);
uint8_t project_storage_working_root(char *out, uint32_t capacity);
uint8_t project_storage_working_patterns_dir(char *out, uint32_t capacity);
uint8_t project_storage_working_pattern_file(char *out, uint32_t capacity,
                                             uint8_t bank, uint8_t pattern);
uint8_t project_storage_patches_root(char *out, uint32_t capacity);
uint8_t project_storage_patch_file(char *out, uint32_t capacity,uint16_t slot);

#endif
