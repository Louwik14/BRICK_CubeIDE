/**
 * @file brick6_recorder_runtime.c
 * @brief Orchestration recorder runtime (live/transport/sd writer).
 *
 * Rôle du module:
 * - Regrouper l'init recorder boot et la logique de service runtime.
 *
 * Frontière:
 * - Ne remplace pas les modules live_recorder / recorder_transport / sd_recorder.
 * - N'implémente pas la policy UI amont.
 */

#include "brick6_recorder_runtime.h"

#include <stddef.h>

#include "Audio/live_recorder.h"
#include "Audio/recorder_transport.h"
#include "Audio/sd_multitrack_recorder.h"
#include "Core/brick6_sd_config.h"

void brick6_recorder_runtime_boot_init(live_recorder_t *rec,
                                       float *buffer,
                                       uint32_t max_frames)
{
    if(rec == NULL)
        return;

    live_recorder_init(rec);
    live_recorder_set_buffer(rec, buffer, max_frames);
    live_recorder_set_loop_length(rec, max_frames);
    live_recorder_start_play(rec);

    recorder_transport_init();
#if BRICK6_SD_ENABLE_RECORDER
    sd_recorder_init();
#endif
}

void brick6_recorder_runtime_process_transport(live_recorder_t *rec)
{
    static uint8_t last_transport_recording = 0U;

    if(rec == NULL)
        return;

    recorder_transport_process();

    {
        const uint8_t transport_recording =
            recorder_transport_is_recording();

        if((transport_recording != 0U) &&
           (last_transport_recording == 0U))
        {
#if BRICK6_SD_ENABLE_RECORDER
            (void)sd_recorder_request_start();
#endif
        }
        else if((transport_recording == 0U) &&
                (last_transport_recording != 0U))
        {
#if BRICK6_SD_ENABLE_RECORDER
            (void)sd_recorder_request_stop();
#endif
        }

        last_transport_recording = transport_recording;
    }
}

void brick6_recorder_runtime_service_writer(void)
{
#if BRICK6_SD_ENABLE_RECORDER
    sd_recorder_writer_service();
#endif
}
