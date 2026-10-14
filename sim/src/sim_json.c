#include "sim_json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *find_value(const char *json, const char *key)
{
    char needle[96];
    if ((json == NULL) || (key == NULL)
            || (snprintf(needle, sizeof(needle), "\"%s\"", key) < 0))
    {
        return NULL;
    }
    const char *p = strstr(json, needle);
    if (p == NULL) return NULL;
    p += strlen(needle);
    while (isspace((unsigned char)*p)) ++p;
    if (*p++ != ':') return NULL;
    while (isspace((unsigned char)*p)) ++p;
    return p;
}

int sim_json_get_string(const char *json, const char *key, char *out, size_t out_size)
{
    const char *p = find_value(json, key);
    size_t used = 0U;
    if ((p == NULL) || (*p++ != '"') || (out == NULL) || (out_size == 0U)) return 0;
    while ((*p != '\0') && (*p != '"'))
    {
        unsigned char c = (unsigned char)*p++;
        if (c == '\\')
        {
            c = (unsigned char)*p++;
            if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            else if (c == 't') c = '\t';
            else if ((c != '"') && (c != '\\') && (c != '/')) return 0;
        }
        if (used + 1U >= out_size) return 0;
        out[used++] = (char)c;
    }
    if (*p != '"') return 0;
    out[used] = '\0';
    return 1;
}

int sim_json_get_u32(const char *json, const char *key, uint32_t *out)
{
    const char *p = find_value(json, key);
    char *end = NULL;
    if ((p == NULL) || (out == NULL) || !isdigit((unsigned char)*p)) return 0;
    unsigned long value = strtoul(p, &end, 10);
    if ((end == p) || (value > UINT32_MAX)) return 0;
    *out = (uint32_t)value;
    return 1;
}

int sim_json_get_i32(const char *json, const char *key, int32_t *out)
{
    const char *p = find_value(json, key);
    char *end = NULL;
    if ((p == NULL) || (out == NULL) || ((*p != '-') && !isdigit((unsigned char)*p))) return 0;
    const long value = strtol(p, &end, 10);
    if ((end == p) || (value < INT32_MIN) || (value > INT32_MAX)) return 0;
    *out = (int32_t)value;
    return 1;
}

int sim_json_get_string_array(const char *json, const char *key,
                              char values[][64], size_t capacity, size_t *count)
{
    const char *p = find_value(json, key);
    size_t used = 0U;
    if (!p || !values || !count || *p++ != '[') return 0;
    for (;;)
    {
        while (isspace((unsigned char)*p)) ++p;
        if (*p == ']') { *count = used; return 1; }
        if (*p++ != '"' || used >= capacity) return 0;
        size_t n = 0U;
        while (*p && *p != '"')
        {
            if (*p == '\\') ++p;
            if (!*p || n + 1U >= 64U) return 0;
            values[used][n++] = *p++;
        }
        if (*p++ != '"') return 0;
        values[used++][n] = '\0';
        while (isspace((unsigned char)*p)) ++p;
        if (*p == ',') { ++p; continue; }
        if (*p == ']') { *count = used; return 1; }
        return 0;
    }
}

int sim_json_get_id(const char *json, char *out, size_t out_size)
{
    const char *p = find_value(json, "id");
    size_t used = 0U;
    if ((p == NULL) || (out == NULL) || (out_size < 2U)) return 0;
    if (*p == '"')
    {
        out[used++] = *p++;
        while ((*p != '\0') && (used + 2U < out_size))
        {
            out[used++] = *p;
            if ((*p == '"') && ((p == json) || (p[-1] != '\\')))
            {
                out[used] = '\0';
                return 1;
            }
            ++p;
        }
        return 0;
    }
    while ((*p != '\0') && !isspace((unsigned char)*p)
            && (*p != ',') && (*p != '}'))
    {
        if (used + 1U >= out_size) return 0;
        out[used++] = *p++;
    }
    if (used == 0U) return 0;
    out[used] = '\0';
    return 1;
}

void sim_json_escape(const char *input, char *output, size_t output_size)
{
    size_t used = 0U;
    if ((output == NULL) || (output_size == 0U)) return;
    if (input == NULL) input = "";
    while ((*input != '\0') && (used + 1U < output_size))
    {
        const unsigned char c = (unsigned char)*input++;
        const char *replacement = NULL;
        if (c == '"') replacement = "\\\"";
        else if (c == '\\') replacement = "\\\\";
        else if (c == '\n') replacement = "\\n";
        else if (c == '\r') replacement = "\\r";
        else if (c == '\t') replacement = "\\t";
        if (replacement != NULL)
        {
            const size_t length = strlen(replacement);
            if (used + length >= output_size) break;
            memcpy(output + used, replacement, length);
            used += length;
        }
        else if (c >= 0x20U)
        {
            output[used++] = (char)c;
        }
    }
    output[used] = '\0';
}
