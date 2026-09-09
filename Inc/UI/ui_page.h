#ifndef UI_PAGE_H
#define UI_PAGE_H

#include "ui_event.h"

typedef struct
{
    void (*enter)(void);
    void (*leave)(void);
    uint8_t (*handle_encoder)(uint8_t encoder, int16_t delta);
    void (*handle_event)(const ui_event_t *);
    void (*tick)(void);
    void (*sync_active_context)(void);
    void (*render)(void);
    uint8_t (*render_pending)(void);
    void (*render_cancel)(void);
    const void *context;

} ui_page_t;

#endif /* UI_PAGE_H */
