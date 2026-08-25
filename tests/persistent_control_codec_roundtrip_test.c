#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "Core/track_state.h"
#include "Param/param_registry.h"
#include "Storage/persistent_control_codec.h"
#include "Storage/persistent_entity_topology.h"
#include "Storage/persistent_key_catalog.h"

const param_desc_t param_registry[PARAM_COUNT] = {0};

ui_track_type_t track_state_get_type(uint8_t track)
{
    (void)track;
    return UI_TRACK_TYPE_NONE;
}

uint8_t seq_param_iface_is_param_plockable(param_id_t param_id)
{
    return (uint8_t)(((param_id >= PARAM_FM_OP1_LEVEL)
                && (param_id <= PARAM_FM_OP6_KEY))
            || (param_id == PARAM_AUDIO_FX_P1)
            || (param_id == PARAM_AUDIO_FX_P2)
            || (param_id == PARAM_AUDIO_FX_P3)
            || (param_id == PARAM_AUDIO_FX_B_P1)
            || (param_id == PARAM_AUDIO_FX_B_P2)
            || (param_id == PARAM_AUDIO_FX_B_P3));
}

typedef struct
{
    uint8_t *bytes;
    uint32_t capacity;
    uint32_t size;
    uint32_t cursor;
} memory_stream_t;

static uint8_t memory_write(void *context, const uint8_t *data, uint32_t length)
{
    memory_stream_t *stream = context;
    if ((stream == NULL) || (data == NULL)
            || (length > stream->capacity - stream->size))
        return 0U;
    memcpy(&stream->bytes[stream->size], data, length);
    stream->size += length;
    return 1U;
}

static uint8_t memory_read(void *context, uint8_t *data, uint32_t length)
{
    memory_stream_t *stream = context;
    if ((stream == NULL) || (data == NULL)
            || (stream->cursor > stream->size)
            || (length > stream->size - stream->cursor))
        return 0U;
    memcpy(data, &stream->bytes[stream->cursor], length);
    stream->cursor += length;
    return 1U;
}

static uint8_t memory_reset(void *context)
{
    memory_stream_t *stream = context;
    if (stream == NULL) return 0U;
    stream->cursor = 0U;
    return 1U;
}

static uint8_t memory_size(void *context, uint32_t *out_size)
{
    memory_stream_t *stream = context;
    if ((stream == NULL) || (out_size == NULL)) return 0U;
    *out_size = stream->size;
    return 1U;
}

static void initialize_modulation(persist_control_modulation_t *modulation)
{
    for (uint8_t i = 0U; i < PERSIST_CONTROL_MOD_LFO_COUNT; ++i)
    {
        modulation->lfos[i].shape_key = PERSIST_LFO_SHAPE_SINE;
        modulation->lfos[i].trigger_key = PERSIST_LFO_TRIGGER_FREE;
    }
    for (uint8_t i = 0U; i < 2U; ++i)
    {
        modulation->multi[i].source_a_key = PERSIST_MOD_SOURCE_NONE;
        modulation->multi[i].source_b_key = PERSIST_MOD_SOURCE_NONE;
        modulation->slew[i].source_key = PERSIST_MOD_SOURCE_NONE;
    }
    for (uint8_t i = 0U; i < PERSIST_CONTROL_MOD_ROUTE_COUNT; ++i)
    {
        modulation->routes[i].source_key = PERSIST_MOD_SOURCE_NONE;
        modulation->routes[i].destination_parameter = PERSIST_CONTROL_KEY_NONE;
    }
}

