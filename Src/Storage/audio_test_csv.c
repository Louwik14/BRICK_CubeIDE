#include "Storage/audio_test_csv.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Core/cpu_load.h"
#include "Core/track_runtime.h"
#include "Param/param_registry.h"
#include "Storage/sd_access_gate.h"
#include "fatfs.h"
#include "stm32h7xx_hal.h"

#define AUDIO_TEST_CSV_SCHEMA_VERSION 5U
#define AUDIO_TEST_CSV_PATH "0:/AUDIO_TEST.CSV"
#define AUDIO_TEST_CSV_V5_PATH "0:/AUDIO_TEST_V5.CSV"
#define AUDIO_TEST_CSV_LINE_MAX 6144U
#define AUDIO_TEST_CSV_TEXT_MAX 40U

static const char k_header[] =
    "schema_version,row_seq,run_id,automatic,test_index,test_total,test_phase,test_name,"
    "test_status,warmup_ms,measure_ms,uptime_ms,track,track_count,voice_count,notes,"
    "engine,models,sources,filter,fx,master,track_gain,pan,send1,send2,"
    "eng_peak,eng_peak_dbfs,eng_rms,eng_rms_dbfs,"
    "flt_in_peak,flt_in_peak_dbfs,flt_in_rms,flt_in_rms_dbfs,"
    "flt_out_peak,flt_out_peak_dbfs,flt_out_rms,flt_out_rms_dbfs,"
    "dsp_peak,dsp_peak_dbfs,dsp_rms,dsp_rms_dbfs,"
    "bus_peak,bus_peak_dbfs,bus_rms,bus_rms_dbfs,"
    "soft_clip_count,filter_clip_count,insert_clip_count,"
    "dry_peak_l,dry_peak_l_dbfs,dry_peak_r,dry_peak_r_dbfs,dry_rms_l,dry_rms_l_dbfs,dry_rms_r,dry_rms_r_dbfs,"
    "send1_peak_l,send1_peak_l_dbfs,send1_peak_r,send1_peak_r_dbfs,send1_rms_l,send1_rms_l_dbfs,send1_rms_r,send1_rms_r_dbfs,"
    "send2_peak_l,send2_peak_l_dbfs,send2_peak_r,send2_peak_r_dbfs,send2_rms_l,send2_rms_l_dbfs,send2_rms_r,send2_rms_r_dbfs,"
    "delay_return_peak_l,delay_return_peak_l_dbfs,delay_return_peak_r,delay_return_peak_r_dbfs,delay_return_rms_l,delay_return_rms_l_dbfs,delay_return_rms_r,delay_return_rms_r_dbfs,"
    "reverb_return_peak_l,reverb_return_peak_l_dbfs,reverb_return_peak_r,reverb_return_peak_r_dbfs,reverb_return_rms_l,reverb_return_rms_l_dbfs,reverb_return_rms_r,reverb_return_rms_r_dbfs,"
    "post_returns_peak_l,post_returns_peak_l_dbfs,post_returns_peak_r,post_returns_peak_r_dbfs,post_returns_rms_l,post_returns_rms_l_dbfs,post_returns_rms_r,post_returns_rms_r_dbfs,"
    "master_fx_in_peak_l,master_fx_in_peak_l_dbfs,master_fx_in_peak_r,master_fx_in_peak_r_dbfs,master_fx_in_rms_l,master_fx_in_rms_l_dbfs,master_fx_in_rms_r,master_fx_in_rms_r_dbfs,"
    "master_fx_out_peak_l,master_fx_out_peak_l_dbfs,master_fx_out_peak_r,master_fx_out_peak_r_dbfs,master_fx_out_rms_l,master_fx_out_rms_l_dbfs,master_fx_out_rms_r,master_fx_out_rms_r_dbfs,"
    "post_preview_peak_l,post_preview_peak_l_dbfs,post_preview_peak_r,post_preview_peak_r_dbfs,post_preview_rms_l,post_preview_rms_l_dbfs,post_preview_rms_r,post_preview_rms_r_dbfs,"
    "post_master_gain_peak_l,post_master_gain_peak_l_dbfs,post_master_gain_peak_r,post_master_gain_peak_r_dbfs,post_master_gain_rms_l,post_master_gain_rms_l_dbfs,post_master_gain_rms_r,post_master_gain_rms_r_dbfs,"
    "pre_pcm24_peak_l,pre_pcm24_peak_l_dbfs,pre_pcm24_peak_r,pre_pcm24_peak_r_dbfs,pre_pcm24_rms_l,pre_pcm24_rms_l_dbfs,pre_pcm24_rms_r,pre_pcm24_rms_r_dbfs,"
    "dma_main_peak_l,dma_main_peak_l_dbfs,dma_main_peak_r,dma_main_peak_r_dbfs,dma_main_rms_l,dma_main_rms_l_dbfs,dma_main_rms_r,dma_main_rms_r_dbfs,"
    "final_clip_count,final_clip_max_over,master_fx_clamp_count,master_fx_clamp_max_over,"
    "delay_clamp_count,delay_clamp_max_over,irq_valid,irq_current_permille,"
    "irq_average_permille,irq_peak_permille,irq_overrun_count,"
    "delay_send,reverb_send,delay_mix,delay_feedback,delay_time_index,"
    "reverb_mix,reverb_size,reverb_decay,reverb_damping,"
    "tail_early_wet_peak,tail_late_wet_peak,tail_cut_detected,"
    "tail_rising_detected,return_over_full_scale_count,nonfinite_count,"
    "final_saturation_detected,irq_overload_detected,headroom_exceeded,"
    "sum_expected_ratio,sum_peak_ratio,sum_rms_ratio,sum_progression_fail,"
    "row_type,sound_type,measurement_phase,cal_note,velocity,model_id,timbre,color,"
    "oscillator_count,oscillator_mode,repetition,cal_peak,cal_peak_dbfs,cal_rms,"
    "cal_rms_dbfs,k_weighted_mean,k_weighted_dbfs,crest_factor,dc_offset,"
    "cal_internal_clips,cal_samples,summary_observations,summary_weighted_median,"
    "summary_weighted_median_dbfs,summary_rms_median,summary_rms_median_dbfs,"
    "summary_peak_high,summary_peak_high_dbfs,summary_crest,summary_worst_dc,"
    "summary_total_clips,weakest_scenario,strongest_scenario,recommended_gain_db,"
    "remaining_headroom_db,cal_status\r\n";

