#ifndef PROJECT_LOAD_QUIESCE_H
#define PROJECT_LOAD_QUIESCE_H

#include <stdint.h>

void project_load_quiesce_init(void);
void project_load_quiesce_request(void);
uint8_t project_load_quiesce_audio_service(void);
uint8_t project_load_quiesce_safe(void);
void project_load_quiesce_end(void);

#endif
