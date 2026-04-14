/**
 * @file live_recorder.c
 * @brief Module applicatif live_recorder.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à live_recorder.
 * - Fournir les services internes utilisés par le firmware utilisateur.
 *
 * Architecture:
 * - Appelé par: modules applicatifs selon l'orchestration du firmware.
 * - Appelle: dépendances matérielles et/ou modules utilisateur associés.
 *
 * Contraintes temps réel:
 * - IRQ: selon les API appelées.
 * - Hard realtime: selon le chemin d'exécution.
 * - malloc: éviter en chemin critique.
 *
 * Notes:
 * - Documentation ajoutée sans modification de la logique d'exécution.
 */

#include "Audio/live_recorder.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define LIVE_RECORDER_READ_SAFETY_MARGIN_FRAMES (256U)

/**
 * @brief Point d'entrée live_recorder_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à live_recorder_init.
 *
 * @param rec Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void live_recorder_init(live_recorder_t *rec)
{
    if(rec == NULL)
        return;

    rec->buffer = NULL;
    rec->max_frames = 0U;
    rec->loop_frames = 0U;
    rec->write_pos = 0U;
    rec->read_pos = 0U;
    rec->recording = 0U;
    rec->playing = 0U;
    rec->recorded_frames = 0U;
    rec->latency_offset_frames = 0U;
    rec->read_pos_q16 = 0U;
    rec->read_step_q16 = (uint32_t)(1.0f * 65536.0f);
    rec->play_rate = 1.0f;
    rec->tap_mode = (uint8_t)LIVE_RECORDER_TAP_POST_MIX;
}

/**
 * @brief Point d'entrée live_recorder_set_buffer.
 *
 * Rôle:
 * - Exécuter le traitement associé à live_recorder_set_buffer.
 *
 * @param rec Paramètre d'entrée de l'API.
 * @param buffer Paramètre d'entrée de l'API.
 * @param max_frames Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void live_recorder_set_buffer(live_recorder_t *rec,
                              float *buffer,
                              uint32_t max_frames)
{
    if(rec == NULL)
        return;

    rec->buffer = buffer;
    rec->max_frames = max_frames;

    if(rec->loop_frames > rec->max_frames)
        rec->loop_frames = rec->max_frames;

    if(rec->write_pos >= rec->max_frames)
        rec->write_pos = 0U;

    if(rec->read_pos >= rec->max_frames)
        rec->read_pos = 0U;
}

/**
 * @brief Point d'entrée live_recorder_set_loop_length.
 *
 * Rôle:
 * - Exécuter le traitement associé à live_recorder_set_loop_length.
 *
 * @param rec Paramètre d'entrée de l'API.
 * @param loop_frames Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void live_recorder_set_loop_length(live_recorder_t *rec,
                                   uint32_t loop_frames)
{
    if(rec == NULL)
        return;

    if(loop_frames > rec->max_frames)
        loop_frames = rec->max_frames;

    rec->loop_frames = loop_frames;

    if((rec->loop_frames == 0U) || (rec->write_pos >= rec->loop_frames))
        rec->write_pos = 0U;

    if((rec->loop_frames == 0U) || (rec->read_pos >= rec->loop_frames))
        rec->read_pos = 0U;
}

/**
 * @brief Point d'entrée live_recorder_start_record.
 *
 * Rôle:
 * - Exécuter le traitement associé à live_recorder_start_record.
 *
 * @param rec Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void live_recorder_start_record(live_recorder_t *rec)
{
    if(rec == NULL)
        return;

    rec->recording = 1U;
    rec->write_pos = 0U;
    rec->read_pos = 0U;
    rec->read_pos_q16 = 0U;
    rec->recorded_frames = 0U;
}

/**
 * @brief Point d'entrée live_recorder_stop_record.
 *
 * Rôle:
 * - Exécuter le traitement associé à live_recorder_stop_record.
 *
 * @param rec Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void live_recorder_stop_record(live_recorder_t *rec)
{
    if(rec == NULL)
        return;

    rec->recording = 0U;
}

/**
 * @brief Point d'entrée live_recorder_clear.
 *
 * Rôle:
 * - Effacer l'état et le contenu bufferisé.
 */
void live_recorder_clear(live_recorder_t *rec)
{
    if(rec == NULL)
        return;

    rec->recording = 0U;
    rec->playing = 0U;
    rec->recorded_frames = 0U;
    rec->write_pos = 0U;
    rec->read_pos = 0U;
    rec->read_pos_q16 = 0U;

    if((rec->buffer != NULL) && (rec->max_frames != 0U))
    {
        memset(rec->buffer, 0, sizeof(float) * rec->max_frames * 2U);
    }
}

