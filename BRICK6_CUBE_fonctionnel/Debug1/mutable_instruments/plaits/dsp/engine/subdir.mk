################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CC_SRCS += \
../mutable_instruments/plaits/dsp/engine/additive_engine.cc \
../mutable_instruments/plaits/dsp/engine/bass_drum_engine.cc \
../mutable_instruments/plaits/dsp/engine/chord_engine.cc \
../mutable_instruments/plaits/dsp/engine/fm_engine.cc \
../mutable_instruments/plaits/dsp/engine/grain_engine.cc \
../mutable_instruments/plaits/dsp/engine/hi_hat_engine.cc \
../mutable_instruments/plaits/dsp/engine/modal_engine.cc \
../mutable_instruments/plaits/dsp/engine/noise_engine.cc \
../mutable_instruments/plaits/dsp/engine/particle_engine.cc \
../mutable_instruments/plaits/dsp/engine/snare_drum_engine.cc \
../mutable_instruments/plaits/dsp/engine/speech_engine.cc \
../mutable_instruments/plaits/dsp/engine/string_engine.cc \
../mutable_instruments/plaits/dsp/engine/swarm_engine.cc \
../mutable_instruments/plaits/dsp/engine/virtual_analog_engine.cc \
../mutable_instruments/plaits/dsp/engine/waveshaping_engine.cc \
../mutable_instruments/plaits/dsp/engine/wavetable_engine.cc 

CC_DEPS += \
./mutable_instruments/plaits/dsp/engine/additive_engine.d \
./mutable_instruments/plaits/dsp/engine/bass_drum_engine.d \
./mutable_instruments/plaits/dsp/engine/chord_engine.d \
./mutable_instruments/plaits/dsp/engine/fm_engine.d \
./mutable_instruments/plaits/dsp/engine/grain_engine.d \
./mutable_instruments/plaits/dsp/engine/hi_hat_engine.d \
./mutable_instruments/plaits/dsp/engine/modal_engine.d \
./mutable_instruments/plaits/dsp/engine/noise_engine.d \
./mutable_instruments/plaits/dsp/engine/particle_engine.d \
./mutable_instruments/plaits/dsp/engine/snare_drum_engine.d \
./mutable_instruments/plaits/dsp/engine/speech_engine.d \
./mutable_instruments/plaits/dsp/engine/string_engine.d \
./mutable_instruments/plaits/dsp/engine/swarm_engine.d \
./mutable_instruments/plaits/dsp/engine/virtual_analog_engine.d \
./mutable_instruments/plaits/dsp/engine/waveshaping_engine.d \
./mutable_instruments/plaits/dsp/engine/wavetable_engine.d 

OBJS += \
./mutable_instruments/plaits/dsp/engine/additive_engine.o \
./mutable_instruments/plaits/dsp/engine/bass_drum_engine.o \
./mutable_instruments/plaits/dsp/engine/chord_engine.o \
./mutable_instruments/plaits/dsp/engine/fm_engine.o \
./mutable_instruments/plaits/dsp/engine/grain_engine.o \
./mutable_instruments/plaits/dsp/engine/hi_hat_engine.o \
./mutable_instruments/plaits/dsp/engine/modal_engine.o \
./mutable_instruments/plaits/dsp/engine/noise_engine.o \
./mutable_instruments/plaits/dsp/engine/particle_engine.o \
./mutable_instruments/plaits/dsp/engine/snare_drum_engine.o \
./mutable_instruments/plaits/dsp/engine/speech_engine.o \
./mutable_instruments/plaits/dsp/engine/string_engine.o \
./mutable_instruments/plaits/dsp/engine/swarm_engine.o \
./mutable_instruments/plaits/dsp/engine/virtual_analog_engine.o \
./mutable_instruments/plaits/dsp/engine/waveshaping_engine.o \
./mutable_instruments/plaits/dsp/engine/wavetable_engine.o 


# Each subdirectory must supply rules for building sources it contributes
mutable_instruments/plaits/dsp/engine/%.o mutable_instruments/plaits/dsp/engine/%.su mutable_instruments/plaits/dsp/engine/%.cyclo: ../mutable_instruments/plaits/dsp/engine/%.cc mutable_instruments/plaits/dsp/engine/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m7 -std=gnu++14 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Micro_Dexed" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Juno" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Daisy_SP" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments/stmlib" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/freeverb-main/Components" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/md-drum-synth-main" -O0 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -Wall -ffast-math -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-mutable_instruments-2f-plaits-2f-dsp-2f-engine

