#include "sd_multitrack_recorder.h"

#include <string.h>

#include "stm32h7xx_hal.h"

typedef enum
{
    CMD_NONE = 0,
    CMD_START,
    CMD_STOP,
    CMD_ARM_STEM,
    CMD_DISARM_STEM
} recorder_cmd_type_t;

typedef struct
{
    recorder_cmd_type_t type;
    uint8_t stem_id;
    sd_recorder_stem_cfg_t cfg;
} recorder_cmd_t;

typedef struct
{
    uint8_t armed;
    sd_recorder_stem_cfg_t cfg;
} recorder_stem_t;

#define REC_CMD_Q_LEN 16U

typedef struct
{
    recorder_cmd_t q[REC_CMD_Q_LEN];
    volatile uint32_t write_idx;
    volatile uint32_t read_idx;

    sd_recorder_state_t state;
    recorder_stem_t stems[SD_RECORDER_MAX_STEMS];

    uint32_t start_requests;
    uint32_t stop_requests;
    uint32_t arm_requests;
    uint32_t disarm_requests;
    uint32_t rejected_config_changes;
    uint32_t rejected_state_requests;
    uint32_t block_boundary_calls;
    uint32_t transition_count;
} recorder_ctx_t;

static recorder_ctx_t g_rec;

static uint8_t recorder_cmd_push(const recorder_cmd_t *cmd)
{
    if(cmd == 0)
        return 0U;

    uint8_t accepted = 0U;

    __disable_irq();

    const uint32_t next = (g_rec.write_idx + 1U) & (REC_CMD_Q_LEN - 1U);
    if(next != g_rec.read_idx)
    {
        g_rec.q[g_rec.write_idx] = *cmd;
        __DMB();
        g_rec.write_idx = next;
        accepted = 1U;
    }

    __enable_irq();

    return accepted;
}

static uint8_t recorder_cmd_pop(recorder_cmd_t *cmd)
{
    if(cmd == 0)
        return 0U;

    if(g_rec.read_idx == g_rec.write_idx)
        return 0U;

    *cmd = g_rec.q[g_rec.read_idx];
    g_rec.read_idx = (g_rec.read_idx + 1U) & (REC_CMD_Q_LEN - 1U);
    return 1U;
}

static void recorder_set_state(sd_recorder_state_t next)
{
    if(g_rec.state != next)
    {
        g_rec.state = next;
        g_rec.transition_count++;
    }
}

void sd_recorder_init(void)
{
    memset(&g_rec, 0, sizeof(g_rec));
    g_rec.state = SD_RECORDER_STATE_IDLE;
}

uint8_t sd_recorder_request_start(void)
{
    recorder_cmd_t cmd;
    cmd.type = CMD_START;
    cmd.stem_id = 0U;
    memset(&cmd.cfg, 0, sizeof(cmd.cfg));

    if(!recorder_cmd_push(&cmd))
        return 0U;

    g_rec.start_requests++;
    return 1U;
}

uint8_t sd_recorder_request_stop(void)
{
    recorder_cmd_t cmd;
    cmd.type = CMD_STOP;
    cmd.stem_id = 0U;
    memset(&cmd.cfg, 0, sizeof(cmd.cfg));

    if(!recorder_cmd_push(&cmd))
        return 0U;

    g_rec.stop_requests++;
    return 1U;
}

uint8_t sd_recorder_request_arm_stem(uint8_t stem_id,
                                     const sd_recorder_stem_cfg_t *cfg)
{
    if((cfg == 0) || (stem_id >= SD_RECORDER_MAX_STEMS))
        return 0U;

    if(g_rec.state != SD_RECORDER_STATE_IDLE)
    {
        g_rec.rejected_config_changes++;
        return 0U;
    }

    recorder_cmd_t cmd;
    cmd.type = CMD_ARM_STEM;
    cmd.stem_id = stem_id;
    cmd.cfg = *cfg;

    if(!recorder_cmd_push(&cmd))
        return 0U;

    g_rec.arm_requests++;
    return 1U;
}

uint8_t sd_recorder_request_disarm_stem(uint8_t stem_id)
{
    if(stem_id >= SD_RECORDER_MAX_STEMS)
        return 0U;

    if(g_rec.state != SD_RECORDER_STATE_IDLE)
    {
        g_rec.rejected_config_changes++;
        return 0U;
    }

    recorder_cmd_t cmd;
    cmd.type = CMD_DISARM_STEM;
    cmd.stem_id = stem_id;
    memset(&cmd.cfg, 0, sizeof(cmd.cfg));

    if(!recorder_cmd_push(&cmd))
        return 0U;

    g_rec.disarm_requests++;
    return 1U;
}

void sd_recorder_audio_block_begin(uint32_t frames)
{
    (void)frames;

    g_rec.block_boundary_calls++;

    recorder_cmd_t cmd;

    while(recorder_cmd_pop(&cmd))
    {
        switch(cmd.type)
        {
            case CMD_START:
                if(g_rec.state == SD_RECORDER_STATE_IDLE)
                {
                    recorder_set_state(SD_RECORDER_STATE_START_PENDING);
                }
                else
                {
                    g_rec.rejected_state_requests++;
                }
                break;

            case CMD_STOP:
                if(g_rec.state == SD_RECORDER_STATE_RECORDING)
                {
                    recorder_set_state(SD_RECORDER_STATE_STOP_PENDING);
                }
                else
                {
                    g_rec.rejected_state_requests++;
                }
                break;

            case CMD_ARM_STEM:
                if((g_rec.state == SD_RECORDER_STATE_IDLE) &&
                   (cmd.stem_id < SD_RECORDER_MAX_STEMS))
                {
                    g_rec.stems[cmd.stem_id].cfg = cmd.cfg;
                    g_rec.stems[cmd.stem_id].armed = 1U;
                }
                else
                {
                    g_rec.rejected_config_changes++;
                }
                break;

            case CMD_DISARM_STEM:
                if((g_rec.state == SD_RECORDER_STATE_IDLE) &&
                   (cmd.stem_id < SD_RECORDER_MAX_STEMS))
                {
                    memset(&g_rec.stems[cmd.stem_id], 0, sizeof(g_rec.stems[cmd.stem_id]));
                }
                else
                {
                    g_rec.rejected_config_changes++;
                }
                break;

            case CMD_NONE:
            default:
                break;
        }
    }

    if(g_rec.state == SD_RECORDER_STATE_START_PENDING)
    {
        recorder_set_state(SD_RECORDER_STATE_RECORDING);
    }
    else if(g_rec.state == SD_RECORDER_STATE_STOP_PENDING)
    {
        recorder_set_state(SD_RECORDER_STATE_FINALIZING);
    }
    else if(g_rec.state == SD_RECORDER_STATE_FINALIZING)
    {
        recorder_set_state(SD_RECORDER_STATE_IDLE);
    }
}

sd_recorder_state_t sd_recorder_get_state(void)
{
    return g_rec.state;
}

void sd_recorder_get_debug(sd_recorder_debug_t *out_debug)
{
    if(out_debug == 0)
        return;

    out_debug->state = g_rec.state;
    out_debug->start_requests = g_rec.start_requests;
    out_debug->stop_requests = g_rec.stop_requests;
    out_debug->arm_requests = g_rec.arm_requests;
    out_debug->disarm_requests = g_rec.disarm_requests;
    out_debug->rejected_config_changes = g_rec.rejected_config_changes;
    out_debug->rejected_state_requests = g_rec.rejected_state_requests;
    out_debug->block_boundary_calls = g_rec.block_boundary_calls;
    out_debug->transition_count = g_rec.transition_count;
}
