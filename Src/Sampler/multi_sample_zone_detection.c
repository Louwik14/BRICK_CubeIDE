#include "Sampler/multi_sample_zone_detection.h"

#include <string.h>

#include "Sampler/multi_sample_pool.h"

typedef struct
{
    uint8_t range_valid;
    uint8_t range_root;
    uint8_t range_low;
    uint8_t range_high;
    uint8_t pair_valid;
    uint8_t pair_root;
    uint8_t pair_velocity;
    uint8_t prefix_valid;
    uint8_t prefix_root;
    uint8_t note_valid;
    uint8_t note_root;
    uint8_t take_valid;
} multi_zone_filename_fact_t;

static uint8_t parse_u8(const char *first, const char *last, uint8_t *out)
{
    if ((first == 0) || (last == 0) || (out == 0) || (first >= last)) return 0U;
    uint32_t value = 0U;
    for (const char *p = first; p < last; ++p)
    {
        if ((*p < '0') || (*p > '9')) return 0U;
        value = value * 10U + (uint32_t)(*p - '0');
        if (value > 127U) return 0U;
    }
    *out = (uint8_t)value;
    return 1U;
}

static uint8_t decimal_token(const char *first, const char *last)
{
    if ((first == 0) || (last == 0) || (first >= last)) return 0U;
    for (const char *p = first; p < last; ++p)
        if ((*p < '0') || (*p > '9')) return 0U;
    return 1U;
}

static const char *filename_end(const char *name)
{
    const char *dot = (name != 0) ? strrchr(name, '.') : 0;
    return (dot != 0) ? dot : ((name != 0) ? name + strlen(name) : 0);
}

static uint8_t separator(char c)
{
    return ((c == '_') || (c == '-') || (c == ' ')) ? 1U : 0U;
}

static uint8_t note_pitch(char c)
{
    switch (c)
    {
        case 'C': case 'c': return 0U;
        case 'D': case 'd': return 2U;
        case 'E': case 'e': return 4U;
        case 'F': case 'f': return 5U;
        case 'G': case 'g': return 7U;
        case 'A': case 'a': return 9U;
        case 'B': case 'b': return 11U;
        default: return UINT8_MAX;
    }
}

static uint8_t parse_text_note(const char *name, const char *end, uint8_t *out)
{
    for (const char *p = name; (p != 0) && (p < end); ++p)
    {
        uint8_t pitch = note_pitch(*p);
        if (pitch == UINT8_MAX) continue;
        if ((p != name) && (separator(p[-1]) == 0U)) continue;
        const char *q = p + 1;
        if ((q < end) && ((*q == '#') || (*q == 'b')))
        {
            pitch = (uint8_t)((pitch + ((*q == '#') ? 1U : 11U)) % 12U);
            q++;
        }
        if ((q >= end) || (*q < '0') || (*q > '9')) continue;
        const uint8_t octave = (uint8_t)(*q - '0');
        q++;
        if ((q < end) && (separator(*q) == 0U)) continue;
        const uint16_t midi = (uint16_t)(octave + 2U) * 12U + pitch;
        if (midi <= 127U)
        {
            *out = (uint8_t)midi;
            return 1U;
        }
    }
    return 0U;
}

static uint8_t previous_token(const char *name, const char **cursor,
                              const char **first, const char **last)
{
    const char *end = *cursor;
    while ((end > name) && (separator(end[-1]) != 0U)) end--;
    const char *begin = end;
    while ((begin > name) && (separator(begin[-1]) == 0U)) begin--;
    if (begin == end) return 0U;
    *cursor = begin;
    *first = begin;
    *last = end;
    return 1U;
}

