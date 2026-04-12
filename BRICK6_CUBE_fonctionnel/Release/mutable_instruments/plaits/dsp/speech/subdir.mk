################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CC_SRCS += \
../mutable_instruments/plaits/dsp/speech/lpc_speech_synth.cc \
../mutable_instruments/plaits/dsp/speech/lpc_speech_synth_controller.cc \
../mutable_instruments/plaits/dsp/speech/lpc_speech_synth_phonemes.cc \
../mutable_instruments/plaits/dsp/speech/lpc_speech_synth_words.cc \
../mutable_instruments/plaits/dsp/speech/naive_speech_synth.cc \
../mutable_instruments/plaits/dsp/speech/sam_speech_synth.cc 

CC_DEPS += \
./mutable_instruments/plaits/dsp/speech/lpc_speech_synth.d \
./mutable_instruments/plaits/dsp/speech/lpc_speech_synth_controller.d \
./mutable_instruments/plaits/dsp/speech/lpc_speech_synth_phonemes.d \
./mutable_instruments/plaits/dsp/speech/lpc_speech_synth_words.d \
./mutable_instruments/plaits/dsp/speech/naive_speech_synth.d \
./mutable_instruments/plaits/dsp/speech/sam_speech_synth.d 

OBJS += \
./mutable_instruments/plaits/dsp/speech/lpc_speech_synth.o \
./mutable_instruments/plaits/dsp/speech/lpc_speech_synth_controller.o \
./mutable_instruments/plaits/dsp/speech/lpc_speech_synth_phonemes.o \
./mutable_instruments/plaits/dsp/speech/lpc_speech_synth_words.o \
./mutable_instruments/plaits/dsp/speech/naive_speech_synth.o \
./mutable_instruments/plaits/dsp/speech/sam_speech_synth.o 


# Each subdirectory must supply rules for building sources it contributes
mutable_instruments/plaits/dsp/speech/%.o mutable_instruments/plaits/dsp/speech/%.su mutable_instruments/plaits/dsp/speech/%.cyclo: ../mutable_instruments/plaits/dsp/speech/%.cc mutable_instruments/plaits/dsp/speech/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m7 -std=gnu++14 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Micro_Dexed" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Juno" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Daisy_SP" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments/stmlib" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/freeverb-main/Components" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/md-drum-synth-main" -O3 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -Wall -ffast-math -fstack-usage -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-mutable_instruments-2f-plaits-2f-dsp-2f-speech

clean-mutable_instruments-2f-plaits-2f-dsp-2f-speech:
	-$(RM) ./mutable_instruments/plaits/dsp/speech/lpc_speech_synth.cyclo ./mutable_instruments/plaits/dsp/speech/lpc_speech_synth.d ./mutable_instruments/plaits/dsp/speech/lpc_speech_synth.o ./mutable_instruments/plaits/dsp/speech/lpc_speech_synth.su ./mutable_instruments/plaits/dsp/speech/lpc_speech_synth_controller.cyclo ./mutable_instruments/plaits/dsp/speech/lpc_speech_synth_controller.d ./mutable_instruments/plaits/dsp/speech/lpc_speech_synth_controller.o ./mutable_instruments/plaits/dsp/speech/lpc_speech_synth_controller.su ./mutable_instruments/plaits/dsp/speech/lpc_speech_synth_phonemes.cyclo ./mutable_instruments/plaits/dsp/speech/lpc_speech_synth_phonemes.d ./mutable_instruments/plaits/dsp/speech/lpc_speech_synth_phonemes.o ./mutable_instruments/plaits/dsp/speech/lpc_speech_synth_phonemes.su ./mutable_instruments/plaits/dsp/speech/lpc_speech_synth_words.cyclo ./mutable_instruments/plaits/dsp/speech/lpc_speech_synth_words.d ./mutable_instruments/plaits/dsp/speech/lpc_speech_synth_words.o ./mutable_instruments/plaits/dsp/speech/lpc_speech_synth_words.su ./mutable_instruments/plaits/dsp/speech/naive_speech_synth.cyclo ./mutable_instruments/plaits/dsp/speech/naive_speech_synth.d ./mutable_instruments/plaits/dsp/speech/naive_speech_synth.o ./mutable_instruments/plaits/dsp/speech/naive_speech_synth.su ./mutable_instruments/plaits/dsp/speech/sam_speech_synth.cyclo ./mutable_instruments/plaits/dsp/speech/sam_speech_synth.d ./mutable_instruments/plaits/dsp/speech/sam_speech_synth.o ./mutable_instruments/plaits/dsp/speech/sam_speech_synth.su

.PHONY: clean-mutable_instruments-2f-plaits-2f-dsp-2f-speech