/**
 * @brief Point d'entrée live_recorder_start_play.
 *
 * Rôle:
 * - Exécuter le traitement associé à live_recorder_start_play.
 *
 * @param rec Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void live_recorder_start_play(live_recorder_t *rec)
{
    if(rec == NULL)
        return;

    rec->playing = 1U;
    rec->read_pos = 0U;
    rec->read_pos_q16 = 0U;
}

/**
 * @brief Point d'entrée live_recorder_stop_play.
 *
 * Rôle:
 * - Exécuter le traitement associé à live_recorder_stop_play.
 *
 * @param rec Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void live_recorder_stop_play(live_recorder_t *rec)
{
    if(rec == NULL)
        return;

    rec->playing = 0U;
}

/**
 * @brief Point d'entrée live_recorder_set_play_rate.
 *
 * Rôle:
 * - Régler la vitesse de lecture.
 */
void live_recorder_set_play_rate(live_recorder_t *rec, float rate)
{
    if(rec == NULL)
        return;

    if(rate < 0.25f)
        rate = 0.25f;
    if(rate > 4.0f)
        rate = 4.0f;

    rec->play_rate = rate;
    rec->read_step_q16 = (uint32_t)(rate * 65536.0f + 0.5f);
    if(rec->read_step_q16 == 0U)
    {
        rec->read_step_q16 = 1U;
    }
}

/**
 * @brief Point d'entrée live_recorder_write.
 *
 * Rôle:
 * - Exécuter le traitement associé à live_recorder_write.
 *
 * @param rec Paramètre d'entrée de l'API.
 * @param L Paramètre d'entrée de l'API.
 * @param R Paramètre d'entrée de l'API.
 * @param frames Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void live_recorder_write(live_recorder_t *rec,
                         const float *L,
                         const float *R,
                         uint32_t frames)
{
    if((rec == NULL) || (L == NULL) || (R == NULL))
        return;

    if((rec->recording == 0U) || (rec->buffer == NULL) || (rec->loop_frames == 0U))
        return;

    uint32_t write_pos = rec->write_pos;
    float *buffer = rec->buffer;
    const uint32_t loop_frames = rec->loop_frames;

    for(uint32_t i = 0U; i < frames; i++)
    {
        const uint32_t idx = write_pos * 2U;
        buffer[idx] = L[i];
        buffer[idx + 1U] = R[i];

        write_pos++;
        if(write_pos >= loop_frames)
            write_pos = 0U;
    }

    rec->write_pos = write_pos;
    if (rec->recorded_frames < loop_frames)
    {
        const uint32_t remaining = loop_frames - rec->recorded_frames;
        if (frames >= remaining)
        {
            rec->recorded_frames = loop_frames;
        }
        else
        {
            rec->recorded_frames += frames;
        }
    }
}


/**
 * @brief Point d'entrée live_recorder_read.
 *
 * Rôle:
 * - Exécuter le traitement associé à live_recorder_read.
 *
 * @param rec Paramètre d'entrée de l'API.
 * @param outL Paramètre d'entrée de l'API.
 * @param outR Paramètre d'entrée de l'API.
 * @param frames Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void live_recorder_read(live_recorder_t *rec,
                        float *outL,
                        float *outR,
                        uint32_t frames)
{
    if((outL == NULL) || (outR == NULL))
        return;

    if((rec == NULL) || (rec->playing == 0U) || (rec->buffer == NULL) || (rec->loop_frames == 0U))
    {
        memset(outL, 0, sizeof(float) * frames);
        memset(outR, 0, sizeof(float) * frames);
        return;
    }

    if (rec->recorded_frames < 2U)
    {
        memset(outL, 0, sizeof(float) * frames);
        memset(outR, 0, sizeof(float) * frames);
        return;
    }

    const float *buffer = rec->buffer;
    const uint32_t loop_frames = rec->recorded_frames;
    const uint32_t write_pos = rec->write_pos;
    uint32_t read_pos_q16 = rec->read_pos_q16;
    const uint32_t read_step_q16 = (rec->read_step_q16 == 0U) ? (1U << 16) : rec->read_step_q16;
    const uint32_t loop_q16 = loop_frames << 16;

    for(uint32_t i = 0U; i < frames; i++)
    {
        const uint32_t read_pos = (read_pos_q16 >> 16);
        uint32_t distance;
        if(write_pos >= read_pos)
            distance = write_pos - read_pos;
        else
            distance = loop_frames - (read_pos - write_pos);

        if ((rec->recording != 0U) && (distance < LIVE_RECORDER_READ_SAFETY_MARGIN_FRAMES))
        {
            outL[i] = 0.0f;
            outR[i] = 0.0f;
        }
        else
        {
            const uint32_t idx = read_pos * 2U;
            const uint32_t next_pos = (read_pos + 1U) % loop_frames;
            const uint32_t next_idx = next_pos * 2U;
            const float frac = (float)(read_pos_q16 & 0xFFFFU) * (1.0f / 65536.0f);
            const float one_minus_frac = 1.0f - frac;
            outL[i] = (buffer[idx] * one_minus_frac) + (buffer[next_idx] * frac);
            outR[i] = (buffer[idx + 1U] * one_minus_frac) + (buffer[next_idx + 1U] * frac);
        }

        read_pos_q16 += read_step_q16;
        while(read_pos_q16 >= loop_q16)
            read_pos_q16 -= loop_q16;
    }

    rec->read_pos_q16 = read_pos_q16;
    rec->read_pos = (read_pos_q16 >> 16);
}