static multi_zone_filename_fact_t analyze_filename(const char *name)
{
    multi_zone_filename_fact_t fact;
    memset(&fact, 0, sizeof(fact));
    const char *const end = filename_end(name);
    if ((name == 0) || (end == 0) || (name == end)) return fact;

    const char *p = name;
    while ((p < end) && (*p >= '0') && (*p <= '9')) p++;
    if ((p > name) && (p < end) && (*p == ' ')
        && (parse_u8(name, p, &fact.prefix_root) != 0U)) fact.prefix_valid = 1U;
    fact.note_valid = parse_text_note(name, end, &fact.note_root);

    const char *cursor = end;
    const char *token_first[4] = {0};
    const char *token_last[4] = {0};
    uint8_t token_count = 0U;
    while ((token_count < 4U)
           && (previous_token(name, &cursor,
                              &token_first[token_count],
                              &token_last[token_count]) != 0U))
        token_count++;
    if (token_count != 0U)
    {
        const uint32_t width = (uint32_t)(token_last[0] - token_first[0]);
        fact.take_valid = ((width >= 4U)
            && (decimal_token(token_first[0], token_last[0]) != 0U)) ? 1U : 0U;
        const uint8_t tail = (fact.take_valid != 0U) ? 1U : 0U;
        if (token_count >= (uint8_t)(tail + 2U))
        {
            if ((parse_u8(token_first[tail + 1U], token_last[tail + 1U],
                          &fact.pair_root) != 0U)
                && (parse_u8(token_first[tail], token_last[tail],
                             &fact.pair_velocity) != 0U)
                && (fact.pair_velocity != 0U)) fact.pair_valid = 1U;
            if ((token_count >= (uint8_t)(tail + 3U))
                && (parse_u8(token_first[tail + 2U], token_last[tail + 2U],
                             &fact.range_root) != 0U)
                && (parse_u8(token_first[tail + 1U], token_last[tail + 1U],
                             &fact.range_low) != 0U)
                && (parse_u8(token_first[tail], token_last[tail],
                             &fact.range_high) != 0U)
                && (fact.range_low <= fact.range_high)) fact.range_valid = 1U;
        }
    }
    return fact;
}

static uint8_t same_variant_base(const char *a, const char *b)
{
    const char *ae = filename_end(a); const char *be = filename_end(b);
    if ((a == 0) || (b == 0) || (ae == 0) || (be == 0)) return 0U;
    const char *as = ae; const char *bs = be;
    while ((as > a) && (as[-1] >= '0') && (as[-1] <= '9')) as--;
    while ((bs > b) && (bs[-1] >= '0') && (bs[-1] <= '9')) bs--;
    if ((as < ae) && (as > a) && (as[-1] == '_')) as--;
    else as = ae;
    if ((bs < be) && (bs > b) && (bs[-1] == '_')) bs--;
    else bs = be;
    return (((size_t)(as - a) == (size_t)(bs - b))
            && (memcmp(a, b, (size_t)(as - a)) == 0)) ? 1U : 0U;
}