static void initialize_pattern(persist_control_pattern_t *pattern,
                               uint8_t group_active)
{
    memset(pattern, 0, sizeof(*pattern));
    for (uint8_t entity = 0U; entity < PERSIST_CONTROL_ENTITY_COUNT; ++entity)
    {
        persist_entity_caps_t caps;
        persist_control_entity_t *record = &pattern->entities[entity];
        (void)persist_entity_caps_resolve(group_active, entity, &caps);
        record->entity_id = entity;
        record->family = PERSIST_FAMILY_OFF;
        record->type = PERSIST_TYPE_NONE;
        if ((caps.active == 0U) && (entity >= PERSIST_CONTROL_FIRST_GROUP_CHILD_ID))
        {
            record->family = PERSIST_FAMILY_SAMPLER;
            record->type = PERSIST_TYPE_RAM_SAMPLE;
        }
        record->midi_channel = (uint8_t)(entity + 1U);
        record->midi_source_key = PERSIST_MIDI_SOURCE_INTERNAL;
        record->input_key = PERSIST_INPUT_NONE;
        record->sequence.length = 1U;
        record->sequence.division = 1U;
        if (caps.modulation_owner != 0U)
        {
            record->modulation_present = 1U;
            initialize_modulation(&record->modulation);
            for (uint8_t route = 0U; route < PERSIST_CONTROL_MOD_ROUTE_COUNT; ++route)
                record->modulation.routes[route].destination_entity = entity;
        }
    }
    if (group_active != 0U)
    {
        pattern->entities[PERSIST_CONTROL_GROUP_MASTER_ID].family = PERSIST_FAMILY_SAMPLER;
        pattern->entities[PERSIST_CONTROL_GROUP_MASTER_ID].type = PERSIST_TYPE_GROUP;
        for (uint8_t entity = PERSIST_CONTROL_FIRST_GROUP_CHILD_ID;
             entity < PERSIST_CONTROL_ENTITY_COUNT; ++entity)
        {
            pattern->entities[entity].family = PERSIST_FAMILY_SAMPLER;
            pattern->entities[entity].type = PERSIST_TYPE_RAM_SAMPLE;
        }
    }
    pattern->globals.tempo_milli_bpm = 120000U;
    pattern->globals.clock_source_key = PERSIST_CLOCK_INTERNAL;
    pattern->globals.record_start_key = PERSIST_RECORD_START_DEFAULT;
    pattern->globals.record_length_key = PERSIST_RECORD_LENGTH_PATTERN;
}

