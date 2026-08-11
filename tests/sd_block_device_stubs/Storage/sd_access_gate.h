#ifndef TEST_SD_ACCESS_GATE_H
#define TEST_SD_ACCESS_GATE_H

#include <stdint.h>

typedef enum
{
    SD_ACCESS_CLIENT_NONE = 0,
    SD_ACCESS_CLIENT_RECORDER = 1,
    SD_ACCESS_CLIENT_SAMPLE_STREAM = 2
} sd_access_client_t;

sd_access_client_t sd_access_gate_current_owner(void);
uint32_t sd_access_media_epoch(void);

#endif