typedef struct
{
    uint32_t run_id;
    uint32_t uptime_ms;
    uint16_t test_index;
    uint16_t test_total;
    uint8_t track;
    uint8_t track_count;
    uint8_t voice_count;
    char phase[16];
    char name[AUDIO_TEST_CSV_TEXT_MAX];
    char status[20];
    char notes[AUDIO_TEST_CSV_TEXT_MAX];
    char sources[AUDIO_TEST_CSV_TEXT_MAX];
    char filter[AUDIO_TEST_CSV_TEXT_MAX];
    char fx[AUDIO_TEST_CSV_TEXT_MAX];
    char master[AUDIO_TEST_CSV_TEXT_MAX];
    char engine[16];
    char models[48];
    uint32_t warmup_ms;
    uint32_t measure_ms;
    float track_gain;
    float pan;
    float send1;
    float send2;
    cpu_load_metrics_t cpu;
    float delay_send;
    float reverb_send;
    float delay_mix;
    float delay_feedback;
    float delay_time;
    float reverb_mix;
    float reverb_size;
    float reverb_decay;
    float reverb_damping;
    float tail_early_wet_peak;
    float tail_late_wet_peak;
    uint32_t return_over_full_scale_count;
    uint32_t nonfinite_count;
    uint8_t tail_cut_detected;
    uint8_t tail_rising_detected;
    uint8_t final_saturation_detected;
    uint8_t irq_overload_detected;
    uint8_t headroom_exceeded;
    float sum_expected_ratio;
    float sum_peak_ratio;
    float sum_rms_ratio;
    uint8_t sum_progression_fail;
    char row_type[16];
    char sound_type[24];
    char measurement_phase[16];
    uint8_t note;
    uint8_t velocity;
    uint8_t model_id;
    float timbre;
    float color;
    uint8_t oscillator_count;
    char oscillator_mode[24];
    uint8_t repetition;
    uint8_t summary;
    uint16_t summary_observations;
    float summary_weighted_median;
    float summary_rms_median;
    float summary_peak_high;
    float summary_crest;
    float summary_worst_dc;
    uint32_t summary_total_clips;
    char weakest_scenario[AUDIO_TEST_CSV_TEXT_MAX];
    char strongest_scenario[AUDIO_TEST_CSV_TEXT_MAX];
    float recommended_gain_db;
    float remaining_headroom_db;
    char calibration_status[24];
    audio_track_diag_snapshot_t track_diag;
    audio_global_diag_snapshot_t global_diag;
} audio_test_csv_request_t;

