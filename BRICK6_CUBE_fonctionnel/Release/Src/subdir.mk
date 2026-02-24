################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../Src/fx_clouds.cpp \
../Src/fx_granular.cpp \
../Src/fx_reverb.cpp 

C_SRCS += \
../Src/app_controls.c \
../Src/app_controls_eq.c \
../Src/audio.c \
../Src/audio_float.c \
../Src/audio_io.c \
../Src/dsp_engine.c \
../Src/brick6_app_init.c \
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
../Src/usbh_midi.c \
../Src/w9825g6kh.c 

C_DEPS += \
./Src/app_controls.d \
./Src/app_controls_eq.d \
./Src/audio.d \
./Src/audio_float.d \
./Src/audio_io.d \
./Src/dsp_engine.d \
./Src/brick6_app_init.d \
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
./Src/fx_onepole.d \
./Src/fx_saturation.d \
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
./Src/usbh_midi.d \
./Src/w9825g6kh.d 

OBJS += \
./Src/app_controls.o \
./Src/app_controls_eq.o \
./Src/audio.o \
./Src/audio_float.o \
./Src/audio_io.o \
./Src/dsp_engine.o \
./Src/brick6_app_init.o \
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
./Src/fx_clouds.o \
./Src/fx_dj_eq3_cmsis.o \
./Src/fx_granular.o \
./Src/fx_onepole.o \
./Src/fx_reverb.o \
./Src/fx_saturation.o \
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
./Src/usbh_midi.o \
./Src/w9825g6kh.o 

CPP_DEPS += \
./Src/fx_clouds.d \
./Src/fx_granular.d \
./Src/fx_reverb.d 


# Each subdirectory must supply rules for building sources it contributes
Src/%.o Src/%.su Src/%.cyclo: ../Src/%.c Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -O3 -ffunction-sections -fdata-sections -Wall -fno-math-errno -ffast-math -fsingle-precision-constant -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Src/%.o Src/%.su Src/%.cyclo: ../Src/%.cpp Src/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m7 -std=gnu++14 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments/stmlib" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/freeverb-main/Components" -O3 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -ffast-math -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Src

