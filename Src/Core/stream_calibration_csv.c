#include "Core/stream_calibration_csv.h"

#include <inttypes.h>
#include <stdio.h>

/* newlib-nano is linked without long-long printf support. Keep uint64_t values
 * decimal and complete without depending on the variadic formatter ABI. */
static void cal_u64_decimal(uint64_t value, char out[21])
{
    char reversed[20];
    uint8_t count = 0U;
    do
    {
        reversed[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);
    for (uint8_t i = 0U; i < count; ++i)
    {
        out[i] = reversed[count - 1U - i];
    }
    out[count] = '\0';
}

int brick6_stream_calibration_format_csv_row(
    char *out,
    size_t out_size,
    const brick6_stream_calibration_result_t *r,
    uint16_t scenario,
    uint8_t saved)
{
    if ((out == NULL) || (out_size == 0U) || (r == NULL))
    {
        return -1;
    }
    char storage_cycles[21];
    char source_bytes[21];
    char read_bytes[21];
    char rounds_at_eight[21];
    cal_u64_decimal(r->service_cycles_total, storage_cycles);
    cal_u64_decimal(r->source_bytes, source_bytes);
    cal_u64_decimal(r->read_bytes, read_bytes);
    cal_u64_decimal(r->full_rounds_at_8_voices, rounds_at_eight);

    return snprintf(out, out_size,
        "%" PRIu16 ",%" PRIu16 ",%u,%u,%u,%u,%" PRIu32 ",%u,%u,%" PRIu32
        ",%u,%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32
        ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32
        ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32
        ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32
        ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32
        ",%" PRIu32 ",%" PRIu32 ",%s,%" PRIu32 ",%s,%s,%u,%u,%u,%u,%" PRIu32
        ",%u,%s,%u,%" PRIu32 ",%" PRIu32 "\r\n",
        (uint16_t)(scenario + 1U), r->page_kib, r->passes, r->advance_pages,
        (unsigned)(r->page_kib * r->passes),
        (unsigned)(r->page_kib * r->advance_pages),
        (uint32_t)r->page_kib * r->passes * r->page_kib * r->advance_pages,
        saved, r->passed, r->underruns, r->first_fault_voice_id,
        r->first_fault_voice_generation, r->first_fault_source_id,
        r->first_fault_page, r->minimum_margin_frames,
        r->minimum_margin_per_voice[0], r->minimum_margin_per_voice[1],
        r->minimum_margin_per_voice[2], r->minimum_margin_per_voice[3],
        r->minimum_margin_per_voice[4], r->minimum_margin_per_voice[5],
        r->minimum_margin_per_voice[6], r->minimum_margin_per_voice[7],
        r->max_voice_service_gap_frames, r->round_cycles_average,
        r->round_cycles_max, r->sd_read_cycles_average,
        r->sd_read_cycles_p99_upper, r->sd_read_cycles_max,
        r->sd_bytes_per_second, r->pages_per_second_q16, r->pages_loaded,
        r->physical_reads, r->contiguous_reads, r->fatfs_reads, r->seeks,
        r->io_errors, storage_cycles, r->audio_irq_overruns,
        source_bytes, read_bytes, r->active_multi_current, r->active_multi_peak,
        r->active_voice_seen_mask, r->voice_generation_changed_mask,
        r->active_voice_incarnations, r->margin_valid_mask, rounds_at_eight,
        r->eight_voice_valid, r->sd_diskio_dma_ops, r->sd_diskio_read_bytes);
}
