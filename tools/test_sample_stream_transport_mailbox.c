#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Include the implementation so the test exercises its real private mailbox. */
#include "../Src/Sampler/sample_stream_transport.c"

static sample_page_load_target_t g_resolved_target;
static uint8_t g_resolve_loading_target = 1U;
static float g_page_cache[SAMPLE_PAGE_BYTES / sizeof(float)];

uint8_t sample_page_cache_resolve_loading_target(
    const sample_page_load_token_t *token,
    sample_page_load_target_t *out_target)
{
    (void)token;
    if ((g_resolve_loading_target == 0U) || (out_target == NULL))
    {
        return 0U;
    }
    *out_target = g_resolved_target;
    return 1U;
}

void sample_stream_io_release_key(sample_audio_key_t key)
{
    (void)key;
}

uint8_t sample_stream_io_begin_to(const sample_stream_io_command_t *command,
                                  float *decoded_frames,
                                  uint32_t decoded_capacity_bytes)
{
    (void)command;
    (void)decoded_frames;
    (void)decoded_capacity_bytes;
    return 0U;
}

uint8_t sample_stream_io_poll(sample_stream_io_result_t *out_result)
{
    (void)out_result;
    return 0U;
}

void sample_stream_physical_map_release(sample_stream_physical_map_t *map)
{
    (void)map;
}

void sample_stream_physical_map_pool_reset(void)
{
}

void sd_scheduler_runtime_service(void)
{
}

static void prepare_target(sample_stream_io_command_t *command,
                           uint32_t frame_count,
                           uint16_t stride_floats)
{
    memset(command, 0, sizeof(*command));
    memset(&g_resolved_target, 0, sizeof(g_resolved_target));
    command->token.key = sample_audio_key_classic(7U);
    command->token.page_index = 3U;
    command->token.page_generation = 19U;
    command->token.registration_epoch = 23U;
    command->token.slot_index = 5U;
    command->target.key = command->token.key;
    command->target.page_index = command->token.page_index;
    command->target.frame_count = frame_count;
    command->target.frames_per_page = (SAMPLE_PAGE_BYTES / sizeof(float))
        / stride_floats;
    command->target.page_generation = command->token.page_generation;
    command->target.registration_epoch = command->token.registration_epoch;
    command->target.slot_index = command->token.slot_index;
    command->target.stride_floats = stride_floats;
    g_resolved_target.key = command->target.key;
    g_resolved_target.page_index = command->target.page_index;
    g_resolved_target.frame_count = frame_count;
    g_resolved_target.frames_per_page = command->target.frames_per_page;
    g_resolved_target.page_generation = command->target.page_generation;
    g_resolved_target.registration_epoch = command->target.registration_epoch;
    g_resolved_target.slot_index = command->target.slot_index;
    g_resolved_target.stride_floats = stride_floats;
    g_resolved_target.frames_interleaved = g_page_cache;
    g_resolve_loading_target = 1U;
}

static sample_stream_transport_mailbox_t *submit_and_get_mailbox(
    const sample_stream_io_command_t *command,
    uint32_t *out_sequence)
{
    assert(sample_stream_transport_submit(command, out_sequence) != 0U);
    for (uint32_t i = 0U; i < SAMPLE_STREAM_TRANSPORT_MAILBOX_COUNT; ++i)
    {
        sample_stream_transport_mailbox_t *const mailbox =
            &g_sample_stream_transport_mailbox[i];
        if ((mailbox->state == SAMPLE_STREAM_TRANSPORT_COMMAND_READY)
            && (mailbox->sequence == *out_sequence))
        {
            return mailbox;
        }
    }
    assert(!"submitted mailbox not found");
    return NULL;
}

static void publish_test_result(sample_stream_transport_mailbox_t *mailbox,
                                sample_page_load_result_t result)
{
    mailbox->result.token = mailbox->command.token;
    mailbox->result.load_result = result;
    mailbox->result.source_bytes = 128U;
    mailbox->result.read_bytes = 128U;
    mailbox->state = SAMPLE_STREAM_TRANSPORT_RESULT_READY;
}

static void fill_payload(sample_stream_transport_mailbox_t *mailbox,
                         uint8_t value)
{
    memset(mailbox->decoded_page, value, sizeof(mailbox->decoded_page));
}

static void assert_payload_byte(const sample_stream_transport_mailbox_t *mailbox,
                                uint8_t value)
{
    for (uint32_t i = 0U; i < sizeof(mailbox->decoded_page); ++i)
    {
        assert(mailbox->decoded_page[i] == value);
    }
}

static void assert_metadata_reset(const sample_stream_transport_mailbox_t *mailbox)
{
    const uint8_t *const bytes = (const uint8_t *)mailbox;
    const size_t abi_begin = offsetof(sample_stream_transport_mailbox_t, abi_version);
    const size_t abi_end = abi_begin + sizeof(mailbox->abi_version);
    for (size_t i = 0U;
         i < offsetof(sample_stream_transport_mailbox_t, decoded_page); ++i)
    {
        if ((i < abi_begin) || (i >= abi_end))
        {
            assert(bytes[i] == 0U);
        }
    }
}

