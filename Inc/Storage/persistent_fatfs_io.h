#ifndef PERSISTENT_FATFS_IO_H
#define PERSISTENT_FATFS_IO_H
#include "Storage/persistent_control_codec.h"
#include "ff.h"
typedef struct { FIL file; uint32_t size; FRESULT last_result; uint32_t requested; uint32_t transferred; } persistent_fatfs_file_t;
uint8_t persistent_fatfs_open_read(persistent_fatfs_file_t *file,const char *path);
uint8_t persistent_fatfs_open_write(persistent_fatfs_file_t *file,const char *path);
FRESULT persistent_fatfs_open_write_result(persistent_fatfs_file_t *file,const char *path);
void persistent_fatfs_close(persistent_fatfs_file_t *file);
FRESULT persistent_fatfs_close_result(persistent_fatfs_file_t *file);
FRESULT persistent_fatfs_recover_replace(const char *final_path,
                                         const char *temporary_path,
                                         const char *backup_path);
FRESULT persistent_fatfs_commit_replace(const char *final_path,
                                        const char *temporary_path,
                                        const char *backup_path);
persist_codec_source_t persistent_fatfs_source(persistent_fatfs_file_t *file);
persist_codec_sink_t persistent_fatfs_sink(persistent_fatfs_file_t *file);
#endif