static int pattern_roundtrip(uint8_t group_active)
{
    const uint32_t capacity = 1024U * 1024U;
    persist_control_pattern_t *input = calloc(1U, sizeof(*input));
    persist_codec_pattern_staging_t *output = calloc(1U, sizeof(*output));
    uint8_t *bytes = malloc(capacity);
    if ((input == NULL) || (output == NULL) || (bytes == NULL)) return 1;

    initialize_pattern(input, group_active);
    static const param_id_t audio_fx_params[] = {
        PARAM_AUDIO_FX_MODEL, PARAM_AUDIO_FX_P1, PARAM_AUDIO_FX_P2,
        PARAM_AUDIO_FX_P3, PARAM_AUDIO_FX_B_MODEL, PARAM_AUDIO_FX_B_P1,
        PARAM_AUDIO_FX_B_P2, PARAM_AUDIO_FX_B_P3, PARAM_AUDIO_FX_FILTER_POS,
        PARAM_AUDIO_FX_ORDER, PARAM_AUDIO_FX_MODE_A, PARAM_AUDIO_FX_MODE_B
    };
    for (uint8_t i = 0U;
         i < (uint8_t)(sizeof(audio_fx_params) / sizeof(audio_fx_params[0])); ++i)
    {
        persist_control_parameter_t *parameter =
            &input->entities[0].parameters[input->entities[0].parameter_count++];
        (void)persist_key_param_to_disk(audio_fx_params[i], &parameter->key);
        parameter->kind = PERSIST_VALUE_FLOAT32;
    }
    input->entities[0].sequence.steps[0].trigger = 1U;
    input->entities[0].sequence.steps[0].play_count = PERSIST_CONTROL_PLAY_ITEM_COUNT;
    for (uint8_t item = 0U; item < PERSIST_CONTROL_PLAY_ITEM_COUNT; ++item)
    {
        input->entities[0].sequence.steps[0].play[item].note = (uint8_t)(60U + item);
        input->entities[0].sequence.steps[0].play[item].present_mask = 1U;
    }
    uint16_t lock_count = 0U;
    for (uint8_t step = 0U; step < 16U; ++step)
    {
        persist_control_step_t *record = &input->entities[0].sequence.steps[step];
        for (param_id_t id = PARAM_FM_OP1_LEVEL;
             (id <= PARAM_FM_OP6_KEY) && (record->lock_count < 32U); ++id)
        {
            if (seq_param_iface_is_param_plockable(id) == 0U) continue;
            persist_control_step_lock_t *lock = &record->locks[record->lock_count++];
            if (persist_key_param_to_disk(id, &lock->parameter) == 0U) return 2;
            lock->kind = PERSIST_VALUE_FLOAT32;
            ++lock_count;
        }
    }
    if (lock_count != SEQ_PLOCK_POOL_CAP_PER_TRACK) return 3;
    if (group_active != 0U)
    {
        persist_control_entity_t *child =
            &input->entities[PERSIST_CONTROL_FIRST_GROUP_CHILD_ID];
        static const param_id_t group_level_params[] = {
            PARAM_GROUP_FX_A_LEVEL, PARAM_GROUP_FX_B_LEVEL
        };
        for (uint8_t i = 0U; i < 2U; ++i)
        {
            persist_control_parameter_t *parameter =
                &child->parameters[child->parameter_count++];
            if (persist_key_param_to_disk(group_level_params[i],
                                          &parameter->key) == 0U)
                return 4;
            parameter->kind = PERSIST_VALUE_FLOAT32;
            parameter->value.f32 = (i == 0U) ? 0.25f : 0.75f;
        }
        persist_control_step_t *child_step =
            &child->sequence.steps[0];
        static const param_id_t audio_fx_plocks[] = {
            PARAM_GROUP_FX_A_LEVEL, PARAM_GROUP_FX_B_LEVEL
        };
        child_step->trigger = 1U;
        child_step->play_count = PERSIST_CONTROL_CHILD_PLAY_ITEM_COUNT;
        child_step->play[0].note = 48U;
        child_step->play[0].present_mask = 1U;
        for (uint8_t i = 0U;
             i < (uint8_t)(sizeof(audio_fx_plocks) / sizeof(audio_fx_plocks[0])); ++i)
        {
            persist_control_step_lock_t *lock =
                &child_step->locks[child_step->lock_count++];
            if (persist_key_param_to_disk(audio_fx_plocks[i], &lock->parameter) == 0U)
                return 4;
            lock->kind = PERSIST_VALUE_FLOAT32;
        }
        persist_control_mod_route_t *route =
            &input->entities[PERSIST_CONTROL_GROUP_MASTER_ID].modulation.routes[0];
        route->source_key = PERSIST_MOD_SOURCE_LFO1;
        route->destination_entity = PERSIST_CONTROL_FIRST_GROUP_CHILD_ID;
        (void)persist_key_param_to_disk(PARAM_GROUP_FX_A_LEVEL,
                                        &route->destination_parameter);
        route->depth = 0.5f;
        route->enabled = 1U;
    }

    memory_stream_t stream = {.bytes = bytes, .capacity = capacity};
    const persist_codec_sink_t sink = {memory_write, &stream};
    const persist_codec_source_t source = {
        memory_read, memory_reset, memory_size, &stream
    };
    int result = 0;
    const persist_codec_result_t encode = persist_codec_encode_pattern(input, &sink, NULL);
    if (encode != PERSIST_CODEC_OK) result = 100 + (int)encode;
    if (result == 0)
    {
        const persist_codec_result_t decode = persist_codec_decode_pattern(&source, output);
        if (decode != PERSIST_CODEC_OK) result = 200 + (int)decode;
    }
    if ((result == 0) && (memcmp(input, &output->pattern, sizeof(*input)) != 0))
        result = 300;
    free(bytes);
    free(output);
    free(input);
    return result;
}

static int topology_contract(void)
{
    for (uint8_t group = 0U; group <= 1U; ++group)
    {
        for (uint8_t entity = 0U; entity < PERSIST_CONTROL_ENTITY_COUNT; ++entity)
        {
            persist_entity_caps_t caps;
            if (persist_entity_caps_resolve(group, entity, &caps) == 0U
                    || caps.exists == 0U || caps.persistable == 0U)
                return 0;
            if (entity < PERSIST_CONTROL_FIRST_GROUP_CHILD_ID)
            {
                if (caps.active == 0U || caps.modulation_owner == 0U
                        || caps.audio_fx_owner == 0U)
                    return 0;
            }
            else if ((caps.active != group) || (caps.modulation_owner != 0U)
                    || (caps.audio_fx_owner != 0U))
                return 0;
        }
    }
    return 1;
}