static audio_test_csv_request_t g_request;
static uint8_t g_session_pending;
static uint8_t g_request_pending;
static uint8_t g_ready;
static uint32_t g_sequence;
static volatile uint8_t g_result;
static const char *g_path = AUDIO_TEST_CSV_PATH;
static char g_line[AUDIO_TEST_CSV_LINE_MAX];
static char g_tail[1025];

static void copy_text(char *out, uint32_t size, const char *text)
{
    if ((out != 0) && (size != 0U))
    {
        (void)snprintf(out, size, "%s", (text != 0) ? text : "");
        for (uint32_t i = 0U; out[i] != '\0'; ++i)
        {
            if ((out[i] == ',') || (out[i] == '\r') || (out[i] == '\n'))
            {
                out[i] = '_';
            }
        }
    }
}

static float track_rms(const audio_track_diag_snapshot_t *snapshot,
                       audio_track_diag_stage_t stage)
{
    if ((snapshot == 0) || (stage >= AUDIO_TRACK_DIAG_STAGE_COUNT)
        || (snapshot->samples[stage] == 0U))
    {
        return 0.0f;
    }
    return sqrtf(snapshot->rms_energy[stage] / (float)snapshot->samples[stage]);
}

static float track_k_weighted_mean(
    const audio_track_diag_snapshot_t *snapshot,
    audio_track_diag_stage_t stage)
{
    if ((snapshot == 0) || (stage >= AUDIO_TRACK_DIAG_STAGE_COUNT)
        || (snapshot->samples[stage] == 0U))
    {
        return 0.0f;
    }
    return sqrtf(snapshot->k_weighted_energy[stage]
                 / (float)snapshot->samples[stage]);
}

static float track_dc(const audio_track_diag_snapshot_t *snapshot,
                      audio_track_diag_stage_t stage)
{
    if ((snapshot == 0) || (stage >= AUDIO_TRACK_DIAG_STAGE_COUNT)
        || (snapshot->samples[stage] == 0U))
    {
        return 0.0f;
    }
    return snapshot->signed_sum[stage] / (float)snapshot->samples[stage];
}

static float dbfs(float value)
{
    return (value > 0.000001f) ? (20.0f * log10f(value)) : -120.0f;
}

static float global_rms(const audio_global_diag_snapshot_t *snapshot,
                        audio_global_diag_stage_t stage,
                        uint8_t right)
{
    if ((snapshot == 0) || (stage >= AUDIO_GLOBAL_DIAG_STAGE_COUNT)
        || (snapshot->samples[stage] == 0U))
    {
        return 0.0f;
    }
    const float energy = (right != 0U)
        ? snapshot->energy_r[stage] : snapshot->energy_l[stage];
    return sqrtf(energy / (float)snapshot->samples[stage]);
}

static const char *engine_name(const track_runtime_ctx_t *ctx)
{
    if (ctx == 0)
    {
        return "INACTIVE";
    }
    switch ((track_runtime_engine_t)ctx->engine)
    {
        case TRACK_RUNTIME_ENGINE_AUDIO_TRACK: return "AUDIO";
        case TRACK_RUNTIME_ENGINE_SAMPLER: return "SAMPLER";
        case TRACK_RUNTIME_ENGINE_LOOPER: return "LOOPER";
        case TRACK_RUNTIME_ENGINE_PRISM: return "PRISM";
        case TRACK_RUNTIME_ENGINE_DRUM: return "DRUM";
        case TRACK_RUNTIME_ENGINE_STACK: return "STACK";
        case TRACK_RUNTIME_ENGINE_WAVE: return "WAVE";
        case TRACK_RUNTIME_ENGINE_DELUGE: return "DELUGE";
        default: return "INACTIVE";
    }
}

static int32_t track_param_raw(param_id_t id, uint8_t track)
{
    float value = -1.0f;
    return (param_registry_get_track_value(id, track, &value) != 0U)
        ? (int32_t)lroundf(value) : -1;
}

static float track_param(param_id_t id, uint8_t track)
{
    float value = 0.0f;
    (void)param_registry_get_track_value(id, track, &value);
    return value;
}

