#ifndef SIM_KEYMAP_H
#define SIM_KEYMAP_H
#include <SDL3/SDL.h>
#include <stddef.h>
typedef enum { SIM_KEY_NONE = 0, SIM_KEY_BUTTON, SIM_KEY_ENCODER, SIM_KEY_ENSEMBLE,
               SIM_KEY_CAPTURE } sim_key_kind_t;
typedef struct { SDL_Scancode scancode; sim_key_kind_t kind; unsigned id; int delta; } sim_key_binding_t;
typedef struct { sim_key_binding_t bindings[64]; size_t count; } sim_keymap_t;
int sim_keymap_load(sim_keymap_t *map, const char *path, char *error, size_t error_size);
const sim_key_binding_t *sim_keymap_find(const sim_keymap_t *map, SDL_Scancode scancode);
int sim_keymap_validate_default(const char *path, char *error, size_t error_size);
#endif
