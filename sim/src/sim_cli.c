#include "sim_cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim_json.h"
#include "sim_path.h"
#include "sim_png.h"

static void response_prefix(FILE *output, const char *id)
{
    fputs("{", output);
    if ((id != NULL) && (*id != '\0')) fprintf(output, "\"id\":%s,", id);
}

static void response_error(FILE *output, const char *id,
                           const char *code, const char *message)
{
    char escaped[512];
    sim_json_escape(message, escaped, sizeof(escaped));
    response_prefix(output, id);
    fprintf(output, "\"ok\":false,\"error\":{\"code\":\"%s\",\"message\":\"%s\"}}\n",
            code, escaped);
    fflush(output);
}

static void response_inspect(FILE *output, const char *id,
                             const sim_engine_t *engine)
{
    char escaped_error[384];
    sim_json_escape(engine->error, escaped_error, sizeof(escaped_error));
    response_prefix(output, id);
    fprintf(output,
            "\"ok\":true,\"result\":{\"page\":\"%s\",\"page_id\":%u,"
            "\"renderer\":\"%s\",\"framebuffer\":{\"width\":128,"
            "\"height\":64,\"bytes\":1024},\"time_ms\":%llu,"
            "\"runtime_error\":%s%s%s}}\n",
            sim_engine_page_name(engine), (unsigned)sim_engine_page_id(engine),
            engine->renderer_ready ? "ready" : "busy",
            (unsigned long long)engine->time_ms,
            engine->error[0] ? "\"" : "null",
            engine->error[0] ? escaped_error : "",
            engine->error[0] ? "\"" : "");
    fflush(output);
}

static int dispatch(sim_engine_t *engine, const char *line, FILE *output,
                    int *should_quit)
{
    char id[128] = {0};
    char command[64];
    (void)sim_json_get_id(line, id, sizeof(id));
    if ((line == NULL) || (strchr(line, '{') == NULL)
            || !sim_json_get_string(line, "cmd", command, sizeof(command)))
    {
        response_error(output, id, "invalid_request",
                       "request must be a JSON object with a string cmd field");
        return 0;
    }

    if (strcmp(command, "discover") == 0)
    {
        response_prefix(output, id);
        fprintf(output,
                "\"ok\":true,\"result\":{\"protocol\":\"brick-ui-sim-jsonl-v1\","
                "\"commands\":[\"discover\",\"inspect\",\"advance\",\"capture\",\"quit\"],"
                "\"initial_page\":\"NAME_EDIT\",\"capabilities\":{"
                "\"sdl\":true,\"headless\":true,\"png\":true,"
                "\"navigation\":false,\"input\":false}}}\n");
        fflush(output);
        return 1;
    }
    if (strcmp(command, "inspect") == 0)
    {
        response_inspect(output, id, engine);
        return 1;
    }
    if (strcmp(command, "advance") == 0)
    {
        uint32_t milliseconds = 0U;
        if (!sim_json_get_u32(line, "ms", &milliseconds))
        {
            response_error(output, id, "invalid_argument",
                           "advance requires a non-negative integer ms field");
            return 0;
        }
        if (!sim_engine_advance(engine, milliseconds))
        {
            response_error(output, id, "runtime_error", engine->error);
            return 0;
        }
        response_prefix(output, id);
        fprintf(output, "\"ok\":true,\"result\":{\"time_ms\":%llu}}\n",
                (unsigned long long)engine->time_ms);
        fflush(output);
        return 1;
    }
    if (strcmp(command, "capture") == 0)
    {
        char requested[1024];
        char resolved[4096];
        char error[256];
        char escaped[4096];
        if (!sim_json_get_string(line, "path", requested, sizeof(requested)))
        {
            response_error(output, id, "invalid_argument",
                           "capture requires a string path field");
            return 0;
        }
        if (!sim_path_resolve_output(requested, resolved, sizeof(resolved),
                                     error, sizeof(error)))
        {
            response_error(output, id, "path_outside_sim", error);
            return 0;
        }
        if (!sim_engine_render(engine)
                || !sim_png_write_framebuffer(resolved,
                    sim_engine_framebuffer(engine), sim_engine_framebuffer_size(),
                    error, sizeof(error)))
        {
            response_error(output, id, "capture_failed",
                           engine->error[0] ? engine->error : error);
            return 0;
        }
        sim_json_escape(resolved, escaped, sizeof(escaped));
        response_prefix(output, id);
        fprintf(output,
                "\"ok\":true,\"result\":{\"path\":\"%s\",\"width\":128,\"height\":64}}\n",
                escaped);
        fflush(output);
        return 1;
    }
    if (strcmp(command, "quit") == 0)
    {
        response_prefix(output, id);
        fputs("\"ok\":true,\"result\":{\"quitting\":true}}\n", output);
        fflush(output);
        *should_quit = 1;
        return 1;
    }

    response_error(output, id, "unknown_command", "command is not available in pass 1");
    return 0;
}