static void capture_models(char *out, uint32_t size, uint8_t track,
                           const track_runtime_ctx_t *ctx)
{
    if ((out == 0) || (size == 0U))
    {
        return;
    }
    out[0] = '\0';
    if (ctx == 0)
    {
        return;
    }
    switch ((track_runtime_engine_t)ctx->engine)
    {
        case TRACK_RUNTIME_ENGINE_PRISM:
            (void)snprintf(out, size, "%ld/%ld",
                           (long)track_param_raw(PARAM_PRISM_EDIT, track),
                           (long)track_param_raw(PARAM_PRISM_OSC2_EDIT, track));
            break;
        case TRACK_RUNTIME_ENGINE_STACK:
            (void)snprintf(out, size, "%ld/%ld/%ld",
                           (long)track_param_raw(PARAM_STACK_OSC1_MODEL, track),
                           (long)track_param_raw(PARAM_STACK_OSC2_MODEL, track),
                           (long)track_param_raw(PARAM_STACK_OSC3_MODEL, track));
            break;
        case TRACK_RUNTIME_ENGINE_DELUGE:
            (void)snprintf(out, size, "%ld",
                           (long)track_param_raw(PARAM_DELUGE_MODEL, track));
            break;
        case TRACK_RUNTIME_ENGINE_WAVE:
            (void)snprintf(out, size, "WT%ld/WT%ld",
                           (long)track_param_raw(PARAM_WAVE_OSC1_TABLE, track),
                           (long)track_param_raw(PARAM_WAVE_OSC2_TABLE, track));
            break;
        default:
            break;
    }
}

void audio_test_csv_init(void)
{
    memset(&g_request, 0, sizeof(g_request));
    g_session_pending = 0U;
    g_request_pending = 0U;
    g_ready = 0U;
    g_sequence = 0U;
    g_result = (uint8_t)AUDIO_TEST_CSV_RESULT_NONE;
    g_path = AUDIO_TEST_CSV_PATH;
}

void audio_test_csv_begin_session(void)
{
    if ((g_session_pending == 0U) && (g_request_pending == 0U))
    {
        g_session_pending = 1U;
        g_ready = 0U;
    }
}

uint8_t audio_test_csv_enqueue_auto(uint8_t track,
                                    const audio_test_csv_case_t *test_case,
                                    const audio_track_diag_snapshot_t *track_snapshot,
                                    const audio_global_diag_snapshot_t *global_snapshot)
{
    if ((track >= UI_TRACK_COUNT) || (test_case == 0) || (track_snapshot == 0)
        || (global_snapshot == 0) || (g_ready == 0U)
        || (g_request_pending != 0U))
    {
        return 0U;
    }

    memset(&g_request, 0, sizeof(g_request));
    g_request.run_id = test_case->run_id;
    g_request.uptime_ms = HAL_GetTick();
    g_request.test_index = test_case->test_index;
    g_request.test_total = test_case->test_total;
    g_request.track = track;
    g_request.track_count = test_case->track_count;
    g_request.voice_count = test_case->voice_count;
    g_request.warmup_ms = test_case->warmup_ms;
    g_request.measure_ms = test_case->measure_ms;
    copy_text(g_request.phase, sizeof(g_request.phase), test_case->test_phase);
    copy_text(g_request.name, sizeof(g_request.name), test_case->test_name);
    copy_text(g_request.status, sizeof(g_request.status), test_case->test_status);
    copy_text(g_request.notes, sizeof(g_request.notes), test_case->notes);
    copy_text(g_request.sources, sizeof(g_request.sources), test_case->source_config);
    copy_text(g_request.filter, sizeof(g_request.filter), test_case->filter_config);
    copy_text(g_request.fx, sizeof(g_request.fx), test_case->fx_config);
    copy_text(g_request.master, sizeof(g_request.master), test_case->master_config);

    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    copy_text(g_request.engine, sizeof(g_request.engine), engine_name(ctx));
    capture_models(g_request.models, sizeof(g_request.models), track, ctx);
    g_request.track_gain = track_param(PARAM_MIX_LEVEL, track);
    g_request.pan = track_param(PARAM_MIX_PAN, track);
    g_request.send1 = track_param(PARAM_MIX_SEND1, track);
    g_request.send2 = track_param(PARAM_MIX_SEND2, track);
    g_request.cpu = test_case->cpu_metrics;
    g_request.delay_send = test_case->delay_send;
    g_request.reverb_send = test_case->reverb_send;
    g_request.delay_mix = test_case->delay_mix;
    g_request.delay_feedback = test_case->delay_feedback;
    g_request.delay_time = test_case->delay_time;
    g_request.reverb_mix = test_case->reverb_mix;
    g_request.reverb_size = test_case->reverb_size;
    g_request.reverb_decay = test_case->reverb_decay;
    g_request.reverb_damping = test_case->reverb_damping;
    g_request.tail_early_wet_peak = test_case->tail_early_wet_peak;
    g_request.tail_late_wet_peak = test_case->tail_late_wet_peak;
    g_request.return_over_full_scale_count =
        test_case->return_over_full_scale_count;
    g_request.nonfinite_count = test_case->nonfinite_count;
    g_request.tail_cut_detected = test_case->tail_cut_detected;
    g_request.tail_rising_detected = test_case->tail_rising_detected;
    g_request.final_saturation_detected =
        test_case->final_saturation_detected;
    g_request.irq_overload_detected = test_case->irq_overload_detected;
    g_request.headroom_exceeded = test_case->headroom_exceeded;
    g_request.sum_expected_ratio = test_case->sum_expected_ratio;
    g_request.sum_peak_ratio = test_case->sum_peak_ratio;
    g_request.sum_rms_ratio = test_case->sum_rms_ratio;
    g_request.sum_progression_fail = test_case->sum_progression_fail;
    copy_text(g_request.row_type, sizeof(g_request.row_type),
              test_case->row_type);
    copy_text(g_request.sound_type, sizeof(g_request.sound_type),
              test_case->sound_type);
    copy_text(g_request.measurement_phase,
              sizeof(g_request.measurement_phase),
              test_case->measurement_phase);
    g_request.note = test_case->note;
    g_request.velocity = test_case->velocity;
    g_request.model_id = test_case->model_id;
    g_request.timbre = test_case->timbre;
    g_request.color = test_case->color;
    g_request.oscillator_count = test_case->oscillator_count;
    copy_text(g_request.oscillator_mode, sizeof(g_request.oscillator_mode),
              test_case->oscillator_mode);
    g_request.repetition = test_case->repetition;
    copy_text(g_request.calibration_status,
              sizeof(g_request.calibration_status),
              test_case->test_status);
    g_request.track_diag = *track_snapshot;
    g_request.global_diag = *global_snapshot;
    g_request_pending = 1U;
    return 1U;
}

