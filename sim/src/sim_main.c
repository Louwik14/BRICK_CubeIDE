#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim_cli.h"
#include "sim_engine.h"
#include "sim_path.h"
#include "sim_png.h"
#include "sim_sdl.h"

static int capture_once(sim_engine_t *engine, const char *requested)
{
    char resolved[4096];
    char error[256];
    if (!sim_path_resolve_output(requested, resolved, sizeof(resolved),
                                 error, sizeof(error))
            || !sim_engine_render(engine)
            || !sim_png_write_framebuffer(resolved, sim_engine_framebuffer(engine),
                                          sim_engine_framebuffer_size(), error,
                                          sizeof(error)))
    {
        fprintf(stderr, "capture failed: %s\n", engine->error[0] ? engine->error : error);
        return 1;
    }
    fprintf(stderr, "captured 128x64 framebuffer: %s\n", resolved);
    return 0;
}

int main(int argc, char **argv)
{
    int stdio_mode = 0;
    int headless = 0;
    int scale = 8;
    unsigned frame_limit = 0U;
    const char *capture_path = NULL;
    const char *self_test_path = NULL;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--headless") == 0) headless = 1;
        else if (strcmp(argv[i], "--stdio") == 0) stdio_mode = 1;
        else if ((strcmp(argv[i], "--capture") == 0) && (i + 1 < argc)) capture_path = argv[++i];
        else if ((strcmp(argv[i], "--scale") == 0) && (i + 1 < argc)) scale = atoi(argv[++i]);
        else if ((strcmp(argv[i], "--frames") == 0) && (i + 1 < argc)) frame_limit = (unsigned)strtoul(argv[++i], NULL, 10);
        else if ((strcmp(argv[i], "--self-test") == 0) && (i + 1 < argc)) self_test_path = argv[++i];
        else
        {
            fprintf(stderr, "unknown or incomplete argument: %s\n", argv[i]);
            return 2;
        }
    }
    if ((scale < 1) || (scale > 16))
    {
        fprintf(stderr, "scale must be between 1 and 16\n");
        return 2;
    }

    sim_engine_t engine;
    if (!sim_engine_init(&engine))
    {
        fprintf(stderr, "simulator initialization failed: %s\n", engine.error);
        return 1;
    }
    if (self_test_path != NULL) return sim_cli_self_test(&engine, self_test_path);
    if (capture_path != NULL) return capture_once(&engine, capture_path);
    if (stdio_mode)
    {
        if (!headless)
        {
            fprintf(stderr, "pass 1 JSONL mode requires --headless --stdio\n");
            return 2;
        }
        return sim_cli_run(&engine);
    }
    if (headless)
    {
        fprintf(stderr, "--headless requires --stdio in pass 1\n");
        return 2;
    }
    return sim_sdl_run(&engine, scale, frame_limit);
}

