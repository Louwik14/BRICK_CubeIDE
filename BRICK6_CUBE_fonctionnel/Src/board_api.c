#include <stdint.h>

// override du weak TinyUSB
uint16_t board_usb_get_serial(uint16_t *serial, uint16_t max_chars)
{
  (void) serial;
  (void) max_chars;
  return 0;
}
