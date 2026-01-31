################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../App/TinyUSB/src/portable/st/typec/typec_stm32.c 

OBJS += \
./App/TinyUSB/src/portable/st/typec/typec_stm32.o 

C_DEPS += \
./App/TinyUSB/src/portable/st/typec/typec_stm32.d 


# Each subdirectory must supply rules for building sources it contributes
App/TinyUSB/src/portable/st/typec/%.o App/TinyUSB/src/portable/st/typec/%.su App/TinyUSB/src/portable/st/typec/%.cyclo: ../App/TinyUSB/src/portable/st/typec/%.c App/TinyUSB/src/portable/st/typec/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack/tinyusb_audio_midi_h7" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/hw" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/src" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-App-2f-TinyUSB-2f-src-2f-portable-2f-st-2f-typec

clean-App-2f-TinyUSB-2f-src-2f-portable-2f-st-2f-typec:
	-$(RM) ./App/TinyUSB/src/portable/st/typec/typec_stm32.cyclo ./App/TinyUSB/src/portable/st/typec/typec_stm32.d ./App/TinyUSB/src/portable/st/typec/typec_stm32.o ./App/TinyUSB/src/portable/st/typec/typec_stm32.su

.PHONY: clean-App-2f-TinyUSB-2f-src-2f-portable-2f-st-2f-typec

