################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../Src/Audio/fx_BusCompressorCore.cpp \
../Src/Audio/fx_Daisy_comp_core.cpp \
../Src/Audio/fx_bus_compressor.cpp \
../Src/Audio/fx_clouds.cpp \
../Src/Audio/fx_daisy_comp.cpp \
../Src/Audio/fx_granular.cpp \
../Src/Audio/fx_reverb.cpp \
../Src/Audio/fx_svf.cpp \
../Src/Audio/juno_synth.cpp \
../Src/Audio/microdexed_synth.cpp \
../Src/Audio/tb3_synth.cpp \
../Src/Audio/monob_moog_ladder.cpp \
../Src/Audio/monob_osc_bank.cpp \
../Src/Audio/moogladder.cpp \
../Src/Audio/oscillator.cpp 

C_SRCS += \
../Src/Audio/audio.c \
../Src/Audio/audio_float.c \
../Src/Audio/audio_io.c \
../Src/Audio/dsp_engine.c \
../Src/Audio/fx_biquad_filter.c \
../Src/Audio/fx_chain.c \
../Src/Audio/fx_dj_eq3_cmsis.c \
../Src/Audio/fx_filter_ladder_moog.c \
../Src/Audio/fx_onepole.c \
../Src/Audio/fx_pool.c \
../Src/Audio/fx_saturation.c \
../Src/Audio/juno_midi_queue.c \
../Src/Audio/live_recorder.c \
../Src/Audio/mixer.c \
../Src/Audio/monob_synth.c \
../Src/Audio/recorder_transport.c \
../Src/Audio/sampler.c \
../Src/Audio/sd_multitrack_recorder.c 

C_DEPS += \
./Src/Audio/audio.d \
./Src/Audio/audio_float.d \
./Src/Audio/audio_io.d \
./Src/Audio/dsp_engine.d \
./Src/Audio/fx_biquad_filter.d \
./Src/Audio/fx_chain.d \
./Src/Audio/fx_dj_eq3_cmsis.d \
./Src/Audio/fx_filter_ladder_moog.d \
./Src/Audio/fx_onepole.d \
./Src/Audio/fx_pool.d \
./Src/Audio/fx_saturation.d \
./Src/Audio/juno_midi_queue.d \
./Src/Audio/live_recorder.d \
./Src/Audio/mixer.d \
./Src/Audio/monob_synth.d \
./Src/Audio/recorder_transport.d \
./Src/Audio/sampler.d \
./Src/Audio/sd_multitrack_recorder.d 

OBJS += \
./Src/Audio/audio.o \
./Src/Audio/audio_float.o \
./Src/Audio/audio_io.o \
./Src/Audio/dsp_engine.o \
./Src/Audio/fx_BusCompressorCore.o \
./Src/Audio/fx_Daisy_comp_core.o \
./Src/Audio/fx_biquad_filter.o \
./Src/Audio/fx_bus_compressor.o \
./Src/Audio/fx_chain.o \
./Src/Audio/fx_clouds.o \
./Src/Audio/fx_daisy_comp.o \
./Src/Audio/fx_dj_eq3_cmsis.o \
./Src/Audio/fx_filter_ladder_moog.o \
./Src/Audio/fx_granular.o \
./Src/Audio/fx_onepole.o \
./Src/Audio/fx_pool.o \
./Src/Audio/fx_reverb.o \
./Src/Audio/fx_saturation.o \
./Src/Audio/fx_svf.o \
./Src/Audio/juno_midi_queue.o \
./Src/Audio/juno_synth.o \
./Src/Audio/live_recorder.o \
./Src/Audio/microdexed_synth.o \
./Src/Audio/tb3_synth.o \
./Src/Audio/mixer.o \
./Src/Audio/monob_moog_ladder.o \
./Src/Audio/monob_osc_bank.o \
./Src/Audio/monob_synth.o \
./Src/Audio/moogladder.o \
./Src/Audio/oscillator.o \
./Src/Audio/recorder_transport.o \
./Src/Audio/sampler.o \
./Src/Audio/sd_multitrack_recorder.o 

