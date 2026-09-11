#include "App/name_contract.h"

static const char g_name_contract_alphabet[] =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";

uint8_t name_contract_is_allowed_char(char value)
{
    for (uint8_t i = 0U; i < name_contract_alphabet_size(); ++i)
    {
        if (g_name_contract_alphabet[i] == value)
        {
            return 1U;
        }
    }
    return 0U;
}

uint8_t name_contract_alphabet_size(void)
{
    return (uint8_t)(sizeof(g_name_contract_alphabet) - 1U);
}

char name_contract_alphabet_char(uint8_t index)
{
    return (index < name_contract_alphabet_size())
        ? g_name_contract_alphabet[index]
        : g_name_contract_alphabet[0];
}

uint8_t name_contract_char_index(char value, uint8_t *index)
{
    if (index == 0)
    {
        return 0U;
    }

    for (uint8_t i = 0U; i < name_contract_alphabet_size(); ++i)
    {
        if (g_name_contract_alphabet[i] == value)
        {
            *index = i;
            return 1U;
        }
    }

    return 0U;
}

name_contract_result_t name_contract_normalize(
    const char *input,
    char output[NAME_CONTRACT_BUFFER_BYTES])
{
    if ((input == 0) || (output == 0))
    {
        return NAME_CONTRACT_RESULT_INVALID_ARGUMENT;
    }

    output[0] = '\0';

    uint32_t input_length = 0U;
    while (input[input_length] != '\0')
    {
        ++input_length;
    }

    uint32_t first = 0U;
    while ((first < input_length) && (input[first] == ' '))
    {
        ++first;
    }

    uint32_t last = input_length;
    while ((last > first) && (input[last - 1U] == ' '))
    {
        --last;
    }

    if (first == last)
    {
        return NAME_CONTRACT_RESULT_EMPTY;
    }

    const uint32_t output_length = last - first;
    if (output_length > NAME_CONTRACT_MAX_CHARS)
    {
        return NAME_CONTRACT_RESULT_TOO_LONG;
    }

    for (uint32_t i = first; i < last; ++i)
    {
        if (name_contract_is_allowed_char(input[i]) == 0U)
        {
            return NAME_CONTRACT_RESULT_INVALID_CHARACTER;
        }
        output[i - first] = input[i];
    }
    output[output_length] = '\0';

    return NAME_CONTRACT_RESULT_OK;
}
