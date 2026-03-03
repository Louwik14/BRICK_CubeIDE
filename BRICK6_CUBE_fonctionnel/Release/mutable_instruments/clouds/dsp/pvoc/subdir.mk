################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../mutable_instruments/clouds/dsp/pvoc/frame_transformation.cpp \
../mutable_instruments/clouds/dsp/pvoc/phase_vocoder.cpp \
../mutable_instruments/clouds/dsp/pvoc/stft.cpp 

OBJS += \
./mutable_instruments/clouds/dsp/pvoc/frame_transformation.o \
./mutable_instruments/clouds/dsp/pvoc/phase_vocoder.o \
./mutable_instruments/clouds/dsp/pvoc/stft.o 

CPP_DEPS += \
./mutable_instruments/clouds/dsp/pvoc/frame_transformation.d \
./mutable_instruments/clouds/dsp/pvoc/phase_vocoder.d \
./mutable_instruments/clouds/dsp/pvoc/stft.d 


# Each subdirectory must supply rules for building sources it contributes
mutable_instruments/clouds/dsp/pvoc/%.o mutable_instruments/clouds/dsp/pvoc/%.su mutable_instruments/clouds/dsp/pvoc/%.cyclo: ../mutable_instruments/clouds/dsp/pvoc/%.cpp mutable_instruments/clouds/dsp/pvoc/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m7 -std=gnu++14 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Daisy_SP" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments/stmlib" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/freeverb-main/Components" -O3 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -ffast-math -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-mutable_instruments-2f-clouds-2f-dsp-2f-pvoc

clean-mutable_instruments-2f-clouds-2f-dsp-2f-pvoc:
	-$(RM) ./mutable_instruments/clouds/dsp/pvoc/frame_transformation.cyclo ./mutable_instruments/clouds/dsp/pvoc/frame_transformation.d ./mutable_instruments/clouds/dsp/pvoc/frame_transformation.o ./mutable_instruments/clouds/dsp/pvoc/frame_transformation.su ./mutable_instruments/clouds/dsp/pvoc/phase_vocoder.cyclo ./mutable_instruments/clouds/dsp/pvoc/phase_vocoder.d ./mutable_instruments/clouds/dsp/pvoc/phase_vocoder.o ./mutable_instruments/clouds/dsp/pvoc/phase_vocoder.su ./mutable_instruments/clouds/dsp/pvoc/stft.cyclo ./mutable_instruments/clouds/dsp/pvoc/stft.d ./mutable_instruments/clouds/dsp/pvoc/stft.o ./mutable_instruments/clouds/dsp/pvoc/stft.su

.PHONY: clean-mutable_instruments-2f-clouds-2f-dsp-2f-pvoc