CPP_DEPS += \
./Src/Audio/fx_BusCompressorCore.d \
./Src/Audio/fx_Daisy_comp_core.d \
./Src/Audio/fx_bus_compressor.d \
./Src/Audio/fx_clouds.d \
./Src/Audio/fx_daisy_comp.d \
./Src/Audio/fx_granular.d \
./Src/Audio/fx_reverb.d \
./Src/Audio/fx_svf.d \
./Src/Audio/juno_synth.d \
./Src/Audio/microdexed_synth.d \
./Src/Audio/tb3_synth.d \
./Src/Audio/monob_moog_ladder.d \
./Src/Audio/monob_osc_bank.d \
./Src/Audio/moogladder.d \
./Src/Audio/oscillator.d 


# Each subdirectory must supply rules for building sources it contributes
Src/Audio/%.o Src/Audio/%.su Src/Audio/%.cyclo: ../Src/Audio/%.c Src/Audio/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Drv_app/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Core" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/MIDI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Param" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Storage" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/SD" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/UI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/U8g2" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/Third_Party/FatFs/src" -O3 -ffunction-sections -fdata-sections -Wall -fno-math-errno -ffast-math -fsingle-precision-constant -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Src/Audio/%.o Src/Audio/%.su Src/Audio/%.cyclo: ../Src/Audio/%.cpp Src/Audio/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m7 -std=gnu++14 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Micro_Dexed" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Juno" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Daisy_SP" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments/stmlib" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/freeverb-main/Components" -O3 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -Wall -ffast-math -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Src-2f-Audio