clean-Src:
	-$(RM) ./Src/app_controls.cyclo ./Src/app_controls.d ./Src/app_controls.o ./Src/app_controls.su ./Src/app_controls_eq.cyclo ./Src/app_controls_eq.d ./Src/app_controls_eq.o ./Src/app_controls_eq.su ./Src/audio.cyclo ./Src/audio.d ./Src/audio.o ./Src/audio.su ./Src/audio_float.cyclo ./Src/audio_float.d ./Src/audio_float.o ./Src/audio_float.su ./Src/audio_io.cyclo ./Src/audio_io.d ./Src/audio_io.o ./Src/audio_io.su ./Src/dsp_engine.cyclo ./Src/dsp_engine.d ./Src/dsp_engine.o ./Src/dsp_engine.su ./Src/brick6_app_init.cyclo ./Src/brick6_app_init.d ./Src/brick6_app_init.o ./Src/brick6_app_init.su ./Src/param_store.cyclo ./Src/param_store.d ./Src/param_store.o ./Src/param_store.su ./Src/cpu_load.cyclo ./Src/cpu_load.d ./Src/cpu_load.o ./Src/cpu_load.su ./Src/cs42448.cyclo ./Src/cs42448.d ./Src/cs42448.o ./Src/cs42448.su ./Src/dma.cyclo ./Src/dma.d ./Src/dma.o ./Src/dma.su ./Src/drv_display.cyclo ./Src/drv_display.d ./Src/drv_display.o ./Src/drv_display.su ./Src/drv_encoders.cyclo ./Src/drv_encoders.d ./Src/drv_encoders.o ./Src/drv_encoders.su ./Src/engine_tasklet.cyclo ./Src/engine_tasklet.d ./Src/engine_tasklet.o ./Src/engine_tasklet.su ./Src/fmc.cyclo ./Src/fmc.d ./Src/fmc.o ./Src/fmc.su ./Src/font.cyclo ./Src/font.d ./Src/font.o ./Src/font.su ./Src/font4x6.cyclo ./Src/font4x6.d ./Src/font4x6.o ./Src/font4x6.su ./Src/font5x7.cyclo ./Src/font5x7.d ./Src/font5x7.o ./Src/font5x7.su ./Src/font5x8_elektron.cyclo ./Src/font5x8_elektron.d ./Src/font5x8_elektron.o ./Src/font5x8_elektron.su ./Src/fx_clouds.cyclo ./Src/fx_clouds.d ./Src/fx_clouds.o ./Src/fx_clouds.su ./Src/fx_dj_eq3_cmsis.cyclo ./Src/fx_dj_eq3_cmsis.d ./Src/fx_dj_eq3_cmsis.o ./Src/fx_dj_eq3_cmsis.su ./Src/fx_granular.cyclo ./Src/fx_granular.d ./Src/fx_granular.o ./Src/fx_granular.su ./Src/fx_onepole.cyclo ./Src/fx_onepole.d ./Src/fx_onepole.o ./Src/fx_onepole.su ./Src/fx_reverb.cyclo ./Src/fx_reverb.d ./Src/fx_reverb.o ./Src/fx_reverb.su ./Src/fx_saturation.cyclo ./Src/fx_saturation.d ./Src/fx_saturation.o ./Src/fx_saturation.su ./Src/gpio.cyclo ./Src/gpio.d ./Src/gpio.o ./Src/gpio.su ./Src/i2c.cyclo ./Src/i2c.d ./Src/i2c.o ./Src/i2c.su ./Src/main.cyclo ./Src/main.d ./Src/main.o ./Src/main.su ./Src/mixer.cyclo ./Src/mixer.d ./Src/mixer.o ./Src/mixer.su ./Src/sai.cyclo ./Src/sai.d ./Src/sai.o ./Src/sai.su ./Src/sd_audio_block_ring.cyclo ./Src/sd_audio_block_ring.d ./Src/sd_audio_block_ring.o ./Src/sd_audio_block_ring.su ./Src/sd_stream.cyclo ./Src/sd_stream.d ./Src/sd_stream.o ./Src/sd_stream.su ./Src/sdmmc.cyclo ./Src/sdmmc.d ./Src/sdmmc.o ./Src/sdmmc.su ./Src/sdram.cyclo ./Src/sdram.d ./Src/sdram.o ./Src/sdram.su ./Src/sdram_alloc.cyclo ./Src/sdram_alloc.d ./Src/sdram_alloc.o ./Src/sdram_alloc.su ./Src/spi.cyclo ./Src/spi.d ./Src/spi.o ./Src/spi.su ./Src/stm32h7xx_hal_msp.cyclo ./Src/stm32h7xx_hal_msp.d ./Src/stm32h7xx_hal_msp.o ./Src/stm32h7xx_hal_msp.su ./Src/stm32h7xx_it.cyclo ./Src/stm32h7xx_it.d ./Src/stm32h7xx_it.o ./Src/stm32h7xx_it.su ./Src/syscalls.cyclo ./Src/syscalls.d ./Src/syscalls.o ./Src/syscalls.su ./Src/sysmem.cyclo ./Src/sysmem.d ./Src/sysmem.o ./Src/sysmem.su ./Src/system_stm32h7xx.cyclo ./Src/system_stm32h7xx.d ./Src/system_stm32h7xx.o ./Src/system_stm32h7xx.su ./Src/ui_tasklet.cyclo ./Src/ui_tasklet.d ./Src/ui_tasklet.o ./Src/ui_tasklet.su ./Src/usart.cyclo ./Src/usart.d ./Src/usart.o ./Src/usart.su ./Src/usbh_midi.cyclo ./Src/usbh_midi.d ./Src/usbh_midi.o ./Src/usbh_midi.su ./Src/w9825g6kh.cyclo ./Src/w9825g6kh.d ./Src/w9825g6kh.o ./Src/w9825g6kh.su

.PHONY: clean-Src

