#include "Storage/project_storage_paths.h"

#include <stdio.h>

static uint8_t path_format(char *out, uint32_t capacity, const char *format,
                           uint8_t a, uint8_t b, uint8_t c)
{
    if (out == NULL || capacity == 0U || format == NULL) return 0U;
    const int length = snprintf(out, capacity, format, a, b, c);
    return (length > 0 && (uint32_t)length < capacity) ? 1U : 0U;
}

uint8_t project_storage_projects_root(char *out, uint32_t capacity)
{
    return path_format(out, capacity, "0:/PROJECTS", 0U, 0U, 0U);
}

uint8_t project_storage_project_dir(char *out, uint32_t capacity, uint8_t slot)
{
    return path_format(out, capacity, "0:/PROJECTS/P%02u", slot, 0U, 0U);
}

uint8_t project_storage_project_file(char *out, uint32_t capacity, uint8_t slot)
{
    return path_format(out, capacity, "0:/PROJECTS/P%02u/PROJECT.B6C", slot, 0U, 0U);
}

uint8_t project_storage_patterns_dir(char *out, uint32_t capacity, uint8_t slot)
{
    return path_format(out, capacity, "0:/PROJECTS/P%02u/PATTERNS", slot, 0U, 0U);
}

uint8_t project_storage_pattern_file(char *out, uint32_t capacity, uint8_t slot,
                                     uint8_t bank, uint8_t pattern)
{
    return path_format(out, capacity,
                       "0:/PROJECTS/P%02u/PATTERNS/B%02u_P%02u.B6C",
                       slot, bank, pattern);
}

uint8_t project_storage_internal_root(char *out, uint32_t capacity)
{
    return path_format(out, capacity, "0:/BRICK", 0U, 0U, 0U);
}

uint8_t project_storage_transactions_root(char *out, uint32_t capacity)
{
    return path_format(out, capacity, "0:/BRICK/TRANSACTIONS", 0U, 0U, 0U);
}

uint8_t project_storage_project_transaction_root(char *out, uint32_t capacity)
{
    return path_format(out, capacity, "0:/BRICK/TRANSACTIONS/PROJECT", 0U, 0U, 0U);
}

uint8_t project_storage_project_transaction_dir(char *out, uint32_t capacity,
                                                uint8_t slot)
{
    return path_format(out, capacity,
                       "0:/BRICK/TRANSACTIONS/PROJECT/P%02u", slot, 0U, 0U);
}

uint8_t project_storage_project_transaction_file(char *out, uint32_t capacity,
                                                 uint8_t slot)
{
    return path_format(out, capacity,
                       "0:/BRICK/TRANSACTIONS/PROJECT/P%02u/PROJECT.B6C",
                       slot, 0U, 0U);
}

uint8_t project_storage_project_transaction_patterns_dir(char *out,
                                                         uint32_t capacity,
                                                         uint8_t slot)
{
    return path_format(out, capacity,
                       "0:/BRICK/TRANSACTIONS/PROJECT/P%02u/PATTERNS",
                       slot, 0U, 0U);
}

uint8_t project_storage_project_delete_transaction_dir(char *out,
                                                       uint32_t capacity,
                                                       uint8_t slot)
{
    return path_format(out, capacity,
                       "0:/BRICK/TRANSACTIONS/DELETE_P%02u", slot, 0U, 0U);
}

uint8_t project_storage_patches_root(char *out, uint32_t capacity)
{
    return path_format(out, capacity, "0:/PATCHES", 0U, 0U, 0U);
}

uint8_t project_storage_patch_file(char *out,uint32_t capacity,uint16_t slot)
{
    if(out==NULL||capacity==0U)return 0U;
    const int length=snprintf(out,capacity,"0:/PATCHES/P%04u.B6C",slot);
    return(length>0&&(uint32_t)length<capacity)?1U:0U;
}
