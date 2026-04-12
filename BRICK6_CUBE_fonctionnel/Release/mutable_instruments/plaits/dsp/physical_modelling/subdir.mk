################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CC_SRCS += \
../mutable_instruments/plaits/dsp/physical_modelling/modal_voice.cc \
../mutable_instruments/plaits/dsp/physical_modelling/resonator.cc \
../mutable_instruments/plaits/dsp/physical_modelling/string.cc \
../mutable_instruments/plaits/dsp/physical_modelling/string_voice.cc 

CC_DEPS += \
./mutable_instruments/plaits/dsp/physical_modelling/modal_voice.d \
./mutable_instruments/plaits/dsp/physical_modelling/resonator.d \
./mutable_instruments/plaits/dsp/physical_modelling/string.d \
./mutable_instruments/plaits/dsp/physical_modelling/string_voice.d 

OBJS += \
./mutable_instruments/plaits/dsp/physical_modelling/modal_voice.o \
./mutable_instruments/plaits/dsp/physical_modelling/resonator.o \
./mutable_instruments/plaits/dsp/physical_modelling/string.o \
./mutable_instruments/plaits/dsp/physical_modelling/string_voice.o 


# Each subdirectory must supply rules for building sources it contributes
mutable_instruments/plaits/dsp/physical_modelling/%.o mutable_instruments/plaits/dsp/physical_modelling/%.su mutable_instruments/plaits/dsp/physical_modelling/%.cyclo: ../mutable_instruments/plaits/dsp/physical_modelling/%.cc mutable_instruments/plaits/dsp/physical_modelling/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m7 -std=gnu++14 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Micro_Dexed" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Juno" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Daisy_SP" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments/stmlib" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/freeverb-main/Components" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/md-drum-synth-main" -O3 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -Wall -ffast-math -fstack-usage -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-mutable_instruments-2f-plaits-2f-dsp-2f-physical_modelling

clean-mutable_instruments-2f-plaits-2f-dsp-2f-physical_modelling:
	-$(RM) ./mutable_instruments/plaits/dsp/physical_modelling/modal_voice.cyclo ./mutable_instruments/plaits/dsp/physical_modelling/modal_voice.d ./mutable_instruments/plaits/dsp/physical_modelling/modal_voice.o ./mutable_instruments/plaits/dsp/physical_modelling/modal_voice.su ./mutable_instruments/plaits/dsp/physical_modelling/resonator.cyclo ./mutable_instruments/plaits/dsp/physical_modelling/resonator.d ./mutable_instruments/plaits/dsp/physical_modelling/resonator.o ./mutable_instruments/plaits/dsp/physical_modelling/resonator.su ./mutable_instruments/plaits/dsp/physical_modelling/string.cyclo ./mutable_instruments/plaits/dsp/physical_modelling/string.d ./mutable_instruments/plaits/dsp/physical_modelling/string.o ./mutable_instruments/plaits/dsp/physical_modelling/string.su ./mutable_instruments/plaits/dsp/physical_modelling/string_voice.cyclo ./mutable_instruments/plaits/dsp/physical_modelling/string_voice.d ./mutable_instruments/plaits/dsp/physical_modelling/string_voice.o ./mutable_instruments/plaits/dsp/physical_modelling/string_voice.su

.PHONY: clean-mutable_instruments-2f-plaits-2f-dsp-2f-physical_modelling

