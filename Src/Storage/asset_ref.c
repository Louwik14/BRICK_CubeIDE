#include "Storage/asset_ref.h"

#include <string.h>

uint8_t asset_ref_kind_valid(persist_control_asset_kind_key_t kind)
{
    return (uint8_t)((kind == PERSIST_ASSET_SAMPLE_STREAM)
        || (kind == PERSIST_ASSET_SAMPLE_RAM)
        || (kind == PERSIST_ASSET_WAVETABLE)
        || (kind == PERSIST_ASSET_MULTI));
}

uint8_t asset_ref_make_canonical(persist_control_asset_kind_key_t kind,
                                 const char *source_path,
                                 persist_control_asset_ref_t *out_ref)
{
    if ((source_path == NULL) || (out_ref == NULL)
            || (asset_ref_kind_valid(kind) == 0U)) return 0U;
    persist_control_asset_ref_t ref = {.kind = kind};
    uint8_t previous_slash = 0U;
    while (*source_path != '\0')
    {
        char value = *source_path++;
        if (value == '\\') value = '/';
        if (value == '/')
        {
            if (previous_slash != 0U) continue;
            previous_slash = 1U;
        }
        else previous_slash = 0U;
        if ((value >= 'A') && (value <= 'Z'))
            value = (char)(value + ('a' - 'A'));
        if (ref.path_length >= PERSIST_CONTROL_ASSET_PATH_BYTES) return 0U;
        ref.canonical_path[ref.path_length++] = value;
    }
    while ((ref.path_length > 1U)
            && (ref.canonical_path[ref.path_length - 1U] == '/'))
        --ref.path_length;
    if (ref.path_length == 0U) return 0U;
    if (ref.path_length < PERSIST_CONTROL_ASSET_PATH_BYTES)
        ref.canonical_path[ref.path_length] = '\0';
    *out_ref = ref;
    return 1U;
}

uint8_t asset_ref_is_canonical(const persist_control_asset_ref_t *ref)
{
    if ((ref == NULL) || (ref->path_length == 0U)
            || (ref->path_length > PERSIST_CONTROL_ASSET_PATH_BYTES)
            || (asset_ref_kind_valid(ref->kind) == 0U)) return 0U;
    char source[PERSIST_CONTROL_ASSET_PATH_BYTES + 1U];
    memcpy(source, ref->canonical_path, ref->path_length);
    source[ref->path_length] = '\0';
    persist_control_asset_ref_t canonical;
    return (uint8_t)((asset_ref_make_canonical(ref->kind, source, &canonical) != 0U)
        && (canonical.path_length == ref->path_length)
        && (memcmp(canonical.canonical_path, ref->canonical_path,
                   ref->path_length) == 0));
}
