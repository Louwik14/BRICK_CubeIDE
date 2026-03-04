#include "brick6_debug_uart.h"

#if BRICK6_STREAM_DEBUG

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "arm_acle.h"

#define DBG_UART_BUFFER_SIZE 4096U
#define DBG_LINE_TMP_SIZE    192U

static UART_HandleTypeDef *g_debug_uart = NULL;
static volatile uint8_t g_tx_busy = 0U;
static volatile uint8_t g_tx_kick_pending = 0U;
static volatile uint32_t g_head = 0U;
static volatile uint32_t g_tail = 0U;
static volatile uint32_t g_inflight_len = 0U;

static uint8_t g_ring[DBG_UART_BUFFER_SIZE];
static uint8_t g_dma_chunk[256];
static uint8_t g_use_dma_tx = 0U;
static uint32_t g_last_log_tick = 0U;

static inline uint8_t brick6_debug_in_isr(void)
{
    return (__get_IPSR() != 0U) ? 1U : 0U;
}

static void brick6_debug_kick_tx(void)
{
    if((g_debug_uart == NULL) || (g_tx_busy != 0U))
        return;

    uint32_t irq_state = __get_PRIMASK();
    __disable_irq();

    uint32_t available = (g_head >= g_tail)
                           ? (g_head - g_tail)
                           : (DBG_UART_BUFFER_SIZE - (g_tail - g_head));

    if(available == 0U)
    {
        if(!irq_state) __enable_irq();
        return;
    }

    if(available > sizeof(g_dma_chunk))
        available = sizeof(g_dma_chunk);

    uint32_t linear = DBG_UART_BUFFER_SIZE - g_tail;
    if(available > linear)
        available = linear;

    memcpy(g_dma_chunk, &g_ring[g_tail], available);
    g_tail = (g_tail + available) % DBG_UART_BUFFER_SIZE;
    g_inflight_len = available;
    g_tx_busy = 1U;

    if(!irq_state) __enable_irq();

    if(g_use_dma_tx != 0U)
    {
        if(HAL_UART_Transmit_DMA(g_debug_uart, g_dma_chunk, available) != HAL_OK)
        {
            irq_state = __get_PRIMASK();
            __disable_irq();
            g_tx_busy = 0U;
            g_inflight_len = 0U;
            if(!irq_state) __enable_irq();
        }
    }
    else
    {
        if(HAL_UART_Transmit(g_debug_uart, g_dma_chunk, available, 1U) == HAL_OK)
        {
            irq_state = __get_PRIMASK();
            __disable_irq();
            g_inflight_len = 0U;
            g_tx_busy = 0U;
            if(!irq_state) __enable_irq();
        }
        else
        {
            irq_state = __get_PRIMASK();
            __disable_irq();
            g_tx_busy = 0U;
            g_inflight_len = 0U;
            if(!irq_state) __enable_irq();
        }
    }
}

static void brick6_debug_enqueue_bytes(const uint8_t *data, uint32_t len)
{
    if((data == NULL) || (len == 0U))
        return;

    uint32_t irq_state = __get_PRIMASK();
    __disable_irq();

    for(uint32_t i = 0U; i < len; i++)
    {
        uint32_t next = (g_head + 1U) % DBG_UART_BUFFER_SIZE;
        if(next == g_tail)
            g_tail = (g_tail + 1U) % DBG_UART_BUFFER_SIZE;
        g_ring[g_head] = data[i];
        g_head = next;
    }

    if(!irq_state) __enable_irq();

    if(brick6_debug_in_isr())
    {
        g_tx_kick_pending = 1U;
    }
    else
    {
        brick6_debug_kick_tx();
    }
}

void brick6_debug_init(UART_HandleTypeDef *huart)
{
    g_debug_uart = huart;
    g_use_dma_tx = ((huart != NULL) && (huart->hdmatx != NULL)) ? 1U : 0U;
    g_tx_busy = 0U;
    g_tx_kick_pending = 0U;
    g_head = 0U;
    g_tail = 0U;
    g_inflight_len = 0U;
    g_last_log_tick = HAL_GetTick();
}

