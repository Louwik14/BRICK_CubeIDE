#include "sim_keymap.h"
#include <stdio.h>
#include <string.h>
#include "sim_engine.h"

int sim_keymap_load(sim_keymap_t *map, const char *path, char *error, size_t error_size)
{
    FILE *file = fopen(path, "rb"); char line[256];
    if (!map || !file) { if (error) snprintf(error, error_size, "cannot open keymap: %s", path); return 0; }
    memset(map, 0, sizeof(*map));
    while (fgets(line, sizeof(line), file))
    {
        char key[64], action[64];
        if (sscanf(line, " \"%63[^\"]\" : \"%63[^\"]\"", key, action) != 2) continue;
        if (map->count >= sizeof(map->bindings) / sizeof(map->bindings[0])) { if (error) snprintf(error, error_size, "too many keymap entries"); fclose(file); return 0; }
        sim_key_binding_t binding = {0}; unsigned id;
        binding.scancode = SDL_GetScancodeFromName(key);
        if (binding.scancode == SDL_SCANCODE_UNKNOWN) { if (error) snprintf(error, error_size, "unknown SDL scancode: %s", key); fclose(file); return 0; }
        if (sim_engine_button_from_name(action, &id)) { binding.kind = SIM_KEY_BUTTON; binding.id = id; }
        else if (strncmp(action, "ROTATE_", 7U) == 0)
        {
            char encoder[32], sign = 0;
            if (sscanf(action + 7, "%31[^:]:%c", encoder, &sign) != 2 || !sim_engine_encoder_from_name(encoder, &id)) { if (error) snprintf(error, error_size, "invalid encoder action: %s", action); fclose(file); return 0; }
            binding.kind = SIM_KEY_ENCODER; binding.id = id; binding.delta = sign == '-' ? -1 : 1;
        }
        else if (strcmp(action, "SIM_CAPTURE") == 0) binding.kind = SIM_KEY_CAPTURE;
        else { if (error) snprintf(error, error_size, "unknown keymap action: %s", action); fclose(file); return 0; }
        map->bindings[map->count++] = binding;
    }
    fclose(file);
    if (map->count == 0U && error) snprintf(error, error_size, "keymap contains no bindings");
    return map->count > 0U;
}
const sim_key_binding_t *sim_keymap_find(const sim_keymap_t *map, SDL_Scancode scancode)
{
    if (!map) return NULL;
    for (size_t i = 0U; i < map->count; ++i) if (map->bindings[i].scancode == scancode) return &map->bindings[i];
    return NULL;
}
