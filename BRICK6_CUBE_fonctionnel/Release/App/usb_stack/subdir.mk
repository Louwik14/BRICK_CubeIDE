################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../App/usb_stack/usb_device.c \
../App/usb_stack/usb_host.c \
../App/usb_stack/usbd_conf.c \
../App/usb_stack/usbd_desc.c \
../App/usb_stack/usbh_conf.c 

C_DEPS += \
./App/usb_stack/usb_device.d \
./App/usb_stack/usb_host.d \
./App/usb_stack/usbd_conf.d \
./App/usb_stack/usbd_desc.d \
./App/usb_stack/usbh_conf.d 

OBJS += \
./App/usb_stack/usb_device.o \
./App/usb_stack/usb_host.o \
./App/usb_stack/usbd_conf.o \
./App/usb_stack/usbd_desc.o \
./App/usb_stack/usbh_conf.o 


# Each subdirectory must supply rules for building sources it contributes
App/usb_stack/%.o App/usb_stack/%.su App/usb_stack/%.cyclo: ../App/usb_stack/%.c App/usb_stack/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Drv_app/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Core" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/MIDI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Param" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Storage" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/SD" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/UI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/U8g2" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/Third_Party/FatFs/src" -O3 -ffunction-sections -fdata-sections -Wall -fno-math-errno -ffast-math -fsingle-precision-constant -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-App-2f-usb_stack

clean-App-2f-usb_stack:
	-$(RM) ./App/usb_stack/usb_device.cyclo ./App/usb_stack/usb_device.d ./App/usb_stack/usb_device.o ./App/usb_stack/usb_device.su ./App/usb_stack/usb_host.cyclo ./App/usb_stack/usb_host.d ./App/usb_stack/usb_host.o ./App/usb_stack/usb_host.su ./App/usb_stack/usbd_conf.cyclo ./App/usb_stack/usbd_conf.d ./App/usb_stack/usbd_conf.o ./App/usb_stack/usbd_conf.su ./App/usb_stack/usbd_desc.cyclo ./App/usb_stack/usbd_desc.d ./App/usb_stack/usbd_desc.o ./App/usb_stack/usbd_desc.su ./App/usb_stack/usbh_conf.cyclo ./App/usb_stack/usbh_conf.d ./App/usb_stack/usbh_conf.o ./App/usb_stack/usbh_conf.su

.PHONY: clean-App-2f-usb_stack

