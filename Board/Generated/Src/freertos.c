/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : FreeRTOS application tasks for BRICK6 BRICK.
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "App/brick6_app_init.h"
#include "App/control_domain.h"
#include "Audio/audio_domain.h"
#include "Board/board_usb.h"
#include "encoders.h"
#include "IPC/live_event.h"
#include "MIDI/midi.h"
#include "MIDI/midi_host.h"
#include "App/control_rt_wakeup.h"
#include "App/control_rt_sampled_state.h"
#include "App/usb_service_wakeup.h"
#include "fusb302.h"
#include "SD/sd_block_device.h"
#include "SD/sd_scheduler_runtime.h"
#include "Storage/storage_io_wakeup.h"
#include "UI/display_flush_service.h"
#include "UI/ui_event.h"
#include "UI/ui_active_track_sync.h"
#include "UI/ui_hall_mode_flow.h"
#include "UI/ui_renderer_oled.h"
#include "UI/ui_service_wakeup.h"
#include "tusb.h"
#include "usb_role_manager.h"
#include "drv_display.h"
#include "led_hw.h"
#include "led_rgb.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
typedef StaticTask_t osStaticThreadDef_t;
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for CONTROL_RT */
osThreadId_t CONTROL_RTHandle;
uint32_t controlRtStack[ 1024 ];
osStaticThreadDef_t controlRtCb;
const osThreadAttr_t CONTROL_RT_attributes = {
  .name = "CONTROL_RT",
  .cb_mem = &controlRtCb,
  .cb_size = sizeof(controlRtCb),
  .stack_mem = &controlRtStack[0],
  .stack_size = sizeof(controlRtStack),
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for STORAGE_IO */
osThreadId_t STORAGE_IOHandle;
uint32_t storageIoStack[ 1536 ];
osStaticThreadDef_t storageIoCb;
const osThreadAttr_t STORAGE_IO_attributes = {
  .name = "STORAGE_IO",
  .cb_mem = &storageIoCb,
  .cb_size = sizeof(storageIoCb),
  .stack_mem = &storageIoStack[0],
  .stack_size = sizeof(storageIoStack),
  .priority = (osPriority_t) osPriorityAboveNormal1,
};
/* Definitions for USB_SERVICE */
osThreadId_t USB_SERVICEHandle;
uint32_t usbServiceStack[ 768 ];
osStaticThreadDef_t usbServiceCb;
const osThreadAttr_t USB_SERVICE_attributes = {
  .name = "USB_SERVICE",
  .cb_mem = &usbServiceCb,
  .cb_size = sizeof(usbServiceCb),
  .stack_mem = &usbServiceStack[0],
  .stack_size = sizeof(usbServiceStack),
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for UI_SERVICE */
osThreadId_t UI_SERVICEHandle;
uint32_t uiServiceStack[ 1024 ];
osStaticThreadDef_t uiServiceCb;
const osThreadAttr_t UI_SERVICE_attributes = {
  .name = "UI_SERVICE",
  .cb_mem = &uiServiceCb,
  .cb_size = sizeof(uiServiceCb),
  .stack_mem = &uiServiceStack[0],
  .stack_size = sizeof(uiServiceStack),
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for AUDIO_BG_LOCAL */
osThreadId_t AUDIO_BG_LOCALHandle;
uint32_t audioBgStack[ 512 ];
osStaticThreadDef_t audioBgCb;
const osThreadAttr_t AUDIO_BG_LOCAL_attributes = {
  .name = "AUDIO_BG_LOCAL",
  .cb_mem = &audioBgCb,
  .cb_size = sizeof(audioBgCb),
  .stack_mem = &audioBgStack[0],
  .stack_size = sizeof(audioBgStack),
  .priority = (osPriority_t) osPriorityAboveNormal2,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartControlTask(void *argument);
void StartStorageTask(void *argument);
void StartUsbTask(void *argument);
void StartUiTask(void *argument);
void StartAudioBgTask(void *argument);

static uint32_t storage_io_wait_timeout_ticks(void)
{
  const uint32_t now_ms = HAL_GetTick();
  uint32_t deadline_ms = 0U;
  uint64_t timeout_ticks;
  int32_t remaining_ms;
  if (storage_io_next_deadline_ms(now_ms, &deadline_ms) == 0U)
    return osWaitForever;
  remaining_ms = (int32_t)(deadline_ms - now_ms);
  if (remaining_ms <= 0)
    return 0U;
  timeout_ticks = (((uint64_t)(uint32_t)remaining_ms
                    * (uint64_t)osKernelGetTickFreq()) + 999U) / 1000U;
  if (timeout_ticks == 0U)
    timeout_ticks = 1U;
  return (timeout_ticks >= (uint64_t)osWaitForever)
      ? (osWaitForever - 1U) : (uint32_t)timeout_ticks;
}

static uint32_t control_rt_wait_timeout_ticks(void)
{
  const uint32_t now_ms = HAL_GetTick();
  uint32_t deadline_ms = now_ms;
  uint64_t timeout_ticks;
  int32_t remaining_ms;

  if (control_rt_sampled_state_next_deadline(now_ms, &deadline_ms) == 0U)
    return osWaitForever;
  remaining_ms = (int32_t)(deadline_ms - now_ms);
  if (remaining_ms <= 0)
    return 0U;
  timeout_ticks = (((uint64_t)(uint32_t)remaining_ms
                    * (uint64_t)osKernelGetTickFreq()) + 999U) / 1000U;
  if (timeout_ticks == 0U)
    timeout_ticks = 1U;
  return (timeout_ticks >= (uint64_t)osWaitForever)
      ? (osWaitForever - 1U) : (uint32_t)timeout_ticks;
}

static uint8_t ui_service_work_pending(void)
{
  return (ui_active_track_sync_is_pending() != 0U
          || ui_event_pending_count() != 0U
          || (control_domain_asset_terminal_available(
                  CONTROL_ASSET_FAMILY_CLASSIC) != 0U)
          || (control_domain_asset_terminal_available(
                  CONTROL_ASSET_FAMILY_RAM) != 0U)
          || (control_domain_asset_terminal_available(
                  CONTROL_ASSET_FAMILY_WAVETABLE) != 0U)
          || (control_domain_asset_terminal_available(
                  CONTROL_ASSET_FAMILY_MULTI) != 0U)
          || ui_service_dirty_is_set() != 0U
          || ((ui_service_led_dirty_is_set() != 0U)
              && (led_hw_busy() == 0U))
          || (drv_display_flush_continuation_pending() != 0U)
          || ((display_flush_service_frame_pending() != 0U)
              && (drv_display_flush_in_progress() == 0U)
              && (drv_display_get_state() == DRV_DISPLAY_STATE_READY))) ? 1U : 0U;
}

static uint8_t usb_service_work_pending(void)
{
  if (fusb302_irq_pending()
      || (usb_role_manager_work_pending() != 0U)
      || (midi_host_transport_work_pending() != 0U)
      || midi_usb_service_work_pending() != 0U)
  {
    return 1U;
  }

  if ((usb_role_manager_is_device_active() != 0U)
      && tud_task_event_ready())
  {
    return 1U;
  }

  if ((usb_role_manager_is_host_active() != 0U)
      && tuh_task_event_ready())
  {
    return 1U;
  }

  return 0U;
}

static uint32_t usb_service_wait_timeout_ticks(void)
{
  const uint32_t now_ms = HAL_GetTick();
  uint32_t deadline_ms;
  int32_t remaining_signed;
  uint64_t timeout_ticks;

  if (board_usb_next_deadline_ms(now_ms, &deadline_ms) == 0U) {
    return osWaitForever;
  }

  remaining_signed = (int32_t)(deadline_ms - now_ms);
  if (remaining_signed <= 0) {
    return 0U;
  }

  /* Deadlines are in milliseconds; CMSIS flags waits use kernel ticks. */
  timeout_ticks = (((uint64_t)(uint32_t)remaining_signed
                    * (uint64_t)osKernelGetTickFreq()) + 999U) / 1000U;
  if (timeout_ticks == 0U) {
    timeout_ticks = 1U;
  }
  if (timeout_ticks >= (uint64_t)osWaitForever) {
    return osWaitForever - 1U;
  }
  return (uint32_t)timeout_ticks;
}

static void ui_service_process_wakeup(uint32_t wake_flags)
{
  const uint8_t deadline_due = ((wake_flags & osFlagsError) != 0U) ? 1U : 0U;

  if (deadline_due != 0U)
    led_presentation_service_deadline(HAL_GetTick());

  if (((wake_flags & UI_SERVICE_WAKE_INPUT) != 0U) || (deadline_due != 0U))
    brick6_app_ui_process_input();

  if ((deadline_due != 0U)
      || ((wake_flags & UI_SERVICE_WAKE_DIRTY) != 0U))
    brick6_app_ui_process_presentation(deadline_due);

  /* Single UI presentation tail: render publishes frame-ready, then the
   * flush service advances DMA or its continuation. */
  display_flush_service_poll();

  if ((deadline_due != 0U)
      || ((wake_flags & UI_SERVICE_WAKE_LED) != 0U))
    led_presentation_service();
}

static uint32_t ui_service_wait_timeout_ticks(void)
{
  const uint32_t now_ms = HAL_GetTick();
  uint32_t timeout_ticks = ui_renderer_oled_next_render_wait_ticks();
  uint32_t deadline_ms = 0U;
  if (ui_hall_mode_flow_next_deadline(now_ms, &deadline_ms) != 0U)
  {
    const int32_t remaining_ms = (int32_t)(deadline_ms - now_ms);
    uint64_t patch_ticks;
    if (remaining_ms <= 0)
      return 0U;
    patch_ticks = (((uint64_t)(uint32_t)remaining_ms
                    * (uint64_t)osKernelGetTickFreq()) + 999U) / 1000U;
    if (patch_ticks == 0U)
      patch_ticks = 1U;
    if (patch_ticks >= (uint64_t)osWaitForever)
      patch_ticks = (uint64_t)(osWaitForever - 1U);
    if (patch_ticks < (uint64_t)timeout_ticks)
      timeout_ticks = (uint32_t)patch_ticks;
  }
  return timeout_ticks;
}

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of CONTROL_RT */
  CONTROL_RTHandle = osThreadNew(StartControlTask, NULL, &CONTROL_RT_attributes);

  /* creation of STORAGE_IO */
  STORAGE_IOHandle = osThreadNew(StartStorageTask, NULL, &STORAGE_IO_attributes);

  /* creation of USB_SERVICE */
  USB_SERVICEHandle = osThreadNew(StartUsbTask, NULL, &USB_SERVICE_attributes);

  /* creation of UI_SERVICE */
  UI_SERVICEHandle = osThreadNew(StartUiTask, NULL, &UI_SERVICE_attributes);

  /* creation of AUDIO_BG_LOCAL */
  AUDIO_BG_LOCALHandle = osThreadNew(StartAudioBgTask, NULL, &AUDIO_BG_LOCAL_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartControlTask */
/**
  * @brief  Function implementing the CONTROL_RT thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartControlTask */
void StartControlTask(void *argument)
{
  /* USER CODE BEGIN StartControlTask */
  uint8_t initial_control_wake = 1U;
  /* Infinite loop */
  for(;;)
  {
    uint32_t wake_flags = CONTROL_RT_WAKE_LATEST;
    if (initial_control_wake != 0U)
    {
      initial_control_wake = 0U;
    }
    else
    {
      wake_flags = osThreadFlagsWait(
          CONTROL_RT_WAKE_HALL
          | CONTROL_RT_WAKE_ENCODER
          | CONTROL_RT_WAKE_MIDI
          | CONTROL_RT_WAKE_UI
          | CONTROL_RT_WAKE_STORAGE
          | CONTROL_RT_WAKE_STREAM_RELEASE
          | CONTROL_RT_WAKE_DEADLINE
          | CONTROL_RT_WAKE_INPUT
          | CONTROL_RT_WAKE_LATEST,
          osFlagsWaitAny,
          control_rt_wait_timeout_ticks());
    }
    brick6_app_control_process_causes(
        ((wake_flags & osFlagsError) != 0U)
            ? CONTROL_RT_WAKE_DEADLINE : wake_flags);
  }
  /* USER CODE END StartControlTask */
}

/* USER CODE BEGIN Header_StartStorageTask */
/**
* @brief Function implementing the STORAGE_IO thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartStorageTask */
void StartStorageTask(void *argument)
{
  /* USER CODE BEGIN StartStorageTask */
  uint32_t wake_flags = 0U;
  /* Infinite loop */
  for(;;)
  {
    if ((wake_flags & STORAGE_IO_WAKE_SD) != 0U)
    {
      sd_scheduler_runtime_service();
      wake_flags &= ~STORAGE_IO_WAKE_SD;
    }
    brick6_app_storage_dispatch_once();
    if (storage_io_owner_snapshot() != 0U)
    {
      continue;
    }
    wake_flags = osThreadFlagsWait(STORAGE_IO_WAKE_SD
                                   | STORAGE_IO_WAKE_RUNNABLE,
                                   osFlagsWaitAny,
                                   storage_io_wait_timeout_ticks());
  }
  /* USER CODE END StartStorageTask */
}

/* USER CODE BEGIN Header_StartUsbTask */
/**
* @brief Function implementing the USB_SERVICE thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartUsbTask */
void StartUsbTask(void *argument)
{
  /* USER CODE BEGIN StartUsbTask */
  /* Infinite loop */
  for(;;)
  {
    brick6_app_usb_process();
    if (usb_service_work_pending() != 0U)
    {
      continue;
    }
    (void)osThreadFlagsWait(USB_SERVICE_WAKE_WORK,
                            osFlagsWaitAny,
                            usb_service_wait_timeout_ticks());
  }
  /* USER CODE END StartUsbTask */
}

/* USER CODE BEGIN Header_StartUiTask */
/**
* @brief Function implementing the UI_SERVICE thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartUiTask */
void StartUiTask(void *argument)
{
  /* USER CODE BEGIN StartUiTask */
  uint32_t wake_flags = UI_SERVICE_WAKE_INPUT;
  /* Infinite loop */
  for(;;)
  {
    ui_service_process_wakeup(wake_flags);
    if (ui_service_work_pending() != 0U)
    {
      wake_flags = 0U;
      if (ui_event_pending_count() != 0U)
        wake_flags |= UI_SERVICE_WAKE_INPUT;
      if (ui_active_track_sync_is_pending() != 0U)
        wake_flags |= UI_SERVICE_WAKE_INPUT;
      if (ui_service_dirty_is_set() != 0U)
        wake_flags |= UI_SERVICE_WAKE_DIRTY;
      if ((ui_service_led_dirty_is_set() != 0U) && (led_hw_busy() == 0U))
        wake_flags |= UI_SERVICE_WAKE_LED;
      if ((drv_display_flush_continuation_pending() != 0U)
          || ((display_flush_service_frame_pending() != 0U)
              && (drv_display_flush_in_progress() == 0U)
              && (drv_display_get_state() == DRV_DISPLAY_STATE_READY)))
        wake_flags |= UI_SERVICE_WAKE_OLED;
      if (wake_flags == 0U)
        continue;
      continue;
    }
    wake_flags = osThreadFlagsWait(UI_SERVICE_WAKE_INPUT
                                   | UI_SERVICE_WAKE_OLED
                                   | UI_SERVICE_WAKE_DIRTY
                                   | UI_SERVICE_WAKE_LED,
                                   osFlagsWaitAny,
                                   ui_service_wait_timeout_ticks());
  }
  /* USER CODE END StartUiTask */
}

/* USER CODE BEGIN Header_StartAudioBgTask */
/**
* @brief Function implementing the AUDIO_BG_LOCAL thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartAudioBgTask */
void StartAudioBgTask(void *argument)
{
  /* USER CODE BEGIN StartAudioBgTask */
  /* Infinite loop */
  for(;;)
  {
    audio_domain_background_task_process();
    osDelay(1);
  }
  /* USER CODE END StartAudioBgTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
