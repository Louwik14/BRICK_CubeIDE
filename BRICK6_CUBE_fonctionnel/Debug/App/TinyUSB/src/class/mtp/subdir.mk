################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../App/TinyUSB/src/class/mtp/mtp_device.c 

OBJS += \
./App/TinyUSB/src/class/mtp/mtp_device.o 

C_DEPS += \
./App/TinyUSB/src/class/mtp/mtp_device.d 


# Each subdirectory must supply rules for building sources it contributes
App/TinyUSB/src/class/mtp/%.o App/TinyUSB/src/class/mtp/%.su App/TinyUSB/src/class/mtp/%.cyclo: ../App/TinyUSB/src/class/mtp/%.c App/TinyUSB/src/class/mtp/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack/tinyusb_audio_midi_h7" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/hw" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/src" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-App-2f-TinyUSB-2f-src-2f-class-2f-mtp

clean-App-2f-TinyUSB-2f-src-2f-class-2f-mtp:
	-$(RM) ./App/TinyUSB/src/class/mtp/mtp_device.cyclo ./App/TinyUSB/src/class/mtp/mtp_device.d ./App/TinyUSB/src/class/mtp/mtp_device.o ./App/TinyUSB/src/class/mtp/mtp_device.su

.PHONY: clean-App-2f-TinyUSB-2f-src-2f-class-2f-mtp