static void test_success_partial_copy_and_reuse(void)
{
    sample_stream_io_command_t command;
    prepare_target(&command, 4U, 2U);
    memset(g_page_cache, 0xA5, sizeof(g_page_cache));
    uint32_t sequence = 0U;
    sample_stream_transport_mailbox_t *mailbox =
        submit_and_get_mailbox(&command, &sequence);
    fill_payload(mailbox, 0x3C);
    publish_test_result(mailbox, SAMPLE_PAGE_LOAD_OK);

    sample_stream_io_result_t result;
    assert(sample_stream_transport_take_result(sequence, &result) != 0U);
    assert(result.load_result == SAMPLE_PAGE_LOAD_OK);
    for (uint32_t i = 0U; i < 8U; ++i)
    {
        const uint32_t bits = ((const uint8_t *)g_page_cache)[i];
        assert(bits == 0x3CU);
    }
    assert(((const uint8_t *)g_page_cache)[32] == 0xA5U);
    assert(mailbox->state == SAMPLE_STREAM_TRANSPORT_EMPTY);
    assert(mailbox->abi_version == SAMPLE_STREAM_TRANSPORT_ABI_VERSION);
    assert_metadata_reset(mailbox);
    assert_payload_byte(mailbox, 0x3CU);

    const uint32_t old_sequence = sequence;
    mailbox = submit_and_get_mailbox(&command, &sequence);
    assert(sequence != old_sequence);
    assert(mailbox->state == SAMPLE_STREAM_TRANSPORT_COMMAND_READY);
    assert_payload_byte(mailbox, 0x3CU);

    fill_payload(mailbox, 0x6DU); /* Simulated decoder overwrite on reuse. */
    publish_test_result(mailbox, SAMPLE_PAGE_LOAD_OK);
    memset(g_page_cache, 0, sizeof(g_page_cache));
    assert(sample_stream_transport_take_result(sequence, &result) != 0U);
    assert(result.load_result == SAMPLE_PAGE_LOAD_OK);
    assert(((const uint8_t *)g_page_cache)[0] == 0x6DU);
    assert(((const uint8_t *)g_page_cache)[32] == 0U);
    assert_payload_byte(mailbox, 0x6DU);
}

static void test_error_and_invalid_state_do_not_expose_payload(void)
{
    sample_stream_io_command_t command;
    prepare_target(&command, 8U, 2U);
    memset(g_page_cache, 0xA5, sizeof(g_page_cache));
    uint32_t sequence = 0U;
    sample_stream_transport_mailbox_t *mailbox =
        submit_and_get_mailbox(&command, &sequence);
    fill_payload(mailbox, 0x71U);
    publish_test_result(mailbox, SAMPLE_PAGE_LOAD_READ_FAILED);

    sample_stream_io_result_t result;
    assert(sample_stream_transport_take_result(sequence, &result) != 0U);
    assert(result.load_result == SAMPLE_PAGE_LOAD_READ_FAILED);
    assert(((const uint8_t *)g_page_cache)[0] == 0xA5U);
    assert_payload_byte(mailbox, 0x71U);
    assert(mailbox->state == SAMPLE_STREAM_TRANSPORT_EMPTY);
    assert(sample_stream_transport_take_result(sequence, &result) == 0U);
    assert(((const uint8_t *)g_page_cache)[0] == 0xA5U);
}

static void test_stale_generation_does_not_copy(void)
{
    sample_stream_io_command_t command;
    prepare_target(&command, 8U, 2U);
    memset(g_page_cache, 0xA5, sizeof(g_page_cache));
    uint32_t sequence = 0U;
    sample_stream_transport_mailbox_t *mailbox =
        submit_and_get_mailbox(&command, &sequence);
    fill_payload(mailbox, 0x82U);
    publish_test_result(mailbox, SAMPLE_PAGE_LOAD_OK);
    ++g_resolved_target.page_generation;

    sample_stream_io_result_t result;
    assert(sample_stream_transport_take_result(sequence, &result) != 0U);
    assert(result.load_result == SAMPLE_PAGE_LOAD_INVALID_ARG);
    assert(((const uint8_t *)g_page_cache)[0] == 0xA5U);
    assert_payload_byte(mailbox, 0x82U);
}

static void test_sequence_mismatch_does_not_expose_payload(void)
{
    sample_stream_io_command_t command;
    prepare_target(&command, 8U, 2U);
    memset(g_page_cache, 0xA5, sizeof(g_page_cache));
    uint32_t sequence = 0U;
    sample_stream_transport_mailbox_t *mailbox =
        submit_and_get_mailbox(&command, &sequence);
    fill_payload(mailbox, 0x93U);
    publish_test_result(mailbox, SAMPLE_PAGE_LOAD_OK);

    sample_stream_io_result_t result;
    assert(sample_stream_transport_take_result(sequence + 1U, &result) == 0U);
    assert(mailbox->state == SAMPLE_STREAM_TRANSPORT_RESULT_READY);
    assert(((const uint8_t *)g_page_cache)[0] == 0xA5U);
    assert_payload_byte(mailbox, 0x93U);
}

int main(void)
{
    assert(offsetof(sample_stream_transport_mailbox_t, decoded_page) == 416U);
    assert(sizeof(((sample_stream_transport_mailbox_t *)0)->decoded_page) == 32768U);
    assert(sizeof(sample_stream_transport_mailbox_t) == 33184U);
    sample_stream_transport_init();
    test_success_partial_copy_and_reuse();
    test_error_and_invalid_state_do_not_expose_payload();
    test_stale_generation_does_not_copy();
    test_sequence_mismatch_does_not_expose_payload();
    puts("Sample stream mailbox reset tests: PASS");
    return 0;
}
