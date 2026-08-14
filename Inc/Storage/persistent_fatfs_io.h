#ifndef PERSISTENT_FATFS_IO_H
#define PERSISTENT_FATFS_IO_H
#include "Storage/persistent_control_codec.h"
#include "ff.h"
typedef struct { FIL file; uint32_t size; } persistent_fatfs_file_t;
uint8_t persistent_fatfs_open_read(persistent_fatfs_file_t *file,const char *path);
uint8_t persistent_fatfs_open_write(persistent_fatfs_file_t *file,const char *path);
void persistent_fatfs_close(persistent_fatfs_file_t *file);
persist_codec_source_t persistent_fatfs_source(persistent_fatfs_file_t *file);
persist_codec_sink_t persistent_fatfs_sink(persistent_fatfs_file_t *file);
#endif
