#ifndef NAME_CONTRACT_H
#define NAME_CONTRACT_H

#include <stdint.h>

#define NAME_CONTRACT_MAX_CHARS 32U
#define NAME_CONTRACT_BUFFER_BYTES (NAME_CONTRACT_MAX_CHARS + 1U)

typedef enum
{
    NAME_CONTRACT_RESULT_OK = 0,
    NAME_CONTRACT_RESULT_EMPTY,
    NAME_CONTRACT_RESULT_INVALID_CHARACTER,
    NAME_CONTRACT_RESULT_TOO_LONG,
    NAME_CONTRACT_RESULT_INVALID_ARGUMENT
} name_contract_result_t;

uint8_t name_contract_is_allowed_char(char value);
uint8_t name_contract_alphabet_size(void);
char name_contract_alphabet_char(uint8_t index);
uint8_t name_contract_char_index(char value, uint8_t *index);

/* Validates and trims only leading/trailing spaces into a bounded output. */
name_contract_result_t name_contract_normalize(
    const char *input,
    char output[NAME_CONTRACT_BUFFER_BYTES]);

#endif /* NAME_CONTRACT_H */