multi_sample_zone_detect_result_t multi_sample_zone_detect_folder(
    const multi_sample_zone_observation_t *obs, uint16_t count,
    multi_sample_zone_resolution_t *out)
{
    if ((obs == 0) || (out == 0) || (count == 0U)
        || (count > MULTI_SAMPLE_POOL_MAX_SAMPLES)) return MULTI_SAMPLE_ZONE_DETECT_INVALID;

    uint16_t pair_count = 0U;
    uint8_t pair_first_root = 0U;
    uint8_t pair_first_velocity = 0U;
    uint8_t pair_roots_vary = 0U;
    uint8_t pair_velocities_vary = 0U;
    uint16_t prefix_count = 0U;
    uint8_t prefix_first_root = 0U;
    uint8_t prefix_roots_vary = 0U;
    uint8_t prefix_note_corroborated = 0U;
    for (uint16_t i = 0U; i < count; ++i)
    {
        const multi_zone_filename_fact_t f = analyze_filename(obs[i].filename);
        if (f.pair_valid != 0U)
        {
            if (pair_count == 0U)
            {
                pair_first_root = f.pair_root;
                pair_first_velocity = f.pair_velocity;
            }
            else
            {
                if (f.pair_root != pair_first_root) pair_roots_vary = 1U;
                if (f.pair_velocity != pair_first_velocity) pair_velocities_vary = 1U;
            }
            pair_count++;
        }
        if (f.prefix_valid != 0U)
        {
            if (prefix_count == 0U) prefix_first_root = f.prefix_root;
            else if (f.prefix_root != prefix_first_root) prefix_roots_vary = 1U;
            if ((f.note_valid != 0U) && ((f.prefix_root % 12U) == (f.note_root % 12U)))
                prefix_note_corroborated = 1U;
            prefix_count++;
        }
    }
    const uint8_t pair_convention = ((pair_count > 1U)
                                     && ((pair_roots_vary != 0U)
                                         || (pair_velocities_vary != 0U))) ? 1U : 0U;
    const uint8_t prefix_convention =
        ((prefix_note_corroborated != 0U)
         || ((prefix_count > 1U) && (prefix_roots_vary != 0U))) ? 1U : 0U;

    for (uint16_t i = 0U; i < count; ++i)
    {
        memset(&out[i], 0, sizeof(out[i]));
        const multi_zone_filename_fact_t f = analyze_filename(obs[i].filename);
        if (obs[i].smpl_root_valid != 0U)
        { out[i].root_note = obs[i].smpl_root; out[i].metadata_flags = MULTI_SAMPLE_INDEX_META_ROOT_SMPL; }
        else if (obs[i].inst_root_valid != 0U)
        { out[i].root_note = obs[i].inst_root; out[i].metadata_flags = MULTI_SAMPLE_INDEX_META_ROOT_INST; }
        else if (f.range_valid != 0U)
        { out[i].root_note = f.range_root; out[i].metadata_flags = MULTI_SAMPLE_INDEX_META_ROOT_FILENAME; }
        else if ((pair_convention != 0U) && (f.pair_valid != 0U))
        { out[i].root_note = f.pair_root; out[i].metadata_flags = MULTI_SAMPLE_INDEX_META_ROOT_FILENAME; }
        else if ((prefix_convention != 0U) && (f.prefix_valid != 0U))
        { out[i].root_note = f.prefix_root; out[i].metadata_flags = MULTI_SAMPLE_INDEX_META_ROOT_FILENAME; }
        else if (f.note_valid != 0U)
        { out[i].root_note = f.note_root; out[i].metadata_flags = MULTI_SAMPLE_INDEX_META_ROOT_FILENAME; }
        else if (f.pair_valid != 0U)
        { out[i].root_note = f.pair_root; out[i].metadata_flags = MULTI_SAMPLE_INDEX_META_ROOT_FILENAME; }
        else if (f.prefix_valid != 0U)
        { out[i].root_note = f.prefix_root; out[i].metadata_flags = MULTI_SAMPLE_INDEX_META_ROOT_FILENAME; }
        else
        {
            uint16_t rank = 0U;
            for (uint16_t j = 0U; j < count; ++j)
            {
                const multi_zone_filename_fact_t other = analyze_filename(obs[j].filename);
                const uint8_t other_resolved =
                    (uint8_t)((obs[j].smpl_root_valid != 0U)
                        || (obs[j].inst_root_valid != 0U)
                        || (other.range_valid != 0U)
                        || ((pair_convention != 0U) && (other.pair_valid != 0U))
                        || ((prefix_convention != 0U) && (other.prefix_valid != 0U))
                        || (other.note_valid != 0U)
                        || (other.pair_valid != 0U)
                        || (other.prefix_valid != 0U));
                if ((other_resolved == 0U)
                    && (strcmp(obs[j].filename, obs[i].filename) < 0)) rank++;
            }
            if (rank > (127U - 36U)) return MULTI_SAMPLE_ZONE_DETECT_OVERFLOW;
            out[i].root_note = (uint8_t)(36U + rank);
            out[i].metadata_flags = MULTI_SAMPLE_INDEX_META_ROOT_ALPHA;
        }

        if (obs[i].inst_velocity_valid != 0U)
        { out[i].vel_low = obs[i].inst_vel_low; out[i].vel_high = obs[i].inst_vel_high;
          out[i].metadata_flags |= MULTI_SAMPLE_INDEX_META_VEL_INST; }
        else if (f.range_valid != 0U)
        { out[i].vel_low = f.range_low; out[i].vel_high = f.range_high;
          out[i].metadata_flags |= MULTI_SAMPLE_INDEX_META_VEL_FILENAME; }
        else if ((pair_convention != 0U) && (f.pair_valid != 0U))
        { out[i].vel_low = f.pair_velocity; out[i].vel_high = f.pair_velocity;
          out[i].velocity_center_valid = 1U; out[i].velocity_center = f.pair_velocity;
          out[i].metadata_flags |= MULTI_SAMPLE_INDEX_META_VEL_FILENAME; }
        else
        { out[i].vel_low = 1U; out[i].vel_high = 127U;
          out[i].metadata_flags |= MULTI_SAMPLE_INDEX_META_VEL_ALPHA; }
    }

    for (uint16_t i = 0U; i < count; ++i)
    {
        const multi_zone_filename_fact_t fi = analyze_filename(obs[i].filename);
        if (fi.take_valid == 0U) continue;
        for (uint16_t j = 0U; j < count; ++j)
        {
            if ((i == j) || (out[i].root_note != out[j].root_note)
                || (out[i].vel_low != out[j].vel_low) || (out[i].vel_high != out[j].vel_high)
                || (same_variant_base(obs[i].filename, obs[j].filename) == 0U)) continue;
            const multi_zone_filename_fact_t fj = analyze_filename(obs[j].filename);
            if ((fj.take_valid == 0U) || (strcmp(obs[j].filename, obs[i].filename) < 0))
            { out[i].skip_variant = 1U; break; }
        }
    }
    return MULTI_SAMPLE_ZONE_DETECT_OK;
}