int sim_cli_run(sim_engine_t *engine)
{
    char line[8192];
    int should_quit = 0;
    while (!should_quit && (fgets(line, sizeof(line), stdin) != NULL))
    {
        if (strchr(line, '\n') == NULL && !feof(stdin))
        {
            int c;
            while (((c = fgetc(stdin)) != '\n') && (c != EOF)) {}
            response_error(stdout, NULL, "request_too_large",
                           "JSON line exceeds 8191 bytes");
            continue;
        }
        (void)dispatch(engine, line, stdout, &should_quit);
    }
    return 0;
}

static int files_equal(const char *left, const char *right)
{
    FILE *a = fopen(left, "rb");
    FILE *b = fopen(right, "rb");
    int equal = (a != NULL) && (b != NULL);
    while (equal)
    {
        const int ca = fgetc(a);
        const int cb = fgetc(b);
        if (ca != cb) equal = 0;
        if ((ca == EOF) || (cb == EOF)) break;
    }
    if (a != NULL) fclose(a);
    if (b != NULL) fclose(b);
    return equal;
}

int sim_cli_self_test(sim_engine_t *engine, const char *output_dir)
{
    char first[2048];
    char second[2048];
    char line[4096];
    FILE *sink = tmpfile();
    int quit = 0;
    if (sink == NULL) return 1;
    (void)snprintf(first, sizeof(first), "%s/frame-a.png", output_dir);
    (void)snprintf(second, sizeof(second), "%s/frame-b.png", output_dir);

    const char *basic[] = {
        "{\"id\":1,\"cmd\":\"discover\"}",
        "{\"id\":2,\"cmd\":\"inspect\"}",
        "{\"id\":3,\"cmd\":\"advance\",\"ms\":100}",
        "{\"id\":4,\"cmd\":\"does-not-exist\"}"
    };
    for (size_t i = 0U; i < sizeof(basic) / sizeof(basic[0]); ++i)
    {
        (void)dispatch(engine, basic[i], sink, &quit);
    }
    (void)snprintf(line, sizeof(line),
                   "{\"id\":5,\"cmd\":\"capture\",\"path\":\"%s\"}", first);
    if (!dispatch(engine, line, sink, &quit)) goto fail;
    (void)snprintf(line, sizeof(line),
                   "{\"id\":6,\"cmd\":\"capture\",\"path\":\"%s\"}", second);
    if (!dispatch(engine, line, sink, &quit) || !files_equal(first, second)) goto fail;
    if (dispatch(engine, "{\"cmd\":\"capture\",\"path\":\"C:/forbidden.png\"}",
                 sink, &quit)) goto fail;
    if (!dispatch(engine, "{\"id\":7,\"cmd\":\"quit\"}", sink, &quit)
            || !quit) goto fail;
    fclose(sink);
    return 0;

fail:
    fclose(sink);
    return 1;
}
