#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "wav_parser.h"

#if defined(__has_include)
#  if __has_include("ff.h")
#    include "ff.h"
#    define WAV_LOADER_HAS_FATFS 1
#  endif
#endif

#ifndef WAV_LOADER_HAS_FATFS
#define WAV_LOADER_HAS_FATFS 0
#endif

#define WAV_LOADER_CATALOG_MAX (9999U)
#define WAV_LOADER_CATALOG_VIEW_MAX (256U)
#define WAV_LOADER_CATALOG_ROOT_PARENT (0xFFFFU)
#define WAV_LOADER_CATALOG_PATH_MAX (160U)
#define WAV_LOADER_CATALOG_NAME_MAX (48U)

typedef enum
{
    WAV_LOADER_CATALOG_INVALID = 0,
    WAV_LOADER_CATALOG_READY
} wav_loader_catalog_state_t;

typedef enum
{
    WAV_LOADER_CATALOG_VIEW_PENDING = 0,
    WAV_LOADER_CATALOG_VIEW_PUBLISHED,
    WAV_LOADER_CATALOG_VIEW_ERROR
} wav_loader_catalog_view_service_result_t;

typedef enum
{
    WAV_LOADER_CATALOG_ENTRY_FILE = 0,
    WAV_LOADER_CATALOG_ENTRY_DIR
} wav_loader_catalog_entry_type_t;

typedef struct
{
    char path[WAV_LOADER_CATALOG_PATH_MAX];
    char name[WAV_LOADER_CATALOG_NAME_MAX];
    uint32_t size;
    uint16_t parent_id;
    uint16_t date;
    uint16_t time;
    wav_loader_catalog_entry_type_t type;
    wav_loader_catalog_state_t state;
} wav_loader_catalog_entry_t;

bool wav_loader_find_first_wav(char *out_path, uint32_t max_len);
void wav_loader_catalog_init_load(void);
void wav_loader_catalog_refresh(void);
void wav_loader_catalog_rebuild(void);
uint8_t wav_loader_catalog_notify_file_created(const char *path);
uint16_t wav_loader_catalog_count(void);
uint16_t wav_loader_catalog_child_count(uint16_t parent_id);
uint8_t wav_loader_catalog_last_sd_busy(void);
uint8_t wav_loader_catalog_last_io_error(void);
uint16_t wav_loader_catalog_get_child_index(uint16_t parent_id, uint16_t child_index);
uint8_t wav_loader_catalog_truncated(void);
uint8_t wav_loader_catalog_path_truncated(void);
uint8_t wav_loader_catalog_loaded(void);
uint8_t wav_loader_catalog_stale(void);
void wav_loader_catalog_mark_stale(void);
wav_loader_catalog_view_service_result_t wav_loader_catalog_view_service(void);
uint8_t wav_loader_catalog_view_busy(void);
uint8_t wav_loader_catalog_find_path(const char *path, uint16_t *out_index, wav_loader_catalog_entry_t *out_entry);
const wav_loader_catalog_entry_t *wav_loader_catalog_get(uint16_t index);
const wav_loader_catalog_entry_t *wav_loader_catalog_get_child(uint16_t parent_id, uint16_t child_index);
