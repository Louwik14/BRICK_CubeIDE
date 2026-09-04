#ifndef BRICK6_TUSB_CONFIG_H_
#define BRICK6_TUSB_CONFIG_H_

#define CFG_TUSB_MCU                    OPT_MCU_STM32H7
#define CFG_TUSB_OS                     OPT_OS_FREERTOS
#define CFG_TUSB_RHPORT0_MODE           (OPT_MODE_DEVICE | OPT_MODE_HOST | OPT_MODE_FULL_SPEED)

/* TinyUSB 0.21.0 names the FreeRTOS tick-to-millisecond conversion. */
#ifndef pdTICKS_TO_MS
#define pdTICKS_TO_MS(_ticks)           ((_ticks) * portTICK_PERIOD_MS)
#endif

#define CFG_TUD_ENABLED                 1
#define CFG_TUD_MIDI                    1
#define CFG_TUD_MIDI_EP_BUFSIZE         64
#define CFG_TUD_MIDI_RX_BUFSIZE         256
#define CFG_TUD_MIDI_TX_BUFSIZE         256
#define CFG_TUD_AUDIO                  1
#define CFG_TUD_AUDIO_ENABLE_EP_IN     1
#define CFG_TUD_AUDIO_ENABLE_EP_OUT    1
#define CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL 1
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP 1
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX  392
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX 392
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ  2048
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ 2048
#define CFG_TUD_AUDIO_CTRL_BUF_SZ       64
#define CFG_TUD_ENDPOINT0_SIZE          64

#define CFG_TUH_MIDI                    1
#define CFG_TUH_MIDI_RX_BUFSIZE         256
#define CFG_TUH_MIDI_TX_BUFSIZE         256
#define CFG_TUH_MIDI_EP_BUFSIZE         64
#define CFG_TUH_ENUMERATION_BUFSIZE     256

#define CFG_TUD_DWC2_DMA_ENABLE         0
#define CFG_TUD_DWC2_SLAVE_ENABLE       1
#define CFG_TUH_DWC2_DMA_ENABLE         0
#define CFG_TUH_DWC2_SLAVE_ENABLE       1
#define CFG_TUH_MEM_DCACHE_ENABLE       0
#define CFG_TUD_MEM_DCACHE_ENABLE       0
#define CFG_TUSB_DEBUG                  0

#endif /* BRICK6_TUSB_CONFIG_H_ */
