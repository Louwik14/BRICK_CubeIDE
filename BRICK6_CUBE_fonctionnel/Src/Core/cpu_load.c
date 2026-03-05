#include "cpu_load.h"
#include "stm32h7xx.h"

#define CPU_LOAD_MAX_PERMILLE 2000U

static uint32_t irq_start_cycles = 0U;
static uint32_t last_irq_entry_cycles = 0U;
static uint32_t current_period_cycles = 0U;

static volatile uint32_t cpu_permille = 0U;
static volatile uint32_t cpu_max_permille = 0U;
static volatile uint32_t cpu_counter_valid = 0U;

void cpu_load_init(void)
{
    uint32_t t0;
    uint32_t t1;

    irq_start_cycles = 0U;
    last_irq_entry_cycles = 0U;
    current_period_cycles = 0U;
    cpu_permille = 0U;
    cpu_max_permille = 0U;
    cpu_counter_valid = 0U;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

#if defined(DWT_LAR)
    DWT->LAR = 0xC5ACCE55UL;
#endif

    __DSB();
    __ISB();

    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    __DSB();
    __ISB();

    t0 = DWT->CYCCNT;
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    t1 = DWT->CYCCNT;

    if(t1 != t0)
        cpu_counter_valid = 1U;
}

void cpu_load_irq_begin(void)
{
    uint32_t now;

    if(cpu_counter_valid == 0U)
        return;

    now = DWT->CYCCNT;
    current_period_cycles = now - last_irq_entry_cycles;
    last_irq_entry_cycles = now;
    irq_start_cycles = now;
}

void cpu_load_irq_end(void)
{
    uint32_t end;
    uint32_t elapsed;
    uint32_t pm;

    if(cpu_counter_valid == 0U)
        return;

    end = DWT->CYCCNT;
    elapsed = end - irq_start_cycles;

    if(current_period_cycles == 0U)
        return;

    pm = (uint32_t)(((uint64_t)elapsed * 1000ULL) /
                    (uint64_t)current_period_cycles);

    if(pm > CPU_LOAD_MAX_PERMILLE)
        pm = CPU_LOAD_MAX_PERMILLE;

    cpu_permille = pm;

    if(pm > cpu_max_permille)
        cpu_max_permille = pm;
}

uint32_t cpu_load_get_permille(void)
{
    return cpu_permille;
}

uint32_t cpu_load_get_max(void)
{
    return cpu_max_permille;
}

uint32_t cpu_load_is_valid(void)
{
    return cpu_counter_valid;
}
