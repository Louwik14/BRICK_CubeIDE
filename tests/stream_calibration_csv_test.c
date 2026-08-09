#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Core/stream_calibration_csv.h"

int main(void)
{
    brick6_stream_calibration_result_t r;
    memset(&r, 0, sizeof(r));
    r.page_kib = 32U;
    r.passes = 1U;
    r.advance_pages = 3U;
    r.passed = 1U;
    r.underruns = 7U;
    r.first_fault_voice_id = 6U;
    r.first_fault_voice_generation = 0x89ABCDEFU;
    r.first_fault_source_id = 0x76543210U;
    r.first_fault_page = 1234U;
    r.service_cycles_total = UINT64_C(0x100000001);
    r.source_bytes = UINT64_C(0x200000002);
    r.read_bytes = UINT64_C(0x300000003);
    r.active_multi_current = 8U;
    r.active_multi_peak = 8U;
    r.active_voice_seen_mask = 0xFFU;
    r.voice_generation_changed_mask = 0xA5U;
    r.active_voice_incarnations = 9U;
    r.margin_valid_mask = 0xFFU;
    r.full_rounds_at_8_voices = UINT64_C(0x400000004);
    r.eight_voice_valid = 1U;
    r.sd_diskio_dma_ops = 77U;
    r.sd_diskio_read_bytes = 39424U;

    char line[768];
    assert(brick6_stream_calibration_format_csv_row(
               line, sizeof(line), &r, 2U, 1U) > 0);

    const char *columns[51];
    uint32_t count = 0U;
    char *token = strtok(line, ",\r\n");
    while ((token != NULL) && (count < 51U))
    {
        columns[count++] = token;
        token = strtok(NULL, ",\r\n");
    }
    assert(count == 51U);
    assert(token == NULL);
    assert(strcmp(columns[0], "3") == 0);
    assert(strcmp(columns[10], "6") == 0);
    assert(strcmp(columns[11], "2309737967") == 0);
    assert(strcmp(columns[12], "1985229328") == 0);
    assert(strcmp(columns[37], "4294967297") == 0);
    assert(strcmp(columns[39], "8589934594") == 0);
    assert(strcmp(columns[40], "12884901891") == 0);
    assert(strcmp(columns[42], "8") == 0);
    assert(strcmp(columns[43], "255") == 0);
    assert(strcmp(columns[47], "17179869188") == 0);
    assert(strcmp(columns[48], "1") == 0);
    assert(strcmp(columns[49], "77") == 0);
    assert(strcmp(columns[50], "39424") == 0);
    return 0;
}