uint8_t audio_test_csv_enqueue_summary(
    const audio_test_csv_summary_t *summary)
{
    if ((summary == 0) || (g_ready == 0U) || (g_request_pending != 0U))
    {
        return 0U;
    }
    memset(&g_request, 0, sizeof(g_request));
    g_request.run_id = summary->run_id;
    g_request.uptime_ms = HAL_GetTick();
    g_request.test_total = summary->test_total;
    g_request.summary = 1U;
    g_request.model_id = summary->model_id;
    g_request.summary_observations = summary->observation_count;
    g_request.summary_weighted_median = summary->weighted_median;
    g_request.summary_rms_median = summary->rms_median;
    g_request.summary_peak_high = summary->peak_high;
    g_request.summary_crest = summary->crest_representative;
    g_request.summary_worst_dc = summary->worst_dc;
    g_request.summary_total_clips = summary->total_clips;
    g_request.recommended_gain_db = summary->recommended_gain_db;
    g_request.remaining_headroom_db = summary->remaining_headroom_db;
    copy_text(g_request.phase, sizeof(g_request.phase), "CAL_SUMMARY");
    copy_text(g_request.row_type, sizeof(g_request.row_type), "CAL_SUMMARY");
    copy_text(g_request.name, sizeof(g_request.name), summary->model_name);
    copy_text(g_request.models, sizeof(g_request.models), summary->model_name);
    copy_text(g_request.engine, sizeof(g_request.engine), summary->engine);
    copy_text(g_request.sound_type, sizeof(g_request.sound_type),
              summary->sound_type);
    copy_text(g_request.status, sizeof(g_request.status), summary->status);
    copy_text(g_request.calibration_status,
              sizeof(g_request.calibration_status), summary->status);
    copy_text(g_request.weakest_scenario,
              sizeof(g_request.weakest_scenario), summary->weakest_scenario);
    copy_text(g_request.strongest_scenario,
              sizeof(g_request.strongest_scenario), summary->strongest_scenario);
    g_request_pending = 1U;
    return 1U;
}

static uint8_t write_all(FIL *file, const char *data, uint32_t size)
{
    UINT written = 0U;
    return ((file != 0) && (data != 0)
        && (f_write(file, data, (UINT)size, &written) == FR_OK)
        && (written == (UINT)size)) ? 1U : 0U;
}

static uint8_t file_header_matches(FIL *file)
{
    if ((file == 0) || (f_size(file) < (FSIZE_t)(sizeof(k_header) - 1U))
        || (f_lseek(file, 0U) != FR_OK))
    {
        return 0U;
    }
    char probe[sizeof(k_header)];
    UINT read = 0U;
    if ((f_read(file, probe, (UINT)(sizeof(k_header) - 1U), &read) != FR_OK)
        || (read != (UINT)(sizeof(k_header) - 1U)))
    {
        return 0U;
    }
    return (memcmp(probe, k_header, sizeof(k_header) - 1U) == 0) ? 1U : 0U;
}

