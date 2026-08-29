#ifndef CONTROL_AUDIO_PUBLICATION_H
#define CONTROL_AUDIO_PUBLICATION_H

#include <stdint.h>
#include "IPC/control_audio_command.h"

/* Sole M4 writer API.  Producers may stage locally, but only this module owns
 * final ordering and the shared FIFO head. */
void control_audio_publication_init(void);
uint8_t control_audio_publish_batch(const control_audio_command_t *commands,
                                    uint16_t count);
uint8_t control_audio_publication_begin_horizon(uint64_t first_sample,
                                                uint16_t frames);
void control_audio_publication_abort_horizon(void);
uint8_t control_audio_publication_commit_horizon(void);
uint16_t control_audio_publication_free(void);
uint32_t control_audio_publication_capacity_failure_count(void);
uint8_t control_audio_publish_program(uint8_t entity, uint32_t descriptor,
                                      uint64_t sample_time);
uint8_t control_audio_publish_param(uint8_t entity, uint16_t param_id,
                                    uint32_t value, uint32_t target_detail,
                                    uint64_t sample_time);
uint8_t control_audio_publish_param_fenced(uint8_t entity, uint16_t param_id,
                                           uint32_t value,
                                           uint32_t target_detail,
                                           uint64_t sample_time,
                                           uint32_t *out_consumer_fence);
uint8_t control_audio_consumer_fence_consumed(uint32_t consumer_fence);
uint8_t control_audio_publish_note(uint8_t entity, uint8_t kind,
                                   uint32_t output_id, uint8_t note,
                                   uint8_t velocity, uint64_t sample_time);
uint8_t control_audio_publish_transport(uint8_t kind, uint32_t position,
                                        uint64_t sample_time);
uint8_t control_audio_publish_record(uint8_t kind, uint32_t session_id,
                                     uint32_t config, uint8_t client,
                                     uint64_t sample_time);
uint8_t control_audio_publish_panic(uint8_t kind, uint8_t entity,
                                    uint64_t sample_time);
uint8_t control_audio_publish_panic_fenced(uint8_t kind, uint8_t entity,
                                           uint64_t sample_time,
                                           uint32_t *out_consumer_fence);

#endif