void brick6_debug_poll(void)
{
    if(g_tx_kick_pending != 0U)
    {
        g_tx_kick_pending = 0U;
        brick6_debug_kick_tx();
        return;
    }

    if(g_tx_busy == 0U)
    {
        brick6_debug_kick_tx();
    }
}

void brick6_debug_log(const char *fmt, ...)
{
    if((g_debug_uart == NULL) || (fmt == NULL))
        return;

    uint32_t now = HAL_GetTick();
    if((now - g_last_log_tick) < 2U)
        return;
    g_last_log_tick = now;

    char line[DBG_LINE_TMP_SIZE];
    va_list ap;
    va_start(ap, fmt);
    int written = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    if(written <= 0)
        return;

    uint32_t len = (written >= (int)sizeof(line)) ? (sizeof(line) - 1U) : (uint32_t)written;
    brick6_debug_enqueue_bytes((const uint8_t *)line, len);
}

void brick6_debug_rate_limited_log(const char *tag, uint32_t period_ms, const char *fmt, ...)
{
    static uint32_t last_tick[8] = {0};
    uint32_t slot = 0U;

    if((g_debug_uart == NULL) || (fmt == NULL) || (period_ms == 0U))
        return;

    if(tag != NULL)
    {
        const uint8_t *p = (const uint8_t *)tag;
        while(*p != 0U)
        {
            slot = (slot * 33U) ^ *p;
            p++;
        }
    }
    slot &= 0x7U;

    uint32_t now = HAL_GetTick();
    if((now - last_tick[slot]) < period_ms)
        return;
    last_tick[slot] = now;

    char line[DBG_LINE_TMP_SIZE];
    uint32_t prefix_len = 0U;
    if(tag != NULL)
    {
        int n = snprintf(line, sizeof(line), "[%s] ", tag);
        if(n > 0)
            prefix_len = (uint32_t)n < sizeof(line) ? (uint32_t)n : (sizeof(line) - 1U);
    }

    va_list ap;
    va_start(ap, fmt);
    int written = vsnprintf(&line[prefix_len], sizeof(line) - prefix_len, fmt, ap);
    va_end(ap);

    if(written <= 0)
        return;

    uint32_t len = prefix_len + (((uint32_t)written >= (sizeof(line) - prefix_len))
                                 ? ((sizeof(line) - prefix_len) - 1U)
                                 : (uint32_t)written);
    brick6_debug_enqueue_bytes((const uint8_t *)line, len);
}

void brick6_debug_hex(const void *data, uint32_t len)
{
    if((data == NULL) || (len == 0U))
        return;

    const uint8_t *bytes = (const uint8_t *)data;
    brick6_debug_log("HEX ");

    for(uint32_t i = 0U; i < len; i++)
    {
        char s[4];
        (void)snprintf(s, sizeof(s), "%02X", bytes[i]);
        brick6_debug_enqueue_bytes((const uint8_t *)s, 2U);
        if(i + 1U < len)
            brick6_debug_enqueue_bytes((const uint8_t *)" ", 1U);
    }

    brick6_debug_enqueue_bytes((const uint8_t *)"\r\n", 2U);
}

void brick6_debug_dump_audio(float *buf, uint32_t frames)
{
    if((buf == NULL) || (frames == 0U))
        return;

    uint32_t n = (frames > 256U) ? 256U : frames;
    brick6_debug_log("AUDIO_DUMP_BEGIN,%lu\r\n", (unsigned long)n);
    for(uint32_t i = 0U; i < n; i++)
    {
        brick6_debug_log("%lu,%.6f\r\n", (unsigned long)i, (double)buf[i]);
    }
    brick6_debug_log("AUDIO_DUMP_END\r\n");
}

void brick6_debug_uart_tx_complete(UART_HandleTypeDef *huart)
{
    if(huart != g_debug_uart)
        return;

    uint32_t irq_state = __get_PRIMASK();
    __disable_irq();
    g_inflight_len = 0U;
    g_tx_busy = 0U;
    if(!irq_state) __enable_irq();

    brick6_debug_kick_tx();
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    brick6_debug_uart_tx_complete(huart);
}

#endif
