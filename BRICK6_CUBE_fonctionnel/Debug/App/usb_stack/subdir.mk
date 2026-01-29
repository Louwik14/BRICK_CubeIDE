################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../App/usb_stack/usb_device.c \
../App/usb_stack/usb_host.c \
../App/usb_stack/usbd_brick6_composite.c \
../App/usb_stack/usbd_conf.c \
../App/usb_stack/usbd_desc.c \
../App/usb_stack/usbh_conf.c 

OBJS += \
./App/usb_stack/usb_device.o \
./App/usb_stack/usb_host.o \
./App/usb_stack/usbd_brick6_composite.o \
./App/usb_stack/usbd_conf.o \
./App/usb_stack/usbd_desc.o \
./App/usb_stack/usbh_conf.o 

C_DEPS += \
./App/usb_stack/usb_device.d \
./App/usb_stack/usb_host.d \
./App/usb_stack/usbd_brick6_composite.d \
./App/usb_stack/usbd_conf.d \
./App/usb_stack/usbd_desc.d \
./App/usb_stack/usbh_conf.d 


# Each subdirectory must supply rules for building sources it contributes
App/usb_stack/%.o App/usb_stack/%.su App/usb_stack/%.cyclo: ../App/usb_stack/%.c App/usb_stack/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-App-2f-usb_stack

clean-App-2f-usb_stack:
	-$(RM) ./App/usb_stack/usb_device.cyclo ./App/usb_stack/usb_device.d ./App/usb_stack/usb_device.o ./App/usb_stack/usb_device.su ./App/usb_stack/usb_host.cyclo ./App/usb_stack/usb_host.d ./App/usb_stack/usb_host.o ./App/usb_stack/usb_host.su ./App/usb_stack/usbd_brick6_composite.cyclo ./App/usb_stack/usbd_brick6_composite.d ./App/usb_stack/usbd_brick6_composite.o ./App/usb_stack/usbd_brick6_composite.su ./App/usb_stack/usbd_conf.cyclo ./App/usb_stack/usbd_conf.d ./App/usb_stack/usbd_conf.o ./App/usb_stack/usbd_conf.su ./App/usb_stack/usbd_desc.cyclo ./App/usb_stack/usbd_desc.d ./App/usb_stack/usbd_desc.o ./App/usb_stack/usbd_desc.su ./App/usb_stack/usbh_conf.cyclo ./App/usb_stack/usbh_conf.d ./App/usb_stack/usbh_conf.o ./App/usb_stack/usbh_conf.su

.PHONY: clean-App-2f-usb_stack