clean-mutable_instruments-2f-plaits-2f-dsp-2f-engine:
	-$(RM) ./mutable_instruments/plaits/dsp/engine/additive_engine.cyclo ./mutable_instruments/plaits/dsp/engine/additive_engine.d ./mutable_instruments/plaits/dsp/engine/additive_engine.o ./mutable_instruments/plaits/dsp/engine/additive_engine.su ./mutable_instruments/plaits/dsp/engine/bass_drum_engine.cyclo ./mutable_instruments/plaits/dsp/engine/bass_drum_engine.d ./mutable_instruments/plaits/dsp/engine/bass_drum_engine.o ./mutable_instruments/plaits/dsp/engine/bass_drum_engine.su ./mutable_instruments/plaits/dsp/engine/chord_engine.cyclo ./mutable_instruments/plaits/dsp/engine/chord_engine.d ./mutable_instruments/plaits/dsp/engine/chord_engine.o ./mutable_instruments/plaits/dsp/engine/chord_engine.su ./mutable_instruments/plaits/dsp/engine/fm_engine.cyclo ./mutable_instruments/plaits/dsp/engine/fm_engine.d ./mutable_instruments/plaits/dsp/engine/fm_engine.o ./mutable_instruments/plaits/dsp/engine/fm_engine.su ./mutable_instruments/plaits/dsp/engine/grain_engine.cyclo ./mutable_instruments/plaits/dsp/engine/grain_engine.d ./mutable_instruments/plaits/dsp/engine/grain_engine.o ./mutable_instruments/plaits/dsp/engine/grain_engine.su ./mutable_instruments/plaits/dsp/engine/hi_hat_engine.cyclo ./mutable_instruments/plaits/dsp/engine/hi_hat_engine.d ./mutable_instruments/plaits/dsp/engine/hi_hat_engine.o ./mutable_instruments/plaits/dsp/engine/hi_hat_engine.su ./mutable_instruments/plaits/dsp/engine/modal_engine.cyclo ./mutable_instruments/plaits/dsp/engine/modal_engine.d ./mutable_instruments/plaits/dsp/engine/modal_engine.o ./mutable_instruments/plaits/dsp/engine/modal_engine.su ./mutable_instruments/plaits/dsp/engine/noise_engine.cyclo ./mutable_instruments/plaits/dsp/engine/noise_engine.d ./mutable_instruments/plaits/dsp/engine/noise_engine.o ./mutable_instruments/plaits/dsp/engine/noise_engine.su ./mutable_instruments/plaits/dsp/engine/particle_engine.cyclo ./mutable_instruments/plaits/dsp/engine/particle_engine.d ./mutable_instruments/plaits/dsp/engine/particle_engine.o ./mutable_instruments/plaits/dsp/engine/particle_engine.su ./mutable_instruments/plaits/dsp/engine/snare_drum_engine.cyclo ./mutable_instruments/plaits/dsp/engine/snare_drum_engine.d ./mutable_instruments/plaits/dsp/engine/snare_drum_engine.o ./mutable_instruments/plaits/dsp/engine/snare_drum_engine.su ./mutable_instruments/plaits/dsp/engine/speech_engine.cyclo ./mutable_instruments/plaits/dsp/engine/speech_engine.d ./mutable_instruments/plaits/dsp/engine/speech_engine.o ./mutable_instruments/plaits/dsp/engine/speech_engine.su ./mutable_instruments/plaits/dsp/engine/string_engine.cyclo ./mutable_instruments/plaits/dsp/engine/string_engine.d ./mutable_instruments/plaits/dsp/engine/string_engine.o ./mutable_instruments/plaits/dsp/engine/string_engine.su ./mutable_instruments/plaits/dsp/engine/swarm_engine.cyclo ./mutable_instruments/plaits/dsp/engine/swarm_engine.d ./mutable_instruments/plaits/dsp/engine/swarm_engine.o ./mutable_instruments/plaits/dsp/engine/swarm_engine.su ./mutable_instruments/plaits/dsp/engine/virtual_analog_engine.cyclo ./mutable_instruments/plaits/dsp/engine/virtual_analog_engine.d ./mutable_instruments/plaits/dsp/engine/virtual_analog_engine.o ./mutable_instruments/plaits/dsp/engine/virtual_analog_engine.su ./mutable_instruments/plaits/dsp/engine/waveshaping_engine.cyclo ./mutable_instruments/plaits/dsp/engine/waveshaping_engine.d ./mutable_instruments/plaits/dsp/engine/waveshaping_engine.o ./mutable_instruments/plaits/dsp/engine/waveshaping_engine.su ./mutable_instruments/plaits/dsp/engine/wavetable_engine.cyclo ./mutable_instruments/plaits/dsp/engine/wavetable_engine.d ./mutable_instruments/plaits/dsp/engine/wavetable_engine.o ./mutable_instruments/plaits/dsp/engine/wavetable_engine.su

.PHONY: clean-mutable_instruments-2f-plaits-2f-dsp-2f-engine

