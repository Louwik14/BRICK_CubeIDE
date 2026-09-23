#include "Platform/brick_fatal.h"

#include "main.h"
#include "Platform/crash_library.h"
#include "stm32h7xx.h"

brick_fatal_record_t g_brick_fatal_record;

_Noreturn void brick_fatal_raise_at(const char *message,
                                    const char *file,
                                    uint32_t line,
                                    const char *function,
                                    brick_fatal_code_t code,
                                    uint32_t entity,
                                    uint32_t context,
                                    uint32_t requested,
                                    uint32_t capacity)
{
    g_brick_fatal_record.message = message;
    g_brick_fatal_record.file = file;
    g_brick_fatal_record.line = line;
    g_brick_fatal_record.function = function;
    g_brick_fatal_record.code = (uint32_t)code;
    g_brick_fatal_record.entity = entity;
    g_brick_fatal_record.context = context;
    g_brick_fatal_record.requested = requested;
    g_brick_fatal_record.capacity = capacity;
    g_brick_fatal_record.caller_pc =
        (uint32_t)(uintptr_t)__builtin_return_address(0);
    __asm volatile("mov %0, sp" : "=r"(g_brick_fatal_record.caller_sp));
    __DMB();
    crash_library_capture_and_persist(&g_brick_fatal_record);
    __disable_irq();
    Error_Handler();
    for (;;)
    {
    }
}
