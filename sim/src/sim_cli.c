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
    uint8_t subpage = 0U;
    const int has_subpage = sim_engine_subpage(engine, &subpage);
    const uint64_t held = sim_engine_held_mask();
    sim_json_escape(engine->error, escaped_error, sizeof(escaped_error));
    response_prefix(output, id);
    fprintf(output,
            "\"ok\":true,\"result\":{\"page\":\"%s\",\"page_id\":%u,"
            "\"renderer\":\"%s\",\"framebuffer\":{\"width\":128,"
            "\"height\":64,\"bytes\":1024},\"time_ms\":%llu,"
            "\"runtime_error\":%s%s%s,\"subpage\":",
            sim_engine_page_name(engine), (unsigned)sim_engine_page_id(engine),
            engine->renderer_ready ? "ready" : "busy",
            (unsigned long long)engine->time_ms,
            engine->error[0] ? "\"" : "null",
            engine->error[0] ? escaped_error : "",
            engine->error[0] ? "\"" : "");
    if (has_subpage) fprintf(output, "%u", (unsigned)subpage); else fputs("null", output);
    fprintf(output, ",\"active_track\":%u,\"engine\":{\"available\":false},"
            "\"focus\":{\"available\":false},\"selected_parameters\":{\"available\":false},"
            "\"overlay\":{\"available\":false},\"transport\":{\"running\":%s,\"tempo_bpm_milli\":%u},"
            "\"held_buttons\":[", (unsigned)sim_engine_active_track(engine),
            sim_engine_transport_running(engine) ? "true" : "false",
            (unsigned)sim_engine_tempo_milli(engine));
    unsigned emitted = 0U;
    for (unsigned i = 0U; i < 44U; ++i)
        if ((held & (UINT64_C(1) << i)) != 0U)
            fprintf(output, "%s\"%s\"", emitted++ ? "," : "", sim_engine_button_name(i));
    fputs("]}}\n", output);
    fflush(output);
}