clean-Src-2f-Audio:
	-$(RM) ./Src/Audio/audio.cyclo ./Src/Audio/audio.d ./Src/Audio/audio.o ./Src/Audio/audio.su ./Src/Audio/audio_float.cyclo ./Src/Audio/audio_float.d ./Src/Audio/audio_float.o ./Src/Audio/audio_float.su ./Src/Audio/audio_io.cyclo ./Src/Audio/audio_io.d ./Src/Audio/audio_io.o ./Src/Audio/audio_io.su ./Src/Audio/dsp_engine.cyclo ./Src/Audio/dsp_engine.d ./Src/Audio/dsp_engine.o ./Src/Audio/dsp_engine.su ./Src/Audio/fx_BusCompressorCore.cyclo ./Src/Audio/fx_BusCompressorCore.d ./Src/Audio/fx_BusCompressorCore.o ./Src/Audio/fx_BusCompressorCore.su ./Src/Audio/fx_Daisy_comp_core.cyclo ./Src/Audio/fx_Daisy_comp_core.d ./Src/Audio/fx_Daisy_comp_core.o ./Src/Audio/fx_Daisy_comp_core.su ./Src/Audio/fx_biquad_filter.cyclo ./Src/Audio/fx_biquad_filter.d ./Src/Audio/fx_biquad_filter.o ./Src/Audio/fx_biquad_filter.su ./Src/Audio/fx_bus_compressor.cyclo ./Src/Audio/fx_bus_compressor.d ./Src/Audio/fx_bus_compressor.o ./Src/Audio/fx_bus_compressor.su ./Src/Audio/fx_chain.cyclo ./Src/Audio/fx_chain.d ./Src/Audio/fx_chain.o ./Src/Audio/fx_chain.su ./Src/Audio/fx_clouds.cyclo ./Src/Audio/fx_clouds.d ./Src/Audio/fx_clouds.o ./Src/Audio/fx_clouds.su ./Src/Audio/fx_daisy_comp.cyclo ./Src/Audio/fx_daisy_comp.d ./Src/Audio/fx_daisy_comp.o ./Src/Audio/fx_daisy_comp.su ./Src/Audio/fx_dj_eq3_cmsis.cyclo ./Src/Audio/fx_dj_eq3_cmsis.d ./Src/Audio/fx_dj_eq3_cmsis.o ./Src/Audio/fx_dj_eq3_cmsis.su ./Src/Audio/fx_filter_ladder_moog.cyclo ./Src/Audio/fx_filter_ladder_moog.d ./Src/Audio/fx_filter_ladder_moog.o ./Src/Audio/fx_filter_ladder_moog.su ./Src/Audio/fx_granular.cyclo ./Src/Audio/fx_granular.d ./Src/Audio/fx_granular.o ./Src/Audio/fx_granular.su ./Src/Audio/fx_onepole.cyclo ./Src/Audio/fx_onepole.d ./Src/Audio/fx_onepole.o ./Src/Audio/fx_onepole.su ./Src/Audio/fx_pool.cyclo ./Src/Audio/fx_pool.d ./Src/Audio/fx_pool.o ./Src/Audio/fx_pool.su ./Src/Audio/fx_reverb.cyclo ./Src/Audio/fx_reverb.d ./Src/Audio/fx_reverb.o ./Src/Audio/fx_reverb.su ./Src/Audio/fx_saturation.cyclo ./Src/Audio/fx_saturation.d ./Src/Audio/fx_saturation.o ./Src/Audio/fx_saturation.su ./Src/Audio/fx_svf.cyclo ./Src/Audio/fx_svf.d ./Src/Audio/fx_svf.o ./Src/Audio/fx_svf.su ./Src/Audio/juno_midi_queue.cyclo ./Src/Audio/juno_midi_queue.d ./Src/Audio/juno_midi_queue.o ./Src/Audio/juno_midi_queue.su ./Src/Audio/juno_synth.cyclo ./Src/Audio/juno_synth.d ./Src/Audio/juno_synth.o ./Src/Audio/juno_synth.su ./Src/Audio/live_recorder.cyclo ./Src/Audio/live_recorder.d ./Src/Audio/live_recorder.o ./Src/Audio/live_recorder.su ./Src/Audio/microdexed_synth.cyclo ./Src/Audio/microdexed_synth.d ./Src/Audio/microdexed_synth.o ./Src/Audio/microdexed_synth.su ./Src/Audio/tb3_synth.cyclo ./Src/Audio/tb3_synth.d ./Src/Audio/tb3_synth.o ./Src/Audio/tb3_synth.su ./Src/Audio/mixer.cyclo ./Src/Audio/mixer.d ./Src/Audio/mixer.o ./Src/Audio/mixer.su ./Src/Audio/monob_moog_ladder.cyclo ./Src/Audio/monob_moog_ladder.d ./Src/Audio/monob_moog_ladder.o ./Src/Audio/monob_moog_ladder.su ./Src/Audio/monob_osc_bank.cyclo ./Src/Audio/monob_osc_bank.d ./Src/Audio/monob_osc_bank.o ./Src/Audio/monob_osc_bank.su ./Src/Audio/monob_synth.cyclo ./Src/Audio/monob_synth.d ./Src/Audio/monob_synth.o ./Src/Audio/monob_synth.su ./Src/Audio/moogladder.cyclo ./Src/Audio/moogladder.d ./Src/Audio/moogladder.o ./Src/Audio/moogladder.su ./Src/Audio/oscillator.cyclo ./Src/Audio/oscillator.d ./Src/Audio/oscillator.o ./Src/Audio/oscillator.su ./Src/Audio/recorder_transport.cyclo ./Src/Audio/recorder_transport.d ./Src/Audio/recorder_transport.o ./Src/Audio/recorder_transport.su ./Src/Audio/sampler.cyclo ./Src/Audio/sampler.d ./Src/Audio/sampler.o ./Src/Audio/sampler.su ./Src/Audio/sd_multitrack_recorder.cyclo ./Src/Audio/sd_multitrack_recorder.d ./Src/Audio/sd_multitrack_recorder.o ./Src/Audio/sd_multitrack_recorder.su

.PHONY: clean-Src-2f-Audio

