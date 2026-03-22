#include "ui_renderer_template.h"

#include <stdio.h>

#include "drv_display.h"
#include "font.h"
#include "param_registry.h"

static void ui_renderer_template_format_value(param_id_t id, char *out, uint32_t out_len)
{
    const param_desc_t *desc = &param_registry[id];
    const float value = param_get(id);

    switch (desc->display_type)
    {
        case PARAM_DISPLAY_BOOL:
        {
            const uint32_t index = (value >= 0.5f) ? 1U : 0U;
            const char *label = ((desc->labels != NULL) && (desc->labels[index] != NULL)) ? desc->labels[index] : ((index != 0U) ? "On" : "Off");
            (void)snprintf(out, out_len, "%s", label);
            break;
        }

        case PARAM_DISPLAY_ENUM:
        {
            const int32_t index = (int32_t)(value + 0.5f);
            const char *label = NULL;
            if ((desc->labels != NULL) && (index >= 0))
            {
                label = desc->labels[index];
            }
            if (label != NULL)
            {
                (void)snprintf(out, out_len, "%s", label);
            }
            else
            {
                (void)snprintf(out, out_len, "%ld", (long)index);
            }
            break;
        }

        case PARAM_DISPLAY_PERCENT:
            (void)snprintf(out, out_len, "%3lu%%", (unsigned long)(value * 100.0f + 0.5f));
            break;

        case PARAM_DISPLAY_DB:
            (void)snprintf(out, out_len, "%.1f%s", (double)value, desc->unit);
            break;

        case PARAM_DISPLAY_TIME_MS:
            (void)snprintf(out, out_len, "%.1fms", (double)(value * 1000.0f));
            break;

        case PARAM_DISPLAY_RATIO:
            (void)snprintf(out, out_len, "%.2f", (double)value);
            break;

        default:
            if ((desc->unit != 0) && (desc->unit[0] != '\0'))
            {
                (void)snprintf(out, out_len, "%.2f%s", (double)value, desc->unit);
            }
            else
            {
                (void)snprintf(out, out_len, "%.2f", (double)value);
            }
            break;
    }
}

void ui_renderer_template_draw(const ui_template_page_state_t *state)
{
    if ((state == 0) || (state->family == 0))
    {
        drv_display_draw_text(0U, 0U, "TEMPLATE N/A");
        return;
    }

    drv_display_set_font(&FONT_4X6);

    char header[32];
    const ui_template_subpage_t *subpage = ui_template_page_get_active_subpage(state);
    const char *subpage_title = (subpage != 0) && (subpage->title != 0) ? subpage->title : "SUB";
    (void)snprintf(header, sizeof(header), "%s %u/4 %s",
                   (state->family->family_title != 0) ? state->family->family_title : "TEMPLATE",
                   (unsigned)(state->active_subpage + 1U),
                   subpage_title);
    drv_display_draw_text(0U, 0U, header);

    if (subpage != 0)
    {
        for (uint8_t i = 0U; i < 4U; i++)
        {
            const param_id_t id = subpage->param_bank.params[i];
            char line[32];
            char value_txt[20];

            if (id >= PARAM_COUNT)
            {
                (void)snprintf(line, sizeof(line), "%u: ---", (unsigned)(i + 1U));
            }
            else
            {
                ui_renderer_template_format_value(id, value_txt, (uint32_t)sizeof(value_txt));
                (void)snprintf(line, sizeof(line), "%u:%s %s",
                               (unsigned)(i + 1U),
                               param_registry[id].name,
                               value_txt);
            }

            drv_display_draw_text(0U, (uint8_t)(10U + (i * 11U)), line);
        }
    }

    for (uint8_t i = 0U; i < 4U; i++)
    {
        const uint8_t x = (uint8_t)(i * 32U);
        const char *label = state->family->nav_labels[i];
        if (label == 0)
        {
            label = "-";
        }

        if (i == state->active_subpage)
        {
            drv_display_draw_rect(x, 54, 31, 10);
        }

        drv_display_draw_text((uint8_t)(x + 2U), 56U, label);
    }

    drv_display_set_font(&FONT_5X7);
}
