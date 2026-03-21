################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../Micro_Dexed/third-party/MD_REncoder/src/MD_REncoder.cpp 

OBJS += \
./Micro_Dexed/third-party/MD_REncoder/src/MD_REncoder.o 

CPP_DEPS += \
./Micro_Dexed/third-party/MD_REncoder/src/MD_REncoder.d 


# Each subdirectory must supply rules for building sources it contributes
Micro_Dexed/third-party/MD_REncoder/src/%.o Micro_Dexed/third-party/MD_REncoder/src/%.su Micro_Dexed/third-party/MD_REncoder/src/%.cyclo: ../Micro_Dexed/third-party/MD_REncoder/src/%.cpp Micro_Dexed/third-party/MD_REncoder/src/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m7 -std=gnu++14 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Micro_Dexed" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Juno" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Daisy_SP" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments/stmlib" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/freeverb-main/Components" -O3 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -ffast-math -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Micro_Dexed-2f-third-2d-party-2f-MD_REncoder-2f-src

clean-Micro_Dexed-2f-third-2d-party-2f-MD_REncoder-2f-src:
	-$(RM) ./Micro_Dexed/third-party/MD_REncoder/src/MD_REncoder.cyclo ./Micro_Dexed/third-party/MD_REncoder/src/MD_REncoder.d ./Micro_Dexed/third-party/MD_REncoder/src/MD_REncoder.o ./Micro_Dexed/third-party/MD_REncoder/src/MD_REncoder.su

.PHONY: clean-Micro_Dexed-2f-third-2d-party-2f-MD_REncoder-2f-src

