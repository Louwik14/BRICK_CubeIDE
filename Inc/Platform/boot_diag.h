#ifndef BOOT_DIAG_H
#define BOOT_DIAG_H

#include <stdint.h>

typedef enum
{
    BOOT_DIAG_STAGE_RESET_ENTRY = 1U,
    BOOT_DIAG_STAGE_EXIT_RUN0_DONE,
    BOOT_DIAG_STAGE_SYSTEM_INIT_DONE,
    BOOT_DIAG_STAGE_MAIN_ENTER,
    BOOT_DIAG_STAGE_MPU_READY,
    BOOT_DIAG_STAGE_CACHE_READY,
    BOOT_DIAG_STAGE_HAL_READY,
    BOOT_DIAG_STAGE_CLOCKS_READY,
    BOOT_DIAG_STAGE_FMC_CONTROLLER_READY,
    BOOT_DIAG_STAGE_PERIPHERALS_READY,
    BOOT_DIAG_STAGE_FATFS_DRIVER_READY,
    BOOT_DIAG_STAGE_SDRAM_SEQUENCE_BEGIN,
    BOOT_DIAG_STAGE_SDRAM_READY,
    BOOT_DIAG_STAGE_SDRAM_COLD_CLEAR_DONE,
    BOOT_DIAG_STAGE_CONTROL_INIT_DONE,
    BOOT_DIAG_STAGE_SEQ_INIT_DONE,
    BOOT_DIAG_STAGE_SAMPLER_INIT_DONE,
    BOOT_DIAG_STAGE_AUDIO_INIT_DONE,
    BOOT_DIAG_STAGE_UI_INIT_DONE,
    BOOT_DIAG_STAGE_APP_INIT_DONE,
    BOOT_DIAG_STAGE_SUPERLOOP,
    BOOT_DIAG_STAGE_SD_MOUNTED,
    BOOT_DIAG_STAGE_AUDIO_DMA_STARTED,
    BOOT_DIAG_STAGE_BENCHMARK_STARTED
} boot_diag_stage_t;

typedef enum
{
    BOOT_DIAG_FAULT_NONE = 0U,
    BOOT_DIAG_FAULT_HARD = 1U,
    BOOT_DIAG_FAULT_MEMMANAGE = 2U,
    BOOT_DIAG_FAULT_BUS = 3U,
    BOOT_DIAG_FAULT_USAGE = 4U,
    BOOT_DIAG_FAULT_ERROR_HANDLER = 5U
} boot_diag_fault_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t boot_count;
    uint32_t stage;
    uint32_t last_pc_tag;
    uint32_t fault_kind;
    uint32_t reset_reason;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t dfsr;
    uint32_t afsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t shcsr;
    uint32_t stacked_r0;
    uint32_t stacked_r1;
    uint32_t stacked_r2;
    uint32_t stacked_r3;
    uint32_t stacked_r12;
    uint32_t stacked_lr;
    uint32_t stacked_pc;
    uint32_t stacked_xpsr;
    uint32_t msp;
    uint32_t psp;
    uint32_t exc_return;
    uint32_t ipsr;
} boot_diag_t;

extern volatile boot_diag_t g_boot_diag;

void boot_diag_reset_entry(void);
void boot_diag_mark(boot_diag_stage_t stage);
void boot_diag_fault_capture(const uint32_t *stack, uint32_t exc_return,
                             uint32_t fault_kind) __attribute__((noreturn));
void boot_diag_error_capture(uint32_t return_address) __attribute__((noreturn));

#endif
