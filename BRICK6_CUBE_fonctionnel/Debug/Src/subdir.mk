################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../Src/fx_reverb.cpp \
../Src/fx_granular.cpp \

C_SRCS += \
../Src/audio.c \
../Src/audio_float.c \
../Src/audio_io.c \
../Src/dsp_engine.c \
../Src/brick6_app_init.c \
../Src/control_router.c \
../Src/control_events.c \
../Src/param_store.c \
../Src/cpu_load.c \
../Src/cs42448.c \
../Src/dma.c \
../Src/drv_display.c \
../Src/drv_encoders.c \
../Src/engine_tasklet.c \
../Src/fmc.c \
../Src/font.c \
../Src/font4x6.c \
../Src/font5x7.c \
../Src/font5x8_elektron.c \
../Src/fx_dj_eq3_cmsis.c \
../Src/fx_chain.c \
../Src/fx_pool.c \
../Src/fx_onepole.c \
../Src/fx_saturation.c \
../Src/gpio.c \
../Src/i2c.c \
../Src/main.c \
../Src/mixer.c \
../Src/sai.c \
../Src/sd_audio_block_ring.c \
../Src/sd_stream.c \
../Src/sdmmc.c \
../Src/sdram.c \
../Src/sdram_alloc.c \
../Src/spi.c \
../Src/stm32h7xx_hal_msp.c \
../Src/stm32h7xx_it.c \
../Src/syscalls.c \
../Src/sysmem.c \
../Src/system_stm32h7xx.c \
../Src/ui_tasklet.c \
../Src/usart.c \
../Src/usb_otg.c \
../Src/usbh_midi.c \
../Src/w9825g6kh.c 

OBJS += \
./Src/audio.o \
./Src/audio_float.o \
./Src/audio_io.o \
./Src/dsp_engine.o \
./Src/brick6_app_init.o \
./Src/control_router.o \
./Src/control_events.o \
./Src/param_store.o \
./Src/cpu_load.o \
./Src/cs42448.o \
./Src/dma.o \
./Src/drv_display.o \
./Src/drv_encoders.o \
./Src/engine_tasklet.o \
./Src/fmc.o \
./Src/font.o \
./Src/font4x6.o \
./Src/font5x7.o \
./Src/font5x8_elektron.o \
./Src/fx_dj_eq3_cmsis.o \
./Src/fx_chain.o \
./Src/fx_pool.o \
./Src/fx_onepole.o \
./Src/fx_saturation.o \
./Src/fx_reverb.o \
./Src/fx_granular.o \
./Src/gpio.o \
./Src/i2c.o \
./Src/main.o \
./Src/mixer.o \
./Src/sai.o \
./Src/sd_audio_block_ring.o \
./Src/sd_stream.o \
./Src/sdmmc.o \
./Src/sdram.o \
./Src/sdram_alloc.o \
./Src/spi.o \
./Src/stm32h7xx_hal_msp.o \
./Src/stm32h7xx_it.o \
./Src/syscalls.o \
./Src/sysmem.o \
./Src/system_stm32h7xx.o \
./Src/ui_tasklet.o \
./Src/usart.o \
./Src/usb_otg.o \
./Src/usbh_midi.o \
./Src/w9825g6kh.o 

C_DEPS += \
./Src/audio.d \
./Src/audio_float.d \
./Src/audio_io.d \
./Src/dsp_engine.d \
./Src/brick6_app_init.d \
./Src/control_router.d \
./Src/control_events.d \
./Src/param_store.d \
./Src/cpu_load.d \
./Src/cs42448.d \
./Src/dma.d \
./Src/drv_display.d \
./Src/drv_encoders.d \
./Src/engine_tasklet.d \
./Src/fmc.d \
./Src/font.d \
./Src/font4x6.d \
./Src/font5x7.d \
./Src/font5x8_elektron.d \
./Src/fx_dj_eq3_cmsis.d \
./Src/fx_chain.d \
./Src/fx_pool.d \
./Src/fx_onepole.d \
./Src/fx_saturation.d \
./Src/fx_reverb.d \
./Src/fx_granular.d \
./Src/gpio.d \
./Src/i2c.d \
./Src/main.d \
./Src/mixer.d \
./Src/sai.d \
./Src/sd_audio_block_ring.d \
./Src/sd_stream.d \
./Src/sdmmc.d \
./Src/sdram.d \
./Src/sdram_alloc.d \
./Src/spi.d \
./Src/stm32h7xx_hal_msp.d \
./Src/stm32h7xx_it.d \
./Src/syscalls.d \
./Src/sysmem.d \
./Src/system_stm32h7xx.d \
./Src/ui_tasklet.d \
./Src/usart.d \
./Src/usb_otg.d \
./Src/usbh_midi.d \
./Src/w9825g6kh.d 


# Each subdirectory must supply rules for building sources it contributes
Src/%.o Src/%.su Src/%.cyclo: ../Src/%.c Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -Og -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"


Src/fx_reverb.o Src/fx_reverb.su Src/fx_reverb.cyclo: ../Src/fx_reverb.cpp Src/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m7 -std=gnu++17 -fno-exceptions -fno-rtti -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -Og -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

Src/fx_granular.o Src/fx_granular.su Src/fx_granular.cyclo: ../Src/fx_granular.cpp Src/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m7 -std=gnu++17 -fno-exceptions -fno-rtti -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -Og -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
clean: clean-Src

clean-Src:

.PHONY: clean-Src

