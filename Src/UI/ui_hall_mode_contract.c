#include "ui_hall_mode_contract.h"

#include "ui_page_manager.h"

typedef struct
{
    uint8_t trigger_hall;
    uint8_t target_page;
    const char *base_label;
} ui_hall_mode_contract_t;

#define UI_HALL_KEYBOARD_MODE_TRIGGER 8U
#define UI_HALL_ARP_MODE_TRIGGER 9U
#define UI_HALL_SEQ_MODE_TRIGGER 10U
#define UI_HALL_MACRO_MODE_TRIGGER 15U
static const ui_hall_mode_contract_t g_ui_hall_mode_contracts[UI_HALL_MODE_COUNT] = {
    [UI_HALL_MODE_SEQ] = {
        .trigger_hall = UI_HALL_SEQ_MODE_TRIGGER,
        .target_page = UI_PAGE_TEMPLATE_SEQ,
        .base_label = "SEQ"
    },
    [UI_HALL_MODE_KEYBOARD] = {
        .trigger_hall = UI_HALL_KEYBOARD_MODE_TRIGGER,
        .target_page = UI_PAGE_TEMPLATE_KEYBOARD,
        .base_label = "KBD"
    },
    [UI_HALL_MODE_ARP] = {
        .trigger_hall = UI_HALL_ARP_MODE_TRIGGER,
        .target_page = UI_PAGE_TEMPLATE_ARP,
        .base_label = "ARP"
    },
    [UI_HALL_MODE_MACRO] = {
        .trigger_hall = UI_HALL_MACRO_MODE_TRIGGER,
        .target_page = UI_PAGE_TEMPLATE_MACRO,
        .base_label = "MACRO"
    },
    [UI_HALL_MODE_PATTERN] = {
        .trigger_hall = UI_HALL_MODE_TRIGGER_NONE,
        .target_page = UI_HALL_MODE_TARGET_PAGE_NONE,
        .base_label = "PAT"
    },
    [UI_HALL_MODE_MUTE] = {
        .trigger_hall = UI_HALL_MODE_TRIGGER_NONE,
        .target_page = UI_HALL_MODE_TARGET_PAGE_NONE,
        .base_label = "MUTE"
    },
};

static const ui_hall_mode_contract_t *ui_hall_mode_contract_get(ui_hall_mode_t mode)
{
    if ((uint8_t)mode >= (uint8_t)UI_HALL_MODE_COUNT)
    {
        return 0;
    }

    return &g_ui_hall_mode_contracts[(uint8_t)mode];
}

uint8_t ui_hall_mode_get_trigger_hall(ui_hall_mode_t mode, uint8_t *out_hall)
{
    if (out_hall == 0)
    {
        return 0U;
    }

    const ui_hall_mode_contract_t *const contract = ui_hall_mode_contract_get(mode);
    if ((contract == 0) || (contract->trigger_hall == UI_HALL_MODE_TRIGGER_NONE))
    {
        return 0U;
    }

    *out_hall = contract->trigger_hall;
    return 1U;
}

uint8_t ui_hall_mode_get_target_page(ui_hall_mode_t mode, uint8_t *out_page)
{
    if (out_page == 0)
    {
        return 0U;
    }

    const ui_hall_mode_contract_t *const contract = ui_hall_mode_contract_get(mode);
    if ((contract == 0) || (contract->target_page == UI_HALL_MODE_TARGET_PAGE_NONE))
    {
        return 0U;
    }

    *out_page = contract->target_page;
    return 1U;
}

const char *ui_hall_mode_get_base_label(ui_hall_mode_t mode)
{
    const ui_hall_mode_contract_t *const contract = ui_hall_mode_contract_get(mode);
    if ((contract == 0) || (contract->base_label == 0))
    {
        return "SEQ";
    }

    return contract->base_label;
}
