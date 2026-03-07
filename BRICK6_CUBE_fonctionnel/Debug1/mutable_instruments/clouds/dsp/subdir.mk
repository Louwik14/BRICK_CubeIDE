################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../mutable_instruments/clouds/dsp/correlator.cpp \
../mutable_instruments/clouds/dsp/granular_processor.cpp \
../mutable_instruments/clouds/dsp/mu_law.cpp 

OBJS += \
./mutable_instruments/clouds/dsp/correlator.o \
./mutable_instruments/clouds/dsp/granular_processor.o \
./mutable_instruments/clouds/dsp/mu_law.o 

CPP_DEPS += \
./mutable_instruments/clouds/dsp/correlator.d \
./mutable_instruments/clouds/dsp/granular_processor.d \
./mutable_instruments/clouds/dsp/mu_law.d 


# Each subdirectory must supply rules for building sources it contributes
mutable_instruments/clouds/dsp/%.o mutable_instruments/clouds/dsp/%.su mutable_instruments/clouds/dsp/%.cyclo: ../mutable_instruments/clouds/dsp/%.cpp mutable_instruments/clouds/dsp/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m7 -std=gnu++14 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Daisy_SP" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments/stmlib" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/freeverb-main/Components" -I../Middlewares/Third_Party/FatFs/src -O0 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-mutable_instruments-2f-clouds-2f-dsp

clean-mutable_instruments-2f-clouds-2f-dsp:
	-$(RM) ./mutable_instruments/clouds/dsp/correlator.cyclo ./mutable_instruments/clouds/dsp/correlator.d ./mutable_instruments/clouds/dsp/correlator.o ./mutable_instruments/clouds/dsp/correlator.su ./mutable_instruments/clouds/dsp/granular_processor.cyclo ./mutable_instruments/clouds/dsp/granular_processor.d ./mutable_instruments/clouds/dsp/granular_processor.o ./mutable_instruments/clouds/dsp/granular_processor.su ./mutable_instruments/clouds/dsp/mu_law.cyclo ./mutable_instruments/clouds/dsp/mu_law.d ./mutable_instruments/clouds/dsp/mu_law.o ./mutable_instruments/clouds/dsp/mu_law.su

.PHONY: clean-mutable_instruments-2f-clouds-2f-dsp

