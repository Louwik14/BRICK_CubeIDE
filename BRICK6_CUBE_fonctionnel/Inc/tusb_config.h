#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "usb_descriptors.h"

//--------------------------------------------------------------------+
// Board Specific Configuration
//--------------------------------------------------------------------+

#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUSB_RHPORT1_MODE 0

#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED   OPT_MODE_DEFAULT_SPEED
#endif

//--------------------------------------------------------------------
// COMMON CONFIGURATION
//--------------------------------------------------------------------

#define CFG_TUSB_MCU OPT_MCU_STM32H7

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0
#endif

#define CFG_TUD_ENABLED 1
#define CFG_TUD_MAX_SPEED BOARD_TUD_MAX_SPEED

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------
// DEVICE CONFIGURATION
//--------------------------------------------------------------------

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE 64
#endif

//------------- CLASS -------------//
#define CFG_TUD_AUDIO  1
#define CFG_TUD_CDC    0
#define CFG_TUD_MSC    0
#define CFG_TUD_HID    0
#define CFG_TUD_MIDI   1
#define CFG_TUD_VENDOR 0

//--------------------------------------------------------------------
// AUDIO CLASS DRIVER CONFIGURATION
//--------------------------------------------------------------------

#define CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE 48000

// 16bit in 16bit slots
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_1_N_BYTES_PER_SAMPLE_TX 2
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_1_N_BYTES_PER_SAMPLE_RX 2
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_1_RESOLUTION_TX         16
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_1_RESOLUTION_RX         16

// Enable endpoints
#define CFG_TUD_AUDIO_ENABLE_EP_IN   1
#define CFG_TUD_AUDIO_ENABLE_EP_OUT  1

// Channels
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX 1
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX 1

// UAC1 Endpoint size calculation
#define CFG_TUD_AUDIO10_FUNC_1_FORMAT_1_EP_SZ_IN \
  TUD_AUDIO_EP_SIZE(false, \
                    CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE, \
                    CFG_TUD_AUDIO_FUNC_1_FORMAT_1_N_BYTES_PER_SAMPLE_TX, \
                    CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX)

#define CFG_TUD_AUDIO10_FUNC_1_FORMAT_1_EP_SZ_OUT \
  TUD_AUDIO_EP_SIZE(true, \
                    CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE, \
                    CFG_TUD_AUDIO_FUNC_1_FORMAT_1_N_BYTES_PER_SAMPLE_RX, \
                    CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX)

// IN sizes
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX \
  CFG_TUD_AUDIO10_FUNC_1_FORMAT_1_EP_SZ_IN

#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ \
  (4 * CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX)

// OUT sizes
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX \
  CFG_TUD_AUDIO10_FUNC_1_FORMAT_1_EP_SZ_OUT

#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ \
  (4 * CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX)

//--------------------------------------------------------------------
// MIDI
//--------------------------------------------------------------------

#define CFG_TUD_MIDI_RX_BUFSIZE (TUD_OPT_HIGH_SPEED ? 512 : 64)
#define CFG_TUD_MIDI_TX_BUFSIZE (TUD_OPT_HIGH_SPEED ? 512 : 64)

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H_ */
