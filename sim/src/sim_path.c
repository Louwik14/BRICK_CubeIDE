#include "sim_path.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

#ifndef SIM_ROOT_DIR
#error SIM_ROOT_DIR must be provided by CMake
#endif

static int path_is_inside(const char *path, const char *root)
{
    const size_t root_length = strlen(root);
    return (_strnicmp(path, root, root_length) == 0)
        && ((path[root_length] == '\\') || (path[root_length] == '/')
            || (path[root_length] == '\0'));
}

static int full_path(const char *input, char *output, size_t output_size)
{
    const DWORD length = GetFullPathNameA(input, (DWORD)output_size, output, NULL);
    return (length > 0U) && (length < output_size);
}

static int ensure_parent_directories(const char *file_path)
{
    char path[MAX_PATH];
    if (strlen(file_path) >= sizeof(path)) return 0;
    (void)snprintf(path, sizeof(path), "%s", file_path);
    char *last = strrchr(path, '\\');
    if (last == NULL) last = strrchr(path, '/');
    if (last == NULL) return 0;
    *last = '\0';

    for (char *p = path + 3; *p != '\0'; ++p)
    {
        if ((*p == '\\') || (*p == '/'))
        {
            const char saved = *p;
            *p = '\0';
            if (!CreateDirectoryA(path, NULL) && (GetLastError() != ERROR_ALREADY_EXISTS))
            {
                return 0;
            }
            *p = saved;
        }
    }
    return CreateDirectoryA(path, NULL) || (GetLastError() == ERROR_ALREADY_EXISTS);
}

int sim_path_resolve_output(const char *requested, char *resolved,
                            size_t resolved_size, char *error,
                            size_t error_size)
{
    char sim_root[MAX_PATH];
    char out_root[MAX_PATH];
    char build_root[MAX_PATH];
    char dist_root[MAX_PATH];

    if ((requested == NULL) || (*requested == '\0'))
    {
        (void)snprintf(error, error_size, "capture path is required");
        return 0;
    }
    if (!full_path(SIM_ROOT_DIR, sim_root, sizeof(sim_root))
            || !full_path(SIM_ROOT_DIR "/out", out_root, sizeof(out_root))
            || !full_path(SIM_ROOT_DIR "/build", build_root, sizeof(build_root))
            || !full_path(SIM_ROOT_DIR "/dist", dist_root, sizeof(dist_root))
            || !full_path(requested, resolved, resolved_size))
    {
        (void)snprintf(error, error_size, "capture path is invalid or too long");
        return 0;
    }

    if (!path_is_inside(resolved, out_root)
            && !path_is_inside(resolved, build_root)
            && !path_is_inside(resolved, dist_root))
    {
        (void)snprintf(error, error_size,
                       "capture path must be inside sim/out, sim/build or sim/dist");
        return 0;
    }

    const char *extension = strrchr(resolved, '.');
    if ((extension == NULL) || (_stricmp(extension, ".png") != 0))
    {
        (void)snprintf(error, error_size, "capture path must end in .png");
        return 0;
    }
    if (!ensure_parent_directories(resolved))
    {
        (void)snprintf(error, error_size, "could not create capture directory");
        return 0;
    }
    (void)sim_root;
    return 1;
}

