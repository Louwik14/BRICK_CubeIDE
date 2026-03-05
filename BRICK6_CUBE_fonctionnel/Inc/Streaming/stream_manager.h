#pragma once

#include <stdbool.h>

bool stream_manager_start(const char *path);
void stream_manager_process(void);
void stream_manager_get_frame(float *L, float *R);
