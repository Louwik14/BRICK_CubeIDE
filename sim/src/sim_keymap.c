#include "sim_keymap.h"
#include <stdio.h>
#include <string.h>
#include "sim_engine.h"
#include "buttons_ids.h"

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
        if (sim_keymap_find(map, binding.scancode) != NULL) { if (error) snprintf(error, error_size, "duplicate SDL scancode: %s", key); fclose(file); return 0; }
        if (sim_engine_button_from_name(action, &id)) { binding.kind = SIM_KEY_BUTTON; binding.id = id; }
        else if (strncmp(action, "ROTATE_", 7U) == 0)
        {
            char encoder[32], sign = 0;
            if (sscanf(action + 7, "%31[^:]:%c", encoder, &sign) != 2 || !sim_engine_encoder_from_name(encoder, &id)) { if (error) snprintf(error, error_size, "invalid encoder action: %s", action); fclose(file); return 0; }
            binding.kind = SIM_KEY_ENCODER; binding.id = id; binding.delta = sign == '-' ? -1 : 1;
        }
        else if (strncmp(action, "ENSEMBLE_", 9U) == 0)
        {
            static const char *const names[] = { "CFG", "ENV", "TONE", "MOD", "MIX", "PLAY", "MIDI_FX", "AUDIO_FX" };
            for (id = 0U; id < SIM_ENSEMBLE_COUNT && strcmp(action + 9, names[id]) != 0; ++id) {}
            if (id >= SIM_ENSEMBLE_COUNT) { if (error) snprintf(error, error_size, "invalid ensemble action: %s", action); fclose(file); return 0; }
            binding.kind = SIM_KEY_ENSEMBLE; binding.id = id;
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

static int expect(const sim_keymap_t *map, SDL_Scancode code,
                  sim_key_kind_t kind, unsigned id, int delta)
{
    const sim_key_binding_t *binding = sim_keymap_find(map, code);
    return binding != NULL && binding->kind == kind && binding->id == id
        && (kind != SIM_KEY_ENCODER || binding->delta == delta);
}

int sim_keymap_validate_default(const char *path, char *error, size_t error_size)
{
    sim_keymap_t map;
    if (!sim_keymap_load(&map, path, error, error_size)) return 0;
    const int valid = map.count == 42U
        && expect(&map, SDL_SCANCODE_F1, SIM_KEY_BUTTON, BTN_STEP_1, 0)
        && expect(&map, SDL_SCANCODE_F8, SIM_KEY_BUTTON, BTN_STEP_8, 0)
        && expect(&map, SDL_SCANCODE_1, SIM_KEY_BUTTON, BTN_STEP_9, 0)
        && expect(&map, SDL_SCANCODE_8, SIM_KEY_BUTTON, BTN_STEP_16, 0)
        && expect(&map, SDL_SCANCODE_Q, SIM_KEY_ENSEMBLE, SIM_ENSEMBLE_CFG, 0)
        && expect(&map, SDL_SCANCODE_W, SIM_KEY_ENSEMBLE, SIM_ENSEMBLE_ENV, 0)
        && expect(&map, SDL_SCANCODE_A, SIM_KEY_BUTTON, BTN_PAGE_1, 0)
        && expect(&map, SDL_SCANCODE_KP_1, SIM_KEY_ENCODER, SIM_ENCODER_PAGE, -1)
        && expect(&map, SDL_SCANCODE_KP_3, SIM_KEY_ENCODER, SIM_ENCODER_PAGE, 1)
        && expect(&map, SDL_SCANCODE_LEFT, SIM_KEY_ENCODER, SIM_ENCODER_B, -1)
        && expect(&map, SDL_SCANCODE_UP, SIM_KEY_ENCODER, SIM_ENCODER_C, 1)
        && expect(&map, SDL_SCANCODE_LCTRL, SIM_KEY_BUTTON, BTN_TRACK, 0)
        && expect(&map, SDL_SCANCODE_F12, SIM_KEY_CAPTURE, 0U, 0);
    if (!valid && error != NULL) snprintf(error, error_size, "default AZERTY keymap does not match its contract");
    return valid;
}
