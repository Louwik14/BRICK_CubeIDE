#ifndef GROOVE_FLASH_LAYOUT_H
#define GROOVE_FLASH_LAYOUT_H
#define GROOVE_FLASH_BASE 0x081C0000UL
#define GROOVE_FLASH_SIZE (256UL * 1024UL)
#define GROOVE_FLASH_END  (GROOVE_FLASH_BASE + GROOVE_FLASH_SIZE)
#define GROOVE_FLASH_SECTOR_BYTES (128UL * 1024UL)
#define GROOVE_FLASH_FLASHWORD_BYTES 32UL
_Static_assert(GROOVE_FLASH_END == 0x08200000UL, "Groove Flash layout mismatch");
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert((GROOVE_FLASH_SIZE / GROOVE_FLASH_SECTOR_BYTES) == 2UL,
               "Groove Flash must occupy exactly two sectors");
#endif
#endif
