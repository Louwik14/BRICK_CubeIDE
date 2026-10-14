#include "sim_sdl.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdint.h>
#include <stdio.h>

static int upload_frame(SDL_Texture *texture, const uint8_t *framebuffer)
{
    void *pixels = NULL;
    int pitch = 0;
    if (!SDL_LockTexture(texture, NULL, &pixels, &pitch)) return 0;
    for (unsigned y = 0U; y < SIM_FRAMEBUFFER_HEIGHT; ++y)
    {
        uint8_t *row = (uint8_t *)pixels + ((size_t)y * (size_t)pitch);
        for (unsigned x = 0U; x < SIM_FRAMEBUFFER_WIDTH; ++x)
        {
            const uint8_t packed = framebuffer[(y / 8U) * SIM_FRAMEBUFFER_WIDTH + x];
            const uint8_t value = ((packed >> (y & 7U)) & 1U) ? 255U : 0U;
            row[x * 4U + 0U] = value;
            row[x * 4U + 1U] = value;
            row[x * 4U + 2U] = value;
            row[x * 4U + 3U] = 255U;
        }
    }
    SDL_UnlockTexture(texture);
    return 1;
}

int sim_sdl_run(sim_engine_t *engine, int scale, unsigned frame_limit)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    int result = 1;
    int running = 1;
    unsigned frames = 0U;

    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    if (!SDL_CreateWindowAndRenderer("BRICK UI Simulator",
                                     (int)SIM_FRAMEBUFFER_WIDTH * scale,
                                     (int)SIM_FRAMEBUFFER_HEIGHT * scale,
                                     0, &window, &renderer))
    {
        fprintf(stderr, "SDL window creation failed: %s\n", SDL_GetError());
        goto cleanup;
    }
    if (!SDL_SetRenderLogicalPresentation(renderer, SIM_FRAMEBUFFER_WIDTH,
                                           SIM_FRAMEBUFFER_HEIGHT,
                                           SDL_LOGICAL_PRESENTATION_INTEGER_SCALE))
    {
        fprintf(stderr, "SDL logical presentation failed: %s\n", SDL_GetError());
        goto cleanup;
    }
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                SDL_TEXTUREACCESS_STREAMING,
                                SIM_FRAMEBUFFER_WIDTH, SIM_FRAMEBUFFER_HEIGHT);
    if ((texture == NULL) || !SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST))
    {
        fprintf(stderr, "SDL texture creation failed: %s\n", SDL_GetError());
        goto cleanup;
    }

    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT) running = 0;
        }
        if (!sim_engine_advance(engine, 16U)
                || !upload_frame(texture, sim_engine_framebuffer(engine)))
        {
            fprintf(stderr, "rendering failed: %s\n", engine->error);
            goto cleanup;
        }
        (void)SDL_SetRenderDrawColor(renderer, 0U, 0U, 0U, 255U);
        (void)SDL_RenderClear(renderer);
        if (!SDL_RenderTexture(renderer, texture, NULL, NULL)
                || !SDL_RenderPresent(renderer))
        {
            fprintf(stderr, "SDL presentation failed: %s\n", SDL_GetError());
            goto cleanup;
        }
        ++frames;
        if ((frame_limit > 0U) && (frames >= frame_limit)) running = 0;
        SDL_Delay(16U);
    }
    result = 0;

cleanup:
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
