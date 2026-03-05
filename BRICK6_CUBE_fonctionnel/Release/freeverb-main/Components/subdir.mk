################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../freeverb-main/Components/allpass.cpp \
../freeverb-main/Components/comb.cpp \
../freeverb-main/Components/revmodel.cpp 

OBJS += \
./freeverb-main/Components/allpass.o \
./freeverb-main/Components/comb.o \
./freeverb-main/Components/revmodel.o 

CPP_DEPS += \
./freeverb-main/Components/allpass.d \
./freeverb-main/Components/comb.d \
./freeverb-main/Components/revmodel.d 


# Each subdirectory must supply rules for building sources it contributes
freeverb-main/Components/%.o freeverb-main/Components/%.su freeverb-main/Components/%.cyclo: ../freeverb-main/Components/%.cpp freeverb-main/Components/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m7 -std=gnu++14 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/freeverb-main/Components" -Os -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-freeverb-2d-main-2f-Components

clean-freeverb-2d-main-2f-Components:
	-$(RM) ./freeverb-main/Components/allpass.cyclo ./freeverb-main/Components/allpass.d ./freeverb-main/Components/allpass.o ./freeverb-main/Components/allpass.su ./freeverb-main/Components/comb.cyclo ./freeverb-main/Components/comb.d ./freeverb-main/Components/comb.o ./freeverb-main/Components/comb.su ./freeverb-main/Components/revmodel.cyclo ./freeverb-main/Components/revmodel.d ./freeverb-main/Components/revmodel.o ./freeverb-main/Components/revmodel.su

.PHONY: clean-freeverb-2d-main-2f-Components