static int boundary_rejections(void)
{
    persist_control_pattern_t *pattern = calloc(1U, sizeof(*pattern));
    if (pattern == NULL) return 1;
    initialize_pattern(pattern, 1U);

    pattern->entities[0].sequence.steps[0].play_count =
        (uint8_t)(PERSIST_CONTROL_PLAY_ITEM_COUNT + 1U);
    if (persist_codec_validate_pattern(pattern) != PERSIST_CODEC_INVALID_PLAY)
    {
        free(pattern);
        return 2;
    }
    pattern->entities[0].sequence.steps[0].play_count = 0U;
    pattern->entities[PERSIST_CONTROL_FIRST_GROUP_CHILD_ID]
        .sequence.steps[0].play_count = 2U;
    if (persist_codec_validate_pattern(pattern) != PERSIST_CODEC_INVALID_PLAY)
    {
        free(pattern);
        return 3;
    }
    pattern->entities[PERSIST_CONTROL_FIRST_GROUP_CHILD_ID]
        .sequence.steps[0].play_count = 0U;
    for (uint8_t step = 0U; step < 16U; ++step)
    {
        persist_control_step_t *record = &pattern->entities[0].sequence.steps[step];
        for (param_id_t id = PARAM_FM_OP1_LEVEL;
             (id <= PARAM_FM_OP6_KEY) && (record->lock_count < 32U); ++id)
        {
            if (seq_param_iface_is_param_plockable(id) == 0U) continue;
            persist_control_step_lock_t *lock = &record->locks[record->lock_count++];
            (void)persist_key_param_to_disk(id, &lock->parameter);
            lock->kind = PERSIST_VALUE_FLOAT32;
        }
    }
    pattern->entities[0].sequence.steps[16].lock_count = 1U;
    (void)persist_key_param_to_disk(PARAM_AUDIO_FX_P1,
        &pattern->entities[0].sequence.steps[16].locks[0].parameter);
    pattern->entities[0].sequence.steps[16].locks[0].kind = PERSIST_VALUE_FLOAT32;
    if (persist_codec_validate_pattern(pattern) != PERSIST_CODEC_INVALID_PLOCK)
    {
        free(pattern);
        return 4;
    }

    free(pattern);
    return 0;
}

typedef struct
{
    persist_control_pattern_t working;
    persist_control_pattern_record_t record;
    persist_control_macros_t macros;
    persist_codec_project_metadata_t metadata;
    uint8_t working_applied;
    uint8_t macros_applied;
    uint8_t record_applied;
    uint8_t committed;
} project_fixture_t;

static const persist_control_pattern_t *project_working_get(void *context)
{
    project_fixture_t *fixture = context;
    return &fixture->working;
}

static const persist_control_pattern_record_t *project_pattern_get(
    void *context, uint16_t ordinal)
{
    project_fixture_t *fixture = context;
    return (ordinal == 0U) ? &fixture->record : NULL;
}

static const persist_control_asset_ref_t *project_asset_get(
    void *context, uint16_t ordinal)
{
    (void)context;
    (void)ordinal;
    return NULL;
}

static uint8_t project_begin(void *context)
{
    return (context != NULL) ? 1U : 0U;
}

static uint8_t project_validate_asset(void *context,
                                      const persist_control_asset_ref_t *asset)
{
    (void)context;
    (void)asset;
    return 1U;
}

static uint8_t project_put_asset(void *context,
                                 const persist_control_asset_ref_t *asset)
{
    (void)context;
    (void)asset;
    return 1U;
}

static uint8_t project_apply_working(void *context,
                                     const persist_codec_project_metadata_t *metadata,
                                     const persist_control_pattern_t *pattern)
{
    project_fixture_t *fixture = context;
    if ((fixture == NULL) || (metadata == NULL) || (pattern == NULL)) return 0U;
    fixture->metadata = *metadata;
    fixture->working = *pattern;
    fixture->working_applied = 1U;
    return 1U;
}

static uint8_t project_apply_macros(void *context,
                                    const persist_control_macros_t *macros)
{
    project_fixture_t *fixture = context;
    if ((fixture == NULL) || (macros == NULL)) return 0U;
    fixture->macros = *macros;
    fixture->macros_applied = 1U;
    return 1U;
}