static uint32_t read_last_sequence(FIL *file)
{
    if (file == 0)
    {
        return 0U;
    }
    const FSIZE_t size = f_size(file);
    const UINT amount = (size > 1024U) ? 1024U : (UINT)size;
    if ((amount == 0U) || (f_lseek(file, size - amount) != FR_OK))
    {
        return 0U;
    }
    UINT read = 0U;
    if (f_read(file, g_tail, amount, &read) != FR_OK)
    {
        return 0U;
    }
    g_tail[read] = '\0';
    char *last = g_tail;
    for (UINT i = 0U; i + 1U < read; ++i)
    {
        if ((g_tail[i] == '\n') && (g_tail[i + 1U] != '\0'))
        {
            last = &g_tail[i + 1U];
        }
    }
    char *const first_comma = strchr(last, ',');
    return (first_comma != 0)
        ? (uint32_t)strtoul(first_comma + 1, 0, 10) : 0U;
}

static uint8_t prepare_path(const char *path, uint8_t allow_fallback)
{
    FIL file;
    if (f_open(&file, path, FA_OPEN_ALWAYS | FA_READ | FA_WRITE) != FR_OK)
    {
        return 0U;
    }
    uint8_t ok = 1U;
    if (f_size(&file) == 0U)
    {
        ok = write_all(&file, k_header, (uint32_t)(sizeof(k_header) - 1U));
        if ((ok != 0U) && (f_sync(&file) != FR_OK))
        {
            ok = 0U;
        }
    }
    else if (file_header_matches(&file) == 0U)
    {
        ok = 0U;
    }
    else
    {
        g_sequence = read_last_sequence(&file);
    }
    if (f_close(&file) != FR_OK)
    {
        ok = 0U;
    }
    if ((ok == 0U) && (allow_fallback != 0U))
    {
        return prepare_path(AUDIO_TEST_CSV_V5_PATH, 0U);
    }
    if (ok != 0U)
    {
        g_path = path;
    }
    return ok;
}

static uint8_t appendf(uint32_t *offset, const char *format, ...)
{
    if ((offset == 0) || (*offset >= sizeof(g_line)))
    {
        return 0U;
    }
    va_list args;
    va_start(args, format);
    const int count = vsnprintf(&g_line[*offset], sizeof(g_line) - *offset, format, args);
    va_end(args);
    if ((count < 0) || ((uint32_t)count >= (sizeof(g_line) - *offset)))
    {
        return 0U;
    }
    *offset += (uint32_t)count;
    return 1U;
}

