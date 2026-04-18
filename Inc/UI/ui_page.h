#ifndef UI_PAGE_H
#define UI_PAGE_H

#include "ui_event.h"

typedef struct
{
    void (*enter)(void);
    void (*leave)(void);
    void (*handle_event)(const ui_event_t *);
    void (*tick)(void);
    void (*sync_active_context)(void);
    void (*render)(void);
    const void *context;

} ui_page_t;

#endif /* UI_PAGE_H */