static uint8_t project_put_pattern(void *context,
                                   const persist_control_pattern_record_t *record)
{
    project_fixture_t *fixture = context;
    if ((fixture == NULL) || (record == NULL)) return 0U;
    fixture->record = *record;
    fixture->record_applied = 1U;
    return 1U;
}

static uint8_t project_commit(void *context)
{
    project_fixture_t *fixture = context;
    if (fixture == NULL) return 0U;
    fixture->committed = 1U;
    return 1U;
}

static void project_abort(void *context)
{
    project_fixture_t *fixture = context;
    if (fixture != NULL) fixture->committed = 0U;
}

static int project_roundtrip(void)
{
    const uint32_t capacity = 2U * 1024U * 1024U;
    project_fixture_t *input = calloc(1U, sizeof(*input));
    project_fixture_t *output = calloc(1U, sizeof(*output));
    persist_codec_project_workspace_t *workspace = calloc(1U, sizeof(*workspace));
    uint8_t *bytes = malloc(capacity);
    uint8_t *incremental = malloc(capacity);
    if ((input == NULL) || (output == NULL) || (workspace == NULL)
            || (bytes == NULL) || (incremental == NULL))
        return 1;

    initialize_pattern(&input->working, 1U);
    initialize_pattern(&input->record.content, 0U);
    input->record.bank = 2U;
    input->record.pattern = 3U;
    input->record.present = 1U;
    input->macros.hall_switch_key = PERSIST_MACRO_HALL_SCENE;

    persist_codec_project_source_t project = {
        .metadata = {
            .active_pattern_bank = 2U,
            .active_pattern = 3U,
            .pattern_count = 1U,
            .asset_count = 0U
        },
        .working_pattern = {project_working_get, input},
        .assets = {0U, project_asset_get, input},
        .macros = &input->macros,
        .patterns = {project_pattern_get, input}
    };
    memory_stream_t stream = {.bytes = bytes, .capacity = capacity};
    const persist_codec_sink_t sink = {memory_write, &stream};
    const persist_codec_source_t source = {
        memory_read, memory_reset, memory_size, &stream
    };
    persist_codec_project_consumer_t project_consumer = {
        project_begin, project_validate_asset, project_put_asset,
        project_apply_working, project_apply_macros, output
    };
    persist_codec_pattern_consumer_t pattern_consumer = {
        project_begin, project_put_pattern, project_commit, project_abort, output
    };

    int result = 0;
    const persist_codec_result_t encode =
        persist_codec_encode_project(&project, &sink, NULL);
    if (encode != PERSIST_CODEC_OK) result = 100 + (int)encode;
    if (result == 0)
    {
        memory_stream_t emitted = {
            .bytes = incremental,
            .capacity = capacity,
            .size = PERSIST_CODEC_HEADER_BYTES
        };
        const persist_codec_sink_t emitted_sink = {memory_write, &emitted};
        uint32_t section_start = emitted.size;
        emitted.size += 8U;
        uint32_t payload_start = emitted.size;
        uint32_t payload_bytes = 0U;
        persist_codec_result_t fragment =
            persist_codec_encode_project_core_payload(
                &project.metadata, &input->working,
                &emitted_sink, &payload_bytes);
        if ((fragment != PERSIST_CODEC_OK)
                || (payload_bytes != emitted.size - payload_start)
                || (persist_codec_build_project_section_header(
                        PERSIST_CODEC_PROJECT_SECTION_CORE, payload_bytes,
                        &incremental[section_start]) == 0U))
            result = 400;

        section_start = emitted.size;
        emitted.size += 8U;
        payload_start = emitted.size;
        fragment = persist_codec_encode_project_assets_payload(
            NULL, 0U, &emitted_sink, &payload_bytes);
        if ((result == 0) && ((fragment != PERSIST_CODEC_OK)
                || (payload_bytes != emitted.size - payload_start)
                || (persist_codec_build_project_section_header(
                        PERSIST_CODEC_PROJECT_SECTION_ASSETS, payload_bytes,
                        &incremental[section_start]) == 0U)))
            result = 401;

        section_start = emitted.size;
        emitted.size += 8U;
        payload_start = emitted.size;
        fragment = persist_codec_encode_project_macros_payload(
            &input->macros, &emitted_sink, &payload_bytes);
        if ((result == 0) && ((fragment != PERSIST_CODEC_OK)
                || (payload_bytes != emitted.size - payload_start)
                || (persist_codec_build_project_section_header(
                        PERSIST_CODEC_PROJECT_SECTION_MACROS, payload_bytes,
                        &incremental[section_start]) == 0U)))
            result = 402;

        section_start = emitted.size;
        emitted.size += 8U;
        payload_start = emitted.size;
        const uint8_t pattern_count[2] = {1U, 0U};
        if (memory_write(&emitted, pattern_count, sizeof(pattern_count)) == 0U)
            result = 403;
        fragment = persist_codec_encode_project_pattern_record_payload(
            &input->record, &emitted_sink, &payload_bytes);
        payload_bytes = emitted.size - payload_start;
        if ((result == 0) && ((fragment != PERSIST_CODEC_OK)
                || (persist_codec_build_project_section_header(
                        PERSIST_CODEC_PROJECT_SECTION_BANK, payload_bytes,
                        &incremental[section_start]) == 0U)))
            result = 404;

        const uint32_t crc = ~persist_codec_crc32_update(
            0xFFFFFFFFUL, &incremental[PERSIST_CODEC_HEADER_BYTES],
            emitted.size - PERSIST_CODEC_HEADER_BYTES);
        if ((result == 0)
                && ((persist_codec_build_project_document_header(
                        emitted.size, crc, incremental) == 0U)
                    || (emitted.size != stream.size)
                    || (memcmp(incremental, bytes, stream.size) != 0)))
            result = 405;
    }
    if (result == 0)
    {
        const persist_codec_result_t decode = persist_codec_decode_project_progressive(
            &source, workspace, &project_consumer, &pattern_consumer);
        if (decode != PERSIST_CODEC_OK) result = 200 + (int)decode;
    }
    if ((result == 0)
            && ((output->working_applied == 0U)
                || (output->macros_applied == 0U)
                || (output->record_applied == 0U)
                || (output->committed == 0U)
                || (output->metadata.active_pattern_bank != 2U)
                || (output->metadata.active_pattern != 3U)
                || (memcmp(&input->working, &output->working,
                           sizeof(input->working)) != 0)
                || (memcmp(&input->record, &output->record,
                           sizeof(input->record)) != 0)
                || (memcmp(&input->macros, &output->macros,
                           sizeof(input->macros)) != 0)))
        result = 300;

    free(bytes);
    free(incremental);
    free(workspace);
    free(output);
    free(input);
    return result;
}

