################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Middlewares/ST/STM32_USB_Host_Library/Class/MTP/Src/usbh_mtp.c \
../Middlewares/ST/STM32_USB_Host_Library/Class/MTP/Src/usbh_mtp_ptp.c 

OBJS += \
./Middlewares/ST/STM32_USB_Host_Library/Class/MTP/Src/usbh_mtp.o \
./Middlewares/ST/STM32_USB_Host_Library/Class/MTP/Src/usbh_mtp_ptp.o 

C_DEPS += \
./Middlewares/ST/STM32_USB_Host_Library/Class/MTP/Src/usbh_mtp.d \
./Middlewares/ST/STM32_USB_Host_Library/Class/MTP/Src/usbh_mtp_ptp.d 


# Each subdirectory must supply rules for building sources it contributes
Middlewares/ST/STM32_USB_Host_Library/Class/MTP/Src/%.o Middlewares/ST/STM32_USB_Host_Library/Class/MTP/Src/%.su Middlewares/ST/STM32_USB_Host_Library/Class/MTP/Src/%.cyclo: ../Middlewares/ST/STM32_USB_Host_Library/Class/MTP/Src/%.c Middlewares/ST/STM32_USB_Host_Library/Class/MTP/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/HID/Inc -I../Middlewares/ST/STM32_USB_Host_Library/Core/Inc -I../Middlewares/ST/STM32_USB_Host_Library/Class/AUDIO/Inc -I../Middlewares/ST/STM32_USB_Host_Library/Class/CDC/Inc -I../Middlewares/ST/STM32_USB_Host_Library/Class/HID/Inc -I../Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Inc -I../Middlewares/ST/STM32_USB_Host_Library/Class/MTP/Inc -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Middlewares-2f-ST-2f-STM32_USB_Host_Library-2f-Class-2f-MTP-2f-Src

clean-Middlewares-2f-ST-2f-STM32_USB_Host_Library-2f-Class-2f-MTP-2f-Src:
	-$(RM) ./Middlewares/ST/STM32_USB_Host_Library/Class/MTP/Src/usbh_mtp.cyclo ./Middlewares/ST/STM32_USB_Host_Library/Class/MTP/Src/usbh_mtp.d ./Middlewares/ST/STM32_USB_Host_Library/Class/MTP/Src/usbh_mtp.o ./Middlewares/ST/STM32_USB_Host_Library/Class/MTP/Src/usbh_mtp.su ./Middlewares/ST/STM32_USB_Host_Library/Class/MTP/Src/usbh_mtp_ptp.cyclo ./Middlewares/ST/STM32_USB_Host_Library/Class/MTP/Src/usbh_mtp_ptp.d ./Middlewares/ST/STM32_USB_Host_Library/Class/MTP/Src/usbh_mtp_ptp.o ./Middlewares/ST/STM32_USB_Host_Library/Class/MTP/Src/usbh_mtp_ptp.su

.PHONY: clean-Middlewares-2f-ST-2f-STM32_USB_Host_Library-2f-Class-2f-MTP-2f-Src

