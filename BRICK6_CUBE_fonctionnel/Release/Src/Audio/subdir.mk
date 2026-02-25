################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../Src/Audio/fx_clouds.cpp \
../Src/Audio/fx_granular.cpp \
../Src/Audio/fx_reverb.cpp 

C_SRCS += \
../Src/Audio/audio.c \
../Src/Audio/audio_float.c \
../Src/Audio/audio_io.c \
../Src/Audio/dsp_engine.c \
../Src/Audio/fx_chain.c \
../Src/Audio/fx_dj_eq3_cmsis.c \
../Src/Audio/fx_onepole.c \
../Src/Audio/fx_pool.c \
../Src/Audio/fx_saturation.c \
../Src/Audio/mixer.c \
../Src/Audio/sd_audio_block_ring.c 

C_DEPS += \
./Src/Audio/audio.d \
./Src/Audio/audio_float.d \
./Src/Audio/audio_io.d \
./Src/Audio/dsp_engine.d \
./Src/Audio/fx_chain.d \
./Src/Audio/fx_dj_eq3_cmsis.d \
./Src/Audio/fx_onepole.d \
./Src/Audio/fx_pool.d \
./Src/Audio/fx_saturation.d \
./Src/Audio/mixer.d \
./Src/Audio/sd_audio_block_ring.d 

OBJS += \
./Src/Audio/audio.o \
./Src/Audio/audio_float.o \
./Src/Audio/audio_io.o \
./Src/Audio/dsp_engine.o \
./Src/Audio/fx_chain.o \
./Src/Audio/fx_clouds.o \
./Src/Audio/fx_dj_eq3_cmsis.o \
./Src/Audio/fx_granular.o \
./Src/Audio/fx_onepole.o \
./Src/Audio/fx_pool.o \
./Src/Audio/fx_reverb.o \
./Src/Audio/fx_saturation.o \
./Src/Audio/mixer.o \
./Src/Audio/sd_audio_block_ring.o 

CPP_DEPS += \
./Src/Audio/fx_clouds.d \
./Src/Audio/fx_granular.d \
./Src/Audio/fx_reverb.d 


# Each subdirectory must supply rules for building sources it contributes
Src/Audio/%.o Src/Audio/%.su Src/Audio/%.cyclo: ../Src/Audio/%.c Src/Audio/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Drv_app/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Core" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/MIDI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Param" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Storage" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/UI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc" -O3 -ffunction-sections -fdata-sections -Wall -fno-math-errno -ffast-math -fsingle-precision-constant -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Src/Audio/%.o Src/Audio/%.su Src/Audio/%.cyclo: ../Src/Audio/%.cpp Src/Audio/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m7 -std=gnu++14 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments/stmlib" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/freeverb-main/Components" -O3 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -ffast-math -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Src-2f-Audio

clean-Src-2f-Audio:
	-$(RM) ./Src/Audio/audio.cyclo ./Src/Audio/audio.d ./Src/Audio/audio.o ./Src/Audio/audio.su ./Src/Audio/audio_float.cyclo ./Src/Audio/audio_float.d ./Src/Audio/audio_float.o ./Src/Audio/audio_float.su ./Src/Audio/audio_io.cyclo ./Src/Audio/audio_io.d ./Src/Audio/audio_io.o ./Src/Audio/audio_io.su ./Src/Audio/dsp_engine.cyclo ./Src/Audio/dsp_engine.d ./Src/Audio/dsp_engine.o ./Src/Audio/dsp_engine.su ./Src/Audio/fx_chain.cyclo ./Src/Audio/fx_chain.d ./Src/Audio/fx_chain.o ./Src/Audio/fx_chain.su ./Src/Audio/fx_clouds.cyclo ./Src/Audio/fx_clouds.d ./Src/Audio/fx_clouds.o ./Src/Audio/fx_clouds.su ./Src/Audio/fx_dj_eq3_cmsis.cyclo ./Src/Audio/fx_dj_eq3_cmsis.d ./Src/Audio/fx_dj_eq3_cmsis.o ./Src/Audio/fx_dj_eq3_cmsis.su ./Src/Audio/fx_granular.cyclo ./Src/Audio/fx_granular.d ./Src/Audio/fx_granular.o ./Src/Audio/fx_granular.su ./Src/Audio/fx_onepole.cyclo ./Src/Audio/fx_onepole.d ./Src/Audio/fx_onepole.o ./Src/Audio/fx_onepole.su ./Src/Audio/fx_pool.cyclo ./Src/Audio/fx_pool.d ./Src/Audio/fx_pool.o ./Src/Audio/fx_pool.su ./Src/Audio/fx_reverb.cyclo ./Src/Audio/fx_reverb.d ./Src/Audio/fx_reverb.o ./Src/Audio/fx_reverb.su ./Src/Audio/fx_saturation.cyclo ./Src/Audio/fx_saturation.d ./Src/Audio/fx_saturation.o ./Src/Audio/fx_saturation.su ./Src/Audio/mixer.cyclo ./Src/Audio/mixer.d ./Src/Audio/mixer.o ./Src/Audio/mixer.su ./Src/Audio/sd_audio_block_ring.cyclo ./Src/Audio/sd_audio_block_ring.d ./Src/Audio/sd_audio_block_ring.o ./Src/Audio/sd_audio_block_ring.su

.PHONY: clean-Src-2f-Audio