static int key_catalog_contract(void)
{
    persist_param_descriptor_t model;
    persist_param_descriptor_t p1;
    persist_param_descriptor_t p1_b;
    if ((persist_key_param_descriptor(PARAM_AUDIO_FX_MODEL, &model) == 0U)
            || (persist_key_param_descriptor(PARAM_AUDIO_FX_P1, &p1) == 0U)
            || (persist_key_param_descriptor(PARAM_AUDIO_FX_B_P1, &p1_b) == 0U)
            || (model.plockable != 0U) || (p1.plockable == 0U)
            || (p1_b.plockable == 0U))
        return 40000;
    for (param_id_t id = 0U; id < PARAM_COUNT; ++id)
    {
        persist_param_descriptor_t descriptor;
        if (persist_key_param_descriptor(id, &descriptor) == 0U) continue;
        if ((descriptor.persistent == 0U) && (descriptor.plockable == 0U)) continue;
        param_id_t decoded = PARAM_COUNT;
        if (descriptor.key == PERSIST_CONTROL_KEY_NONE)
            return 10000 + (int)id;
        if (persist_key_param_from_disk(descriptor.key, &decoded) == 0U)
            return 20000 + (int)id;
        if (decoded != id)
            return 30000 + (int)id;
    }
    return 0;
}

int main(void)
{
    if (!topology_contract()) return 1;
    const int boundary_error = boundary_rejections();
    if (boundary_error != 0) return 100 + boundary_error;
    const int key_error = key_catalog_contract();
    if (key_error != 0) return key_error;
    const int plain_error = pattern_roundtrip(0U);
    if (plain_error != 0) return 1000 + plain_error;
    const int group_error = pattern_roundtrip(1U);
    if (group_error != 0) return 2000 + group_error;
    const int project_error = project_roundtrip();
    if (project_error != 0) return 3000 + project_error;
    return 0;
}
