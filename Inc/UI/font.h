#ifndef UI_FONT_H
#define UI_FONT_H

#include <stdint.h>

typedef struct
{
    uint8_t id;
} font_t;

extern const font_t FONT_5X7;
extern const font_t FONT_4X6;
extern const font_t FONT_MINIMAL3X3;
extern const font_t FONT_3X3BASIC;
extern const font_t FONT_PEAR;
extern const font_t FONT_HELVB14;
extern const font_t FONT_OFF_COMPACT;

#endif /* UI_FONT_H */
