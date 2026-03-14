#include "ui_page_manager.h"

#define UI_PAGE_MANAGER_MAX_PAGES 16U

/*
 * Page manager responsibilities:
 * - maintain a static page registry (no dynamic allocation)
 * - track the active page ID
 * - execute leave/enter hooks when switching pages
 */
static const ui_page_t *g_ui_pages[UI_PAGE_MANAGER_MAX_PAGES];
static uint8_t g_ui_page_count = 0U;
static uint8_t g_ui_current_page_id = 0U;

void ui_page_manager_init(void)
{
    for (uint8_t i = 0U; i < UI_PAGE_MANAGER_MAX_PAGES; i++)
    {
        g_ui_pages[i] = 0;
    }

    g_ui_page_count = 0U;
    g_ui_current_page_id = 0U;
}

void ui_page_manager_register(const ui_page_t *page)
{
    if ((page == 0) || (g_ui_page_count >= UI_PAGE_MANAGER_MAX_PAGES))
    {
        return;
    }

    g_ui_pages[g_ui_page_count] = page;
    g_ui_page_count++;
}

void ui_page_set(uint8_t page_id)
{
    if ((page_id >= g_ui_page_count) || (g_ui_pages[page_id] == 0))
    {
        return;
    }

    const ui_page_t *current_page = g_ui_pages[g_ui_current_page_id];
    const ui_page_t *next_page = g_ui_pages[page_id];

    if ((current_page != 0) && (current_page->leave != 0))
    {
        current_page->leave();
    }

    g_ui_current_page_id = page_id;

    if ((next_page != 0) && (next_page->enter != 0))
    {
        next_page->enter();
    }
}

const ui_page_t *ui_page_get(void)
{
    if ((g_ui_current_page_id >= g_ui_page_count) || (g_ui_pages[g_ui_current_page_id] == 0))
    {
        return 0;
    }

    return g_ui_pages[g_ui_current_page_id];
}

uint8_t ui_page_get_id(void)
{
    return g_ui_current_page_id;
}
