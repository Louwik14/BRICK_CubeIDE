#ifndef SIM_PNG_H
#define SIM_PNG_H
#include <stddef.h>
#include <stdint.h>
int sim_png_write_framebuffer(const char *path, const uint8_t *framebuffer,
                              size_t framebuffer_size, char *error,
                              size_t error_size);
#endif

