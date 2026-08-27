#include "Core/audio_retire_ack.h"

#include "Storage/memory_layout.h"
#include "stm32h7xx.h"

#define AUDIO_RETIRE_ACK_CAPACITY (32U)

typedef struct
{
    audio_retire_ack_t entries[AUDIO_RETIRE_ACK_CAPACITY];
    volatile uint8_t head;
    volatile uint8_t tail;
} audio_retire_ack_ring_t;

D3_IPC static audio_retire_ack_ring_t g_audio_retire_ack_ring;

void audio_retire_ack_init(void)
{
    g_audio_retire_ack_ring.head = 0U;
    g_audio_retire_ack_ring.tail = 0U;
    __DMB();
}

uint8_t audio_retire_ack_publish(audio_retire_ack_kind_t kind,
                                 uint16_t slot,
                                 uint32_t generation)
{
    const uint8_t head = g_audio_retire_ack_ring.head;
    const uint8_t next = (uint8_t)((head + 1U) % AUDIO_RETIRE_ACK_CAPACITY);
    if (next == g_audio_retire_ack_ring.tail) return 0U;
    g_audio_retire_ack_ring.entries[head].kind = (uint8_t)kind;
    g_audio_retire_ack_ring.entries[head].reserved = 0U;
    g_audio_retire_ack_ring.entries[head].slot = slot;
    g_audio_retire_ack_ring.entries[head].generation = generation;
    __DMB();
    g_audio_retire_ack_ring.head = next;
    return 1U;
}

uint8_t audio_retire_ack_drain(audio_retire_ack_t *out_ack,
                               uint8_t capacity)
{
    if ((out_ack == 0) || (capacity == 0U)) return 0U;
    uint8_t count = 0U;
    uint8_t tail = g_audio_retire_ack_ring.tail;
    const uint8_t head = g_audio_retire_ack_ring.head;
    __DMB();
    while ((tail != head) && (count < capacity))
    {
        out_ack[count++] = g_audio_retire_ack_ring.entries[tail];
        tail = (uint8_t)((tail + 1U) % AUDIO_RETIRE_ACK_CAPACITY);
    }
    __DMB();
    g_audio_retire_ack_ring.tail = tail;
    return count;
}