static uint8_t format_request(uint32_t row_sequence, uint32_t *out_length)
{
    const audio_test_csv_request_t *const r = &g_request;
    uint32_t offset = 0U;
    if (appendf(&offset,
        "%u,%lu,%lu,1,%u,%u,%s,%s,%s,%lu,%lu,%lu,%u,%u,%u,%s,%s,%s,%s,%s,%s,%s,"
        "%.9g,%.9g,%.9g,%.9g,"
        "%.9g,%.4f,%.9g,%.4f,%.9g,%.4f,%.9g,%.4f,"
        "%.9g,%.4f,%.9g,%.4f,%.9g,%.4f,%.9g,%.4f,"
        "%.9g,%.4f,%.9g,%.4f,%lu,%lu,%lu",
        AUDIO_TEST_CSV_SCHEMA_VERSION, (unsigned long)row_sequence,
        (unsigned long)r->run_id, (unsigned)r->test_index,
        (unsigned)r->test_total, r->phase, r->name, r->status,
        (unsigned long)r->warmup_ms, (unsigned long)r->measure_ms,
        (unsigned long)r->uptime_ms, (unsigned)(r->track + 1U),
        (unsigned)r->track_count, (unsigned)r->voice_count, r->notes,
        r->engine, r->models, r->sources, r->filter, r->fx, r->master,
        r->track_gain, r->pan, r->send1, r->send2,
        r->track_diag.peak[AUDIO_TRACK_DIAG_ENG],
        dbfs(r->track_diag.peak[AUDIO_TRACK_DIAG_ENG]),
        track_rms(&r->track_diag, AUDIO_TRACK_DIAG_ENG),
        dbfs(track_rms(&r->track_diag, AUDIO_TRACK_DIAG_ENG)),
        r->track_diag.peak[AUDIO_TRACK_DIAG_FILTER_IN],
        dbfs(r->track_diag.peak[AUDIO_TRACK_DIAG_FILTER_IN]),
        track_rms(&r->track_diag, AUDIO_TRACK_DIAG_FILTER_IN),
        dbfs(track_rms(&r->track_diag, AUDIO_TRACK_DIAG_FILTER_IN)),
        r->track_diag.peak[AUDIO_TRACK_DIAG_FILTER_OUT],
        dbfs(r->track_diag.peak[AUDIO_TRACK_DIAG_FILTER_OUT]),
        track_rms(&r->track_diag, AUDIO_TRACK_DIAG_FILTER_OUT),
        dbfs(track_rms(&r->track_diag, AUDIO_TRACK_DIAG_FILTER_OUT)),
        r->track_diag.peak[AUDIO_TRACK_DIAG_DSP],
        dbfs(r->track_diag.peak[AUDIO_TRACK_DIAG_DSP]),
        track_rms(&r->track_diag, AUDIO_TRACK_DIAG_DSP),
        dbfs(track_rms(&r->track_diag, AUDIO_TRACK_DIAG_DSP)),
        r->track_diag.peak[AUDIO_TRACK_DIAG_BUS],
        dbfs(r->track_diag.peak[AUDIO_TRACK_DIAG_BUS]),
        track_rms(&r->track_diag, AUDIO_TRACK_DIAG_BUS),
        dbfs(track_rms(&r->track_diag, AUDIO_TRACK_DIAG_BUS)),
        (unsigned long)r->track_diag.soft_clip_count,
        (unsigned long)r->track_diag.filter_clip_count,
        (unsigned long)r->track_diag.insert_clip_count) == 0U)
    {
        return 0U;
    }

    for (uint8_t stage = 0U; stage < AUDIO_GLOBAL_DIAG_STAGE_COUNT; ++stage)
    {
        const float peak_l = r->global_diag.peak_l[stage];
        const float peak_r = r->global_diag.peak_r[stage];
        const float rms_l =
            global_rms(&r->global_diag, (audio_global_diag_stage_t)stage, 0U);
        const float rms_r =
            global_rms(&r->global_diag, (audio_global_diag_stage_t)stage, 1U);
        if (appendf(&offset, ",%.9g,%.4f,%.9g,%.4f,%.9g,%.4f,%.9g,%.4f",
                    peak_l, dbfs(peak_l), peak_r, dbfs(peak_r),
                    rms_l, dbfs(rms_l), rms_r, dbfs(rms_r)) == 0U)
        {
            return 0U;
        }
    }
    if (appendf(&offset,
                ",%lu,%.9g,%lu,%.9g,%lu,%.9g,%lu,%lu,%lu,%lu,%lu,"
                "%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,"
                "%.9g,%.9g,%u,%u,%lu,%lu,%u,%u,%u,"
                "%.9g,%.9g,%.9g,%u",
                (unsigned long)r->global_diag.final_clip_count,
                r->global_diag.final_clip_max_over,
                (unsigned long)r->global_diag.master_fx_clamp_count,
                r->global_diag.master_fx_clamp_max_over,
                (unsigned long)r->global_diag.delay_clamp_count,
                r->global_diag.delay_clamp_max_over,
                (unsigned long)r->cpu.counter_valid,
                (unsigned long)r->cpu.last_permille,
                (unsigned long)r->cpu.avg_permille,
                (unsigned long)r->cpu.peak_permille,
                (unsigned long)r->cpu.over_100_count,
                r->delay_send, r->reverb_send, r->delay_mix,
                r->delay_feedback, r->delay_time, r->reverb_mix,
                r->reverb_size, r->reverb_decay, r->reverb_damping,
                r->tail_early_wet_peak, r->tail_late_wet_peak,
                (unsigned)r->tail_cut_detected,
                (unsigned)r->tail_rising_detected,
                (unsigned long)r->return_over_full_scale_count,
                (unsigned long)r->nonfinite_count,
                (unsigned)r->final_saturation_detected,
                (unsigned)r->irq_overload_detected,
                (unsigned)r->headroom_exceeded,
                r->sum_expected_ratio, r->sum_peak_ratio, r->sum_rms_ratio,
                (unsigned)r->sum_progression_fail) == 0U)
    {
        return 0U;
    }
    const float cal_peak = r->track_diag.peak[AUDIO_TRACK_DIAG_ENG];
    const float cal_rms = track_rms(&r->track_diag, AUDIO_TRACK_DIAG_ENG);
    const float cal_weighted =
        track_k_weighted_mean(&r->track_diag, AUDIO_TRACK_DIAG_ENG);
    const float cal_crest = (cal_rms > 0.0000001f)
        ? (cal_peak / cal_rms) : 0.0f;
    const float cal_dc = track_dc(&r->track_diag, AUDIO_TRACK_DIAG_ENG);
    const uint32_t cal_clips =
        r->track_diag.soft_clip_count
        + r->track_diag.filter_clip_count
        + r->track_diag.insert_clip_count
        + r->global_diag.final_clip_count
        + r->global_diag.master_fx_clamp_count
        + r->global_diag.delay_clamp_count;
    if (appendf(&offset,
                ",%s,%s,%s,%u,%u,%u,%.9g,%.9g,%u,%s,%u,"
                "%.9g,%.4f,%.9g,%.4f,%.9g,%.4f,%.9g,%.9g,%lu,%lu,"
                "%u,%.9g,%.4f,%.9g,%.4f,%.9g,%.4f,%.9g,%.9g,%lu,"
                "%s,%s,%.4f,%.4f,%s\r\n",
                r->row_type, r->sound_type, r->measurement_phase,
                (unsigned)r->note, (unsigned)r->velocity,
                (unsigned)r->model_id, r->timbre, r->color,
                (unsigned)r->oscillator_count, r->oscillator_mode,
                (unsigned)r->repetition,
                cal_peak, dbfs(cal_peak), cal_rms, dbfs(cal_rms),
                cal_weighted, dbfs(cal_weighted), cal_crest, cal_dc,
                (unsigned long)cal_clips,
                (unsigned long)r->track_diag.samples[AUDIO_TRACK_DIAG_ENG],
                (unsigned)r->summary_observations,
                r->summary_weighted_median,
                dbfs(r->summary_weighted_median),
                r->summary_rms_median, dbfs(r->summary_rms_median),
                r->summary_peak_high, dbfs(r->summary_peak_high),
                r->summary_crest, r->summary_worst_dc,
                (unsigned long)r->summary_total_clips,
                r->weakest_scenario, r->strongest_scenario,
                r->recommended_gain_db, r->remaining_headroom_db,
                r->calibration_status) == 0U)
    {
        return 0U;
    }
    *out_length = offset;
    return 1U;
}

