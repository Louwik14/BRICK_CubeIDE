#ifndef BRICK_STREAM_REC_PERF_H
#define BRICK_STREAM_REC_PERF_H

#include <stddef.h>
#include <stdint.h>
#include "stm32h7xx.h"

#ifndef BRICK_PERF_DIAG
#define BRICK_PERF_DIAG 0
#endif

typedef struct { uint32_t calls; uint32_t max; uint64_t total; } brick_perf_span_t;

enum {
    PERF_CPU_AUDIO_TOTAL, PERF_CPU_AUDIO_CONVERT, PERF_CPU_AUDIO_PEAK,
    PERF_CPU_AUDIO_RING,
    PERF_CPU_READER_NEED, PERF_CPU_READER_LEASE, PERF_CPU_READER_RESOLVE,
    PERF_CPU_MANAGER_PICK, PERF_CPU_MANAGER_FINISH,
    PERF_CPU_CACHE_RESERVE, PERF_CPU_CACHE_RECYCLE,
    PERF_CPU_STREAM_COMMAND, PERF_CPU_STREAM_SUBMIT,
    PERF_CPU_STREAM_IO_BEGIN, PERF_CPU_STREAM_IO_FINALIZE,
    PERF_CPU_STREAM_READ_START, PERF_CPU_STREAM_READ_COMPLETE,
    PERF_CPU_STREAM_DMA_LAUNCH, PERF_CPU_REC_DMA_LAUNCH,
    PERF_CPU_REC_SOURCE_SCRATCH, PERF_CPU_REC_SOURCE_CONVERT,
    PERF_CPU_REC_PACK, PERF_CPU_REC_PREPARE, PERF_CPU_REC_SERVICE,
    PERF_CPU_REC_WRITE_START, PERF_CPU_REC_WRITE_COMPLETE,
    PERF_CPU_COUNT
};
enum {
    PERF_WALL_STREAM_REQUEST_READY, PERF_WALL_STREAM_SUBMIT_DMA,
    PERF_WALL_STREAM_DMA, PERF_WALL_STREAM_DMA_IO_FINALIZE,
    PERF_WALL_REC_WRITE, PERF_WALL_REC_SUBMIT_DMA, PERF_WALL_COUNT
};
enum {
    PERF_N_PAGES_REQUESTED, PERF_N_PAGES_READY,
    PERF_N_CLASSIC_READY, PERF_N_MULTI_READY, PERF_N_REC_READY,
    PERF_N_CACHE_READY, PERF_N_CACHE_HIT,
    PERF_N_CACHE_LOADING, PERF_N_CACHE_MISS, PERF_N_CACHE_ALLOC,
    PERF_N_CACHE_RECYCLE, PERF_N_CACHE_PROTECTED, PERF_N_CACHE_ALLOC_FAIL,
    PERF_N_PAGE_FAILED, PERF_N_LEASE_PUBLISH, PERF_N_LEASE_CHECK,
    PERF_N_AUDIO_PAGE_MISSING, PERF_N_READS, PERF_N_READ_BYTES,
    PERF_N_READ_MIN_BYTES, PERF_N_READ_MAX_BYTES, PERF_N_REC_FRAMES,
    PERF_N_AUDIO_CONVERT_FRAMES,
    PERF_N_REC_PCM_BYTES, PERF_N_REC_WRITE_BYTES, PERF_N_REC_WRITES,
    PERF_N_REC_WRITE_MIN_BYTES, PERF_N_REC_WRITE_MAX_BYTES,
    PERF_N_REC_RING_FILL, PERF_N_REC_RING_MAX, PERF_N_REC_RING_MIN_FREE,
    PERF_N_REC_RING_NEAR_FULL, PERF_N_REC_OVERFLOW,
    PERF_N_REC_SOURCE_PAGES, PERF_N_REC_SOURCE_FRAMES,
    PERF_N_TEST_ELAPSED_MS,
    PERF_N_COUNT
};

typedef struct {
    uint32_t magic, version, size, cpu_hz;
    brick_perf_span_t cpu[PERF_CPU_COUNT];
    brick_perf_span_t wall[PERF_WALL_COUNT];
    uint64_t count[PERF_N_COUNT];
} brick_stream_rec_perf_t;

_Static_assert(sizeof(brick_perf_span_t) == 16, "perf span ABI");
_Static_assert(offsetof(brick_stream_rec_perf_t, cpu) == 16, "perf header ABI");
_Static_assert(offsetof(brick_stream_rec_perf_t, count) == 16 + 16 * (PERF_CPU_COUNT + PERF_WALL_COUNT), "perf count ABI");
_Static_assert((sizeof(brick_stream_rec_perf_t) % 4) == 0, "word dump ABI");
_Static_assert(sizeof(brick_stream_rec_perf_t) == 816, "perf v3 size ABI");
_Static_assert(_Alignof(brick_stream_rec_perf_t) >= 8, "perf 64-bit alignment ABI");

#if BRICK_PERF_DIAG
extern volatile brick_stream_rec_perf_t g_stream_rec_perf;
void brick_perf_diag_reset(void);
static inline uint32_t brick_perf_now(void) { return DWT->CYCCNT; }
static inline void brick_perf_add(unsigned id, uint32_t delta)
{
    volatile brick_perf_span_t *s = &g_stream_rec_perf.cpu[id];
    s->calls++;
    s->total += delta;
    if (delta > s->max) s->max = delta;
}
static inline void brick_perf_wall(unsigned id, uint32_t delta)
{
    volatile brick_perf_span_t *s = &g_stream_rec_perf.wall[id];
    s->calls++;
    s->total += delta;
    if (delta > s->max) s->max = delta;
}
#define PERF_START(name) uint32_t name = brick_perf_now()
#define PERF_END(id, name) brick_perf_add((id), brick_perf_now() - (name))
#define PERF_COUNT(id) (g_stream_rec_perf.count[(id)]++)
#define PERF_ACCUM(id, value) (g_stream_rec_perf.count[(id)] += (uint64_t)(value))
#define PERF_SET(id, value) (g_stream_rec_perf.count[(id)] = (value))
#define PERF_MAX(id, value) do { uint64_t perf_value_ = (value); \
    if (perf_value_ > g_stream_rec_perf.count[(id)]) \
        g_stream_rec_perf.count[(id)] = perf_value_; } while (0)
#define PERF_MIN_NONZERO(id, value) do { uint64_t perf_value_ = (value); \
    if ((g_stream_rec_perf.count[(id)] == 0U) \
        || (perf_value_ < g_stream_rec_perf.count[(id)])) \
        g_stream_rec_perf.count[(id)] = perf_value_; } while (0)
#else
static inline uint32_t brick_perf_now(void) { return 0; }
static inline void brick_perf_add(unsigned id, uint32_t delta) { (void)id; (void)delta; }
static inline void brick_perf_wall(unsigned id, uint32_t delta) { (void)id; (void)delta; }
#define PERF_START(name) ((void)0)
#define PERF_END(id, name) ((void)0)
#define PERF_COUNT(id) ((void)0)
#define PERF_ACCUM(id, value) ((void)0)
#define PERF_SET(id, value) ((void)0)
#define PERF_MAX(id, value) ((void)0)
#define PERF_MIN_NONZERO(id, value) ((void)0)
#endif
#endif
