################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc.c \
../App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc_bot.c \
../App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc_scsi.c 

OBJS += \
./App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc.o \
./App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc_bot.o \
./App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc_scsi.o 

C_DEPS += \
./App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc.d \
./App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc_bot.d \
./App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc_scsi.d 


# Each subdirectory must supply rules for building sources it contributes
App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/%.o App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/%.su App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/%.cyclo: ../App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/%.c App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-App-2f-Middlewares-2f-ST-2f-STM32_USB_Host_Library-2f-Class-2f-MSC-2f-Src

clean-App-2f-Middlewares-2f-ST-2f-STM32_USB_Host_Library-2f-Class-2f-MSC-2f-Src:
	-$(RM) ./App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc.cyclo ./App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc.d ./App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc.o ./App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc.su ./App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc_bot.cyclo ./App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc_bot.d ./App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc_bot.o ./App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc_bot.su ./App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc_scsi.cyclo ./App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc_scsi.d ./App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc_scsi.o ./App/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc_scsi.su

.PHONY: clean-App-2f-Middlewares-2f-ST-2f-STM32_USB_Host_Library-2f-Class-2f-MSC-2f-Src

