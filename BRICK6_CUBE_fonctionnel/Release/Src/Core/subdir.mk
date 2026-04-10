################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Src/Core/brick6_app_init.c \
../Src/Core/brick6_audio_runtime.c \
../Src/Core/brick6_boot_defaults.c \
../Src/Core/brick6_boot_fx_policy.c \
../Src/Core/brick6_master_control.c \
../Src/Core/brick6_recorder_runtime.c \
../Src/Core/brick6_sampler_bootstrap.c \
../Src/Core/cpu_load.c \
../Src/Core/engine_tasklet.c \
../Src/Core/track_runtime.c 

C_DEPS += \
./Src/Core/brick6_app_init.d \
./Src/Core/brick6_audio_runtime.d \
./Src/Core/brick6_boot_defaults.d \
./Src/Core/brick6_boot_fx_policy.d \
./Src/Core/brick6_master_control.d \
./Src/Core/brick6_recorder_runtime.d \
./Src/Core/brick6_sampler_bootstrap.d \
./Src/Core/cpu_load.d \
./Src/Core/engine_tasklet.d \
./Src/Core/track_runtime.d 

OBJS += \
./Src/Core/brick6_app_init.o \
./Src/Core/brick6_audio_runtime.o \
./Src/Core/brick6_boot_defaults.o \
./Src/Core/brick6_boot_fx_policy.o \
./Src/Core/brick6_master_control.o \
./Src/Core/brick6_recorder_runtime.o \
./Src/Core/brick6_sampler_bootstrap.o \
./Src/Core/cpu_load.o \
./Src/Core/engine_tasklet.o \
./Src/Core/track_runtime.o 


# Each subdirectory must supply rules for building sources it contributes
Src/Core/%.o Src/Core/%.su Src/Core/%.cyclo: ../Src/Core/%.c Src/Core/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Drv_app/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Core" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/MIDI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Param" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Storage" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/UI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/SD" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/U8g2" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/Third_Party/FatFs/src" -O1 -ffunction-sections -fdata-sections -Wall -fno-math-errno -ffast-math -fsingle-precision-constant -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Src-2f-Core

clean-Src-2f-Core:
	-$(RM) ./Src/Core/brick6_app_init.cyclo ./Src/Core/brick6_app_init.d ./Src/Core/brick6_app_init.o ./Src/Core/brick6_app_init.su ./Src/Core/brick6_audio_runtime.cyclo ./Src/Core/brick6_audio_runtime.d ./Src/Core/brick6_audio_runtime.o ./Src/Core/brick6_audio_runtime.su ./Src/Core/brick6_boot_defaults.cyclo ./Src/Core/brick6_boot_defaults.d ./Src/Core/brick6_boot_defaults.o ./Src/Core/brick6_boot_defaults.su ./Src/Core/brick6_boot_fx_policy.cyclo ./Src/Core/brick6_boot_fx_policy.d ./Src/Core/brick6_boot_fx_policy.o ./Src/Core/brick6_boot_fx_policy.su ./Src/Core/brick6_master_control.cyclo ./Src/Core/brick6_master_control.d ./Src/Core/brick6_master_control.o ./Src/Core/brick6_master_control.su ./Src/Core/brick6_recorder_runtime.cyclo ./Src/Core/brick6_recorder_runtime.d ./Src/Core/brick6_recorder_runtime.o ./Src/Core/brick6_recorder_runtime.su ./Src/Core/brick6_sampler_bootstrap.cyclo ./Src/Core/brick6_sampler_bootstrap.d ./Src/Core/brick6_sampler_bootstrap.o ./Src/Core/brick6_sampler_bootstrap.su ./Src/Core/cpu_load.cyclo ./Src/Core/cpu_load.d ./Src/Core/cpu_load.o ./Src/Core/cpu_load.su ./Src/Core/engine_tasklet.cyclo ./Src/Core/engine_tasklet.d ./Src/Core/engine_tasklet.o ./Src/Core/engine_tasklet.su ./Src/Core/track_runtime.cyclo ./Src/Core/track_runtime.d ./Src/Core/track_runtime.o ./Src/Core/track_runtime.su

.PHONY: clean-Src-2f-Core

