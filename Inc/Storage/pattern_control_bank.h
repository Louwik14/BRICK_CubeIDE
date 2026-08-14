#ifndef PATTERN_CONTROL_BANK_H
#define PATTERN_CONTROL_BANK_H
#include <stdint.h>
#include "Storage/persistent_control_codec.h"
void pattern_control_bank_init(void);
uint8_t pattern_control_bank_load(uint8_t bank,uint8_t pattern,persist_control_pattern_t*out);
uint8_t pattern_control_bank_store(uint8_t bank,uint8_t pattern,const persist_control_pattern_t*in);
uint8_t pattern_control_bank_delete(uint8_t bank,uint8_t pattern);
uint8_t pattern_control_bank_present(uint8_t bank,uint8_t pattern);
uint16_t pattern_control_bank_count(void);
uint8_t pattern_control_bank_get_ordinal(uint16_t ordinal,persist_control_pattern_record_t*out);
uint8_t pattern_control_bank_put_record(const persist_control_pattern_record_t*record);
uint8_t pattern_control_bank_get_ordinal_project(uint16_t ordinal,persist_control_pattern_record_t*out);
uint8_t pattern_control_bank_put_record_project(const persist_control_pattern_record_t*record);
void pattern_control_bank_clear_project(void);
uint8_t pattern_control_bank_commit(void*context);
void pattern_control_bank_abort(void*context);
#endif