static uint8_t append_request(void)
{
    uint32_t length = 0U;
    const uint32_t next = g_sequence + 1U;
    if (format_request(next, &length) == 0U)
    {
        return 0U;
    }
    FIL file;
    if (f_open(&file, g_path, FA_OPEN_ALWAYS | FA_WRITE) != FR_OK)
    {
        return 0U;
    }
    uint8_t ok = ((f_lseek(&file, f_size(&file)) == FR_OK)
        && (write_all(&file, g_line, length) != 0U)
        && (f_sync(&file) == FR_OK)) ? 1U : 0U;
    if (f_close(&file) != FR_OK)
    {
        ok = 0U;
    }
    if (ok != 0U)
    {
        g_sequence = next;
    }
    return ok;
}

void audio_test_csv_service(void)
{
    if ((g_session_pending == 0U) && (g_request_pending == 0U))
    {
        return;
    }
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_AUDIO_TEST) == 0U)
    {
        return;
    }
    if (sd_access_fs_mount_if_needed() == 0U)
    {
        g_ready = 0U;
        g_session_pending = 0U;
        g_request_pending = 0U;
        g_result = (uint8_t)AUDIO_TEST_CSV_RESULT_ERROR;
        sd_access_gate_release(SD_ACCESS_CLIENT_AUDIO_TEST);
        return;
    }
    if (g_session_pending != 0U)
    {
        g_ready = prepare_path(AUDIO_TEST_CSV_PATH, 1U);
        g_session_pending = 0U;
        g_result = (uint8_t)((g_ready != 0U)
            ? AUDIO_TEST_CSV_RESULT_SESSION_OK : AUDIO_TEST_CSV_RESULT_ERROR);
    }
    else
    {
        const uint8_t ok = append_request();
        g_request_pending = 0U;
        g_result = (uint8_t)((ok != 0U)
            ? AUDIO_TEST_CSV_RESULT_ROW_OK : AUDIO_TEST_CSV_RESULT_ERROR);
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_AUDIO_TEST);
}

audio_test_csv_result_t audio_test_csv_take_result(void)
{
    const uint8_t result = g_result;
    g_result = (uint8_t)AUDIO_TEST_CSV_RESULT_NONE;
    return (audio_test_csv_result_t)result;
}

uint8_t audio_test_csv_is_busy(void)
{
    return ((g_session_pending != 0U) || (g_request_pending != 0U)) ? 1U : 0U;
}
