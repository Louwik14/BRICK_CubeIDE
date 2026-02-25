#pragma once

#include <stddef.h>
#include <stdint.h>

#ifndef AUDIO_MEM_FAST_BYTES
#define AUDIO_MEM_FAST_BYTES (512u * 1024u)
#endif

#ifndef AUDIO_MEM_SLOW_BYTES
#define AUDIO_MEM_SLOW_BYTES (256u * 1024u)
#endif

void audio_mem_init(void);

void* audio_mem_alloc_fast(size_t size, size_t align);
void  audio_mem_free_fast(void* ptr);

void* audio_mem_alloc_slow(size_t size, size_t align);
void  audio_mem_free_slow(void* ptr);

size_t audio_mem_get_fast_used(void);
size_t audio_mem_get_fast_free(void);
size_t audio_mem_get_slow_used(void);
size_t audio_mem_get_slow_free(void);
