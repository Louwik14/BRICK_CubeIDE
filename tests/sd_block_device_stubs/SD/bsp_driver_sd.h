#ifndef TEST_BSP_DRIVER_SD_H
#define TEST_BSP_DRIVER_SD_H

#include <stdint.h>

#define MSD_OK 0U
#define MSD_ERROR 1U
#define SD_TRANSFER_OK 0U
#define SD_TRANSFER_BUSY 1U
#define SD_PRESENT 1U
#define SD_NOT_PRESENT 0U

uint8_t BSP_SD_GetCardState(void);

#endif
