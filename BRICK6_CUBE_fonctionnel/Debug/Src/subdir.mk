################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Src/audio_in.c \
../Src/audio_out.c \
../Src/cs42448.c \
../Src/dma.c \
../Src/gpio.c \
../Src/i2c.c \
../Src/main.c \
../Src/midi.c \
../Src/midi_host.c \
../Src/sai.c \
../Src/stm32h7xx_hal_msp.c \
../Src/stm32h7xx_it.c \
../Src/syscalls.c \
../Src/sysmem.c \
../Src/system_stm32h7xx.c \
../Src/usart.c \
../Src/usb_device.c \
../Src/usb_host.c \
../Src/usbd_conf.c \
../Src/usbd_desc.c \
../Src/usbh_conf.c \
../Src/usbh_midi.c 

OBJS += \
./Src/audio_in.o \
./Src/audio_out.o \
./Src/cs42448.o \
./Src/dma.o \
./Src/gpio.o \
./Src/i2c.o \
./Src/main.o \
./Src/midi.o \
./Src/midi_host.o \
./Src/sai.o \
./Src/stm32h7xx_hal_msp.o \
./Src/stm32h7xx_it.o \
./Src/syscalls.o \
./Src/sysmem.o \
./Src/system_stm32h7xx.o \
./Src/usart.o \
./Src/usb_device.o \
./Src/usb_host.o \
./Src/usbd_conf.o \
./Src/usbd_desc.o \
./Src/usbh_conf.o \
./Src/usbh_midi.o 

C_DEPS += \
./Src/audio_in.d \
./Src/audio_out.d \
./Src/cs42448.d \
./Src/dma.d \
./Src/gpio.d \
./Src/i2c.d \
./Src/main.d \
./Src/midi.d \
./Src/midi_host.d \
./Src/sai.d \
./Src/stm32h7xx_hal_msp.d \
./Src/stm32h7xx_it.d \
./Src/syscalls.d \
./Src/sysmem.d \
./Src/system_stm32h7xx.d \
./Src/usart.d \
./Src/usb_device.d \
./Src/usb_host.d \
./Src/usbd_conf.d \
./Src/usbd_desc.d \
./Src/usbh_conf.d \
./Src/usbh_midi.d 


# Each subdirectory must supply rules for building sources it contributes
Src/%.o Src/%.su Src/%.cyclo: ../Src/%.c Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/HID/Inc -I../Middlewares/ST/STM32_USB_Host_Library/Core/Inc -I../Middlewares/ST/STM32_USB_Host_Library/Class/AUDIO/Inc -I../Middlewares/ST/STM32_USB_Host_Library/Class/CDC/Inc -I../Middlewares/ST/STM32_USB_Host_Library/Class/HID/Inc -I../Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Inc -I../Middlewares/ST/STM32_USB_Host_Library/Class/MTP/Inc -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Src

clean-Src:
	-$(RM) ./Src/audio_in.cyclo ./Src/audio_in.d ./Src/audio_in.o ./Src/audio_in.su ./Src/audio_out.cyclo ./Src/audio_out.d ./Src/audio_out.o ./Src/audio_out.su ./Src/cs42448.cyclo ./Src/cs42448.d ./Src/cs42448.o ./Src/cs42448.su ./Src/dma.cyclo ./Src/dma.d ./Src/dma.o ./Src/dma.su ./Src/gpio.cyclo ./Src/gpio.d ./Src/gpio.o ./Src/gpio.su ./Src/i2c.cyclo ./Src/i2c.d ./Src/i2c.o ./Src/i2c.su ./Src/main.cyclo ./Src/main.d ./Src/main.o ./Src/main.su ./Src/midi.cyclo ./Src/midi.d ./Src/midi.o ./Src/midi.su ./Src/midi_host.cyclo ./Src/midi_host.d ./Src/midi_host.o ./Src/midi_host.su ./Src/sai.cyclo ./Src/sai.d ./Src/sai.o ./Src/sai.su ./Src/stm32h7xx_hal_msp.cyclo ./Src/stm32h7xx_hal_msp.d ./Src/stm32h7xx_hal_msp.o ./Src/stm32h7xx_hal_msp.su ./Src/stm32h7xx_it.cyclo ./Src/stm32h7xx_it.d ./Src/stm32h7xx_it.o ./Src/stm32h7xx_it.su ./Src/syscalls.cyclo ./Src/syscalls.d ./Src/syscalls.o ./Src/syscalls.su ./Src/sysmem.cyclo ./Src/sysmem.d ./Src/sysmem.o ./Src/sysmem.su ./Src/system_stm32h7xx.cyclo ./Src/system_stm32h7xx.d ./Src/system_stm32h7xx.o ./Src/system_stm32h7xx.su ./Src/usart.cyclo ./Src/usart.d ./Src/usart.o ./Src/usart.su ./Src/usb_device.cyclo ./Src/usb_device.d ./Src/usb_device.o ./Src/usb_device.su ./Src/usb_host.cyclo ./Src/usb_host.d ./Src/usb_host.o ./Src/usb_host.su ./Src/usbd_conf.cyclo ./Src/usbd_conf.d ./Src/usbd_conf.o ./Src/usbd_conf.su ./Src/usbd_desc.cyclo ./Src/usbd_desc.d ./Src/usbd_desc.o ./Src/usbd_desc.su ./Src/usbh_conf.cyclo ./Src/usbh_conf.d ./Src/usbh_conf.o ./Src/usbh_conf.su ./Src/usbh_midi.cyclo ./Src/usbh_midi.d ./Src/usbh_midi.o ./Src/usbh_midi.su

.PHONY: clean-Src

