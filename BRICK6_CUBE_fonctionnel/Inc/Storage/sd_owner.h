#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SD_OWNER_NONE = 0,
    SD_OWNER_FATFS,
    SD_OWNER_STREAM
} sd_owner_t;

void sd_set_owner(sd_owner_t owner);
sd_owner_t sd_get_owner(void);

#ifdef __cplusplus
}
#endif
