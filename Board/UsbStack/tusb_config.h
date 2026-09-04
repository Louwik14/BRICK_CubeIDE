#ifndef BRICK6_TUSB_CONFIG_H_
#define BRICK6_TUSB_CONFIG_H_

#define CFG_TUSB_MCU                    OPT_MCU_STM32H7
#define CFG_TUSB_OS                     OPT_OS_FREERTOS
#define CFG_TUSB_RHPORT0_MODE           (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#define CFG_TUD_ENABLED                 1
#define CFG_TUD_MIDI                    1
#define CFG_TUD_MIDI_EP_BUFSIZE         64
#define CFG_TUD_MIDI_RX_BUFSIZE         256
#define CFG_TUD_MIDI_TX_BUFSIZE         256
#define CFG_TUD_ENDPOINT0_SIZE          64

#define CFG_TUD_DWC2_DMA_ENABLE         0
#define CFG_TUD_DWC2_SLAVE_ENABLE       1
#define CFG_TUD_MEM_DCACHE_ENABLE       0
#define CFG_TUSB_DEBUG                  0

#endif /* BRICK6_TUSB_CONFIG_H_ */
