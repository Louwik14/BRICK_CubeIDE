################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CC_SRCS += \
../mutable_instruments/plaits/dsp/fm/algorithms.cc \
../mutable_instruments/plaits/dsp/fm/dx_units.cc 

CC_DEPS += \
./mutable_instruments/plaits/dsp/fm/algorithms.d \
./mutable_instruments/plaits/dsp/fm/dx_units.d 

OBJS += \
./mutable_instruments/plaits/dsp/fm/algorithms.o \
./mutable_instruments/plaits/dsp/fm/dx_units.o 


# Each subdirectory must supply rules for building sources it contributes
mutable_instruments/plaits/dsp/fm/%.o mutable_instruments/plaits/dsp/fm/%.su mutable_instruments/plaits/dsp/fm/%.cyclo: ../mutable_instruments/plaits/dsp/fm/%.cc mutable_instruments/plaits/dsp/fm/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m7 -std=gnu++14 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Micro_Dexed" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Juno" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Daisy_SP" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments/stmlib" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/freeverb-main/Components" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/md-drum-synth-main" -O3 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -Wall -ffast-math -fstack-usage -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-mutable_instruments-2f-plaits-2f-dsp-2f-fm

clean-mutable_instruments-2f-plaits-2f-dsp-2f-fm:
	-$(RM) ./mutable_instruments/plaits/dsp/fm/algorithms.cyclo ./mutable_instruments/plaits/dsp/fm/algorithms.d ./mutable_instruments/plaits/dsp/fm/algorithms.o ./mutable_instruments/plaits/dsp/fm/algorithms.su ./mutable_instruments/plaits/dsp/fm/dx_units.cyclo ./mutable_instruments/plaits/dsp/fm/dx_units.d ./mutable_instruments/plaits/dsp/fm/dx_units.o ./mutable_instruments/plaits/dsp/fm/dx_units.su

.PHONY: clean-mutable_instruments-2f-plaits-2f-dsp-2f-fm

