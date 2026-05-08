/**
 * @file brick6_recorder_runtime.c
 * @brief Orchestration recorder runtime (live/transport).
 *
 * Rôle du module:
 * - Regrouper l'init recorder boot et la logique de service runtime.
 *
 * Frontière:
 * - Ne remplace pas les modules live_recorder / recorder_transport.
 * - N'implémente pas la policy UI amont.
 */

#include "brick6_recorder_runtime.h"

#include <stddef.h>

#include "Audio/live_recorder.h"
#include "Audio/recorder_transport.h"

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
}

void brick6_recorder_runtime_process_transport(live_recorder_t *rec)
{
    if(rec == NULL)
        return;

    recorder_transport_process();
}
