#ifndef SIM_JSON_H
#define SIM_JSON_H
#include <stddef.h>
#include <stdint.h>
int sim_json_get_string(const char *json, const char *key, char *out, size_t out_size);
int sim_json_get_u32(const char *json, const char *key, uint32_t *out);
int sim_json_get_i32(const char *json, const char *key, int32_t *out);
int sim_json_get_string_array(const char *json, const char *key,
                              char values[][64], size_t capacity, size_t *count);
int sim_json_get_id(const char *json, char *out, size_t out_size);
void sim_json_escape(const char *input, char *output, size_t output_size);
#endif