static int require_button(const char *line, unsigned *button, char *message, size_t size)
{
    char name[64];
    if (!sim_json_get_string(line, "button", name, sizeof(name)))
    { (void)snprintf(message, size, "command requires a string button field"); return 0; }
    if (!sim_engine_button_from_name(name, button))
    { (void)snprintf(message, size, "unknown button: %s", name); return 0; }
    return 1;
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
                "\"commands\":[\"discover\",\"inspect\",\"advance\",\"press\",\"release\","
                "\"tap\",\"hold\",\"combo\",\"double_tap\",\"rotate\",\"push_encoder\","
                "\"wait_until\",\"capture\",\"quit\"],\"buttons\":[");
        for (unsigned i = 0U; i < 44U; ++i)
            fprintf(output, "%s\"%s\"", i ? "," : "", sim_engine_button_name(i));
        fputs("],\"encoders\":[\"PAGE\",\"A\",\"B\",\"C\"],"
              "\"pages\":[\"CFG\",\"ENV\",\"TONE\",\"MOD\",\"MIX\",\"PLAY\",\"SEQ\","
              "\"KEYBOARD\",\"MIDI_FX\",\"AUDIO_FX\",\"SETTINGS\"],"
              "\"inspection_fields\":[\"page\",\"page_id\",\"subpage\",\"active_track\","
              "\"renderer\",\"framebuffer\",\"time_ms\",\"held_buttons\",\"transport\",\"runtime_error\"],"
              "\"initial_page\":\"CFG\",\"capabilities\":{\"sdl\":true,\"headless\":true,"
              "\"png\":true,\"navigation\":true,\"input\":true,\"virtual_time\":true}}}\n", output);
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
    if ((strcmp(command, "press") == 0) || (strcmp(command, "release") == 0)
            || (strcmp(command, "tap") == 0) || (strcmp(command, "hold") == 0)
            || (strcmp(command, "double_tap") == 0))
    {
        unsigned button;
        uint32_t duration = strcmp(command, "hold") == 0 ? 700U : 40U;
        char message[128];
        if (!require_button(line, &button, message, sizeof(message)))
        { response_error(output, id, "invalid_argument", message); return 0; }
        (void)sim_json_get_u32(line, "duration_ms", &duration);
        int ok;
        if (strcmp(command, "press") == 0) ok = sim_engine_press(engine, button);
        else if (strcmp(command, "release") == 0) ok = sim_engine_release(engine, button);
        else if (strcmp(command, "double_tap") == 0)
        {
            uint32_t interval = 80U;
            (void)sim_json_get_u32(line, "interval_ms", &interval);
            ok = sim_engine_tap(engine, button, duration)
                && sim_engine_advance(engine, interval)
                && sim_engine_tap(engine, button, duration);
        }
        else ok = sim_engine_tap(engine, button, duration);
        if (!ok) { response_error(output, id, "runtime_error", engine->error); return 0; }
        response_prefix(output, id);
        fprintf(output, "\"ok\":true,\"result\":{\"button\":\"%s\",\"time_ms\":%llu}}\n",
                sim_engine_button_name(button), (unsigned long long)engine->time_ms);
        fflush(output); return 1;
    }
    if (strcmp(command, "combo") == 0)
    {
        char names[16][64]; size_t count = 0U; unsigned buttons[16]; uint32_t duration = 40U;
        if (!sim_json_get_string_array(line, "buttons", names, 16U, &count) || count < 2U)
        { response_error(output, id, "invalid_argument", "combo requires at least two button names"); return 0; }
        for (size_t i = 0U; i < count; ++i)
            if (!sim_engine_button_from_name(names[i], &buttons[i]))
            { response_error(output, id, "invalid_argument", "combo contains an unknown button"); return 0; }
        (void)sim_json_get_u32(line, "duration_ms", &duration);
        if (!sim_engine_combo(engine, buttons, count, duration))
        { response_error(output, id, "runtime_error", engine->error); return 0; }
        response_prefix(output, id);
        fprintf(output, "\"ok\":true,\"result\":{\"count\":%u,\"time_ms\":%llu}}\n",
                (unsigned)count, (unsigned long long)engine->time_ms);
        fflush(output); return 1;
    }
    if ((strcmp(command, "rotate") == 0) || (strcmp(command, "push_encoder") == 0))
    {
        char name[32]; unsigned encoder; int ok;
        if (!sim_json_get_string(line, "encoder", name, sizeof(name))
                || !sim_engine_encoder_from_name(name, &encoder))
        { response_error(output, id, "invalid_argument", "unknown or missing encoder (PAGE, A, B, C)"); return 0; }
        if (strcmp(command, "rotate") == 0)
        {
            int32_t delta;
            if (!sim_json_get_i32(line, "delta", &delta) || delta == 0)
            { response_error(output, id, "invalid_argument", "rotate requires a non-zero integer delta"); return 0; }
            ok = sim_engine_rotate(engine, encoder, delta);
        }
        else
        {
            uint32_t duration = 40U; (void)sim_json_get_u32(line, "duration_ms", &duration);
            ok = sim_engine_tap(engine, 20U + encoder, duration);
        }
        if (!ok) { response_error(output, id, "runtime_error", engine->error); return 0; }
        response_prefix(output, id);
        fprintf(output, "\"ok\":true,\"result\":{\"encoder\":\"%s\",\"time_ms\":%llu}}\n",
                sim_engine_encoder_name(encoder), (unsigned long long)engine->time_ms);
        fflush(output); return 1;
    }
    if (strcmp(command, "wait_until") == 0)
    {
        char page[64] = {0}; uint32_t timeout = 0U, wanted = 0U;
        const int need_subpage = sim_json_get_u32(line, "subpage", &wanted);
        (void)sim_json_get_string(line, "page", page, sizeof(page));
        if (!sim_json_get_u32(line, "timeout_ms", &timeout) || (page[0] == '\0' && !need_subpage))
        { response_error(output, id, "invalid_argument", "wait_until requires page and/or subpage plus timeout_ms"); return 0; }
        uint32_t waited = 0U; int matched = 0;
        for (;;)
        {
            uint8_t actual = 0U;
            matched = ((page[0] == '\0') || strcmp(page, sim_engine_page_name(engine)) == 0)
                && (!need_subpage || (sim_engine_subpage(engine, &actual) && actual == wanted));
            if (matched || waited >= timeout) break;
            if (!sim_engine_advance(engine, 1U)) break;
            ++waited;
        }
        if (!matched) { response_error(output, id, "timeout", "UI state was not reached before timeout"); return 0; }
        response_prefix(output, id);
        fprintf(output, "\"ok\":true,\"result\":{\"matched\":true,\"waited_ms\":%u}}\n", waited);
        fflush(output); return 1;
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

    response_error(output, id, "unknown_command", "command is not available");
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
    if (strcmp(sim_engine_page_name(engine), "CFG") != 0) goto fail;
    if (!dispatch(engine, "{\"cmd\":\"tap\",\"button\":\"PARAM_2\"}", sink, &quit)
            || strcmp(sim_engine_page_name(engine), "TONE") != 0) goto fail;
    if (!dispatch(engine, "{\"cmd\":\"tap\",\"button\":\"PAGE_2\"}", sink, &quit)) goto fail;
    uint8_t subpage = 0U;
    if (!sim_engine_subpage(engine, &subpage) || subpage != 1U) goto fail;
    if (!dispatch(engine, "{\"cmd\":\"rotate\",\"encoder\":\"A\",\"delta\":2}", sink, &quit)
            || !dispatch(engine, "{\"cmd\":\"rotate\",\"encoder\":\"A\",\"delta\":-2}", sink, &quit)) goto fail;
    if (!dispatch(engine, "{\"cmd\":\"press\",\"button\":\"SHIFT\"}", sink, &quit)
            || sim_engine_held_mask() == 0U
            || !dispatch(engine, "{\"cmd\":\"release\",\"button\":\"SHIFT\"}", sink, &quit)
            || sim_engine_held_mask() != 0U) goto fail;
    if (!dispatch(engine, "{\"cmd\":\"hold\",\"button\":\"COPY\",\"duration_ms\":700}", sink, &quit)
            || !dispatch(engine, "{\"cmd\":\"combo\",\"buttons\":[\"SHIFT\",\"COPY\"]}", sink, &quit)
            || !dispatch(engine, "{\"cmd\":\"double_tap\",\"button\":\"PLAY\",\"interval_ms\":80}", sink, &quit)) goto fail;
    const char *encoder_names[] = {"PAGE", "A", "B", "C"};
    for (size_t i = 0U; i < 4U; ++i)
    {
        (void)snprintf(line, sizeof(line), "{\"cmd\":\"push_encoder\",\"encoder\":\"%s\"}", encoder_names[i]);
        if (!dispatch(engine, line, sink, &quit)) goto fail;
    }
    if (!dispatch(engine, "{\"cmd\":\"tap\",\"button\":\"SETTINGS\"}", sink, &quit)
            || strcmp(sim_engine_page_name(engine), "SETTINGS") != 0
            || !dispatch(engine, "{\"cmd\":\"tap\",\"button\":\"PAGE_1\"}", sink, &quit)
            || strcmp(sim_engine_page_name(engine), "TONE") != 0) goto fail;
    if (!dispatch(engine, "{\"cmd\":\"wait_until\",\"page\":\"TONE\",\"timeout_ms\":2}", sink, &quit)) goto fail;
    if (dispatch(engine, "{\"cmd\":\"wait_until\",\"page\":\"NOPE\",\"timeout_ms\":2}", sink, &quit)) goto fail;
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
