################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../App/usb_stack/tinyusb_audio_midi_h7/tusb_stm32_h7.c \
../App/usb_stack/tinyusb_audio_midi_h7/usb_descriptors.c 

OBJS += \
./App/usb_stack/tinyusb_audio_midi_h7/tusb_stm32_h7.o \
./App/usb_stack/tinyusb_audio_midi_h7/usb_descriptors.o 

C_DEPS += \
./App/usb_stack/tinyusb_audio_midi_h7/tusb_stm32_h7.d \
./App/usb_stack/tinyusb_audio_midi_h7/usb_descriptors.d 


# Each subdirectory must supply rules for building sources it contributes
App/usb_stack/tinyusb_audio_midi_h7/%.o App/usb_stack/tinyusb_audio_midi_h7/%.su App/usb_stack/tinyusb_audio_midi_h7/%.cyclo: ../App/usb_stack/tinyusb_audio_midi_h7/%.c App/usb_stack/tinyusb_audio_midi_h7/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/src" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -include"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack/tinyusb_audio_midi_h7/tusb_config.h" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-App-2f-usb_stack-2f-tinyusb_audio_midi_h7

clean-App-2f-usb_stack-2f-tinyusb_audio_midi_h7:
	-$(RM) ./App/usb_stack/tinyusb_audio_midi_h7/tusb_stm32_h7.cyclo ./App/usb_stack/tinyusb_audio_midi_h7/tusb_stm32_h7.d ./App/usb_stack/tinyusb_audio_midi_h7/tusb_stm32_h7.o ./App/usb_stack/tinyusb_audio_midi_h7/tusb_stm32_h7.su ./App/usb_stack/tinyusb_audio_midi_h7/usb_descriptors.cyclo ./App/usb_stack/tinyusb_audio_midi_h7/usb_descriptors.d ./App/usb_stack/tinyusb_audio_midi_h7/usb_descriptors.o ./App/usb_stack/tinyusb_audio_midi_h7/usb_descriptors.su

.PHONY: clean-App-2f-usb_stack-2f-tinyusb_audio_midi_h7

