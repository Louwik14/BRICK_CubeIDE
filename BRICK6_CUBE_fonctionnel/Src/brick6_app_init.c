#include "brick6_app_init.h"

#include "audio_in.h"
#include "audio_out.h"
#include "brick6_refactor.h"
#include "diagnostics_tasklet.h"
#include "engine_tasklet.h"
#include "midi.h"
#include "sai.h"
#include "sd_stream.h"
#include "sdmmc.h"
#include "sdram.h"
#include "stm32h7xx_hal.h"
#include "usb_device.h"
#include "usb_host.h"

void brick6_app_init(void)
{
  diagnostics_log("FMC init OK\r\n");
  diagnostics_log("Starting SDRAM init...\r\n");
  SDRAM_Init();
  diagnostics_log("SDRAM init done\r\n");
  diagnostics_log("Starting SDRAM test...\r\n");
  SDRAM_Test();
  diagnostics_sdram_alloc_test();

  diagnostics_on_sd_stream_init(sd_stream_init(&hsd1));

  MX_USB_DEVICE_Init();
  MX_USB_HOST_Init();
  /* Init audio */
  AudioOut_Init(&hsai_BlockA1);
  AudioIn_Init(&hsai_BlockB1);

#if BRICK6_REFACTOR_STEP_3
  engine_tasklet_init(AUDIO_OUT_SAMPLE_RATE);
#endif

  AudioOut_Start();
  (void)HAL_SAI_Receive_DMA(&hsai_BlockB1,
                            (uint8_t *)AudioIn_GetBuffer(),
                            AudioIn_GetBufferSamples());

  HAL_Delay(200);

  /* Init MIDI */
  midi_init();
}
