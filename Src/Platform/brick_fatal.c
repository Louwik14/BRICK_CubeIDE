#include "Platform/brick_fatal.h"

#include "main.h"
#include "stm32h7xx.h"

brick_fatal_record_t g_brick_fatal_record;

_Noreturn void brick_fatal_raise(brick_fatal_code_t code,
                       uint32_t entity,
                       uint32_t context,
                       uint32_t requested,
                       uint32_t capacity)
{
    __disable_irq();
    g_brick_fatal_record.code = (uint32_t)code;
    g_brick_fatal_record.entity = entity;
    g_brick_fatal_record.context = context;
    g_brick_fatal_record.requested = requested;
    g_brick_fatal_record.capacity = capacity;
    __DMB();
    Error_Handler();
    for (;;)
    {
    }
}
