#include "Platform/boot_diag.h"

#include "Platform/memory_layout.h"
#include "stm32h7xx.h"

#define BOOT_DIAG_MAGIC UINT32_C(0x424F4F54)
#define BOOT_DIAG_VERSION 1U

IRQ_SHARED_D3 volatile boot_diag_t g_boot_diag
    __attribute__((used, externally_visible));

void __attribute__((used, externally_visible, noinline)) boot_diag_reset_entry(void)
{
    if ((g_boot_diag.magic != BOOT_DIAG_MAGIC)
        || (g_boot_diag.version != BOOT_DIAG_VERSION)
        || (g_boot_diag.size != sizeof(g_boot_diag)))
    {
        volatile uint32_t *const words = (volatile uint32_t *)&g_boot_diag;
        for (uint32_t i = 0U; i < (sizeof(g_boot_diag) / sizeof(uint32_t)); ++i)
        {
            words[i] = 0U;
        }
        g_boot_diag.magic = BOOT_DIAG_MAGIC;
        g_boot_diag.version = BOOT_DIAG_VERSION;
        g_boot_diag.size = sizeof(g_boot_diag);
    }
    ++g_boot_diag.boot_count;
    g_boot_diag.stage = BOOT_DIAG_STAGE_RESET_ENTRY;
    g_boot_diag.last_pc_tag = 0U;
    g_boot_diag.fault_kind = BOOT_DIAG_FAULT_NONE;
    g_boot_diag.reset_reason = RCC->RSR;
    __DSB();
}

void __attribute__((used, externally_visible, noinline))
boot_diag_mark(boot_diag_stage_t stage)
{
    g_boot_diag.last_pc_tag = (uint32_t)(uintptr_t)__builtin_return_address(0);
    g_boot_diag.stage = (uint32_t)stage;
    __DMB();
}

static void boot_diag_capture_scb(void)
{
    g_boot_diag.cfsr = SCB->CFSR;
    g_boot_diag.hfsr = SCB->HFSR;
    g_boot_diag.dfsr = SCB->DFSR;
    g_boot_diag.afsr = SCB->AFSR;
    g_boot_diag.mmfar = SCB->MMFAR;
    g_boot_diag.bfar = SCB->BFAR;
    g_boot_diag.shcsr = SCB->SHCSR;
    g_boot_diag.msp = __get_MSP();
    g_boot_diag.psp = __get_PSP();
    g_boot_diag.ipsr = __get_IPSR();
}

void __attribute__((used, externally_visible, noinline, noreturn))
boot_diag_fault_capture(const uint32_t *stack, uint32_t exc_return,
                        uint32_t fault_kind)
{
    __disable_irq();
    g_boot_diag.fault_kind = fault_kind;
    g_boot_diag.exc_return = exc_return;
    boot_diag_capture_scb();
    if (stack != 0)
    {
        g_boot_diag.stacked_r0 = stack[0];
        g_boot_diag.stacked_r1 = stack[1];
        g_boot_diag.stacked_r2 = stack[2];
        g_boot_diag.stacked_r3 = stack[3];
        g_boot_diag.stacked_r12 = stack[4];
        g_boot_diag.stacked_lr = stack[5];
        g_boot_diag.stacked_pc = stack[6];
        g_boot_diag.stacked_xpsr = stack[7];
    }
    __DSB();
    for (;;) { __NOP(); }
}

void __attribute__((used, externally_visible, noinline, noreturn))
boot_diag_error_capture(uint32_t return_address)
{
    __disable_irq();
    g_boot_diag.fault_kind = BOOT_DIAG_FAULT_ERROR_HANDLER;
    g_boot_diag.stacked_pc = return_address;
    boot_diag_capture_scb();
    __DSB();
    for (;;) { __NOP(); }
}
