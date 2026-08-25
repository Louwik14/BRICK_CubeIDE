#ifndef RESTORE_TRANSACTION_H
#define RESTORE_TRANSACTION_H

#include <stdint.h>

#include "Storage/restore_plan_contract.h"

/* CONTROL owns publication; AUDIO owns service. The published plan must be
 * the linker-owned singleton and remains immutable until completion. */
void restore_transaction_control_init(void);
uint8_t restore_transaction_control_publish(uint32_t *out_request_seq);
uint8_t restore_transaction_audio_service(void);
uint8_t restore_transaction_control_completed(uint32_t request_seq,
                                               restore_result_t *out_result);

#endif
