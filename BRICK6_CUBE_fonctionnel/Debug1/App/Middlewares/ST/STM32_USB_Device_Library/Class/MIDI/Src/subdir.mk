################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Src/usbd_midi.c 

C_DEPS += \
./App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Src/usbd_midi.d 

OBJS += \
./App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Src/usbd_midi.o 


# Each subdirectory must supply rules for building sources it contributes
App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Src/%.o App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Src/%.su App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Src/%.cyclo: ../App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Src/%.c App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Drv_app/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Core" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/MIDI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Param" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Storage" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/UI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/SD" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/U8g2" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/Third_Party/FatFs/src" -Og -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-App-2f-Middlewares-2f-ST-2f-STM32_USB_Device_Library-2f-Class-2f-MIDI-2f-Src

clean-App-2f-Middlewares-2f-ST-2f-STM32_USB_Device_Library-2f-Class-2f-MIDI-2f-Src:
	-$(RM) ./App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Src/usbd_midi.cyclo ./App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Src/usbd_midi.d ./App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Src/usbd_midi.o ./App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Src/usbd_midi.su

.PHONY: clean-App-2f-Middlewares-2f-ST-2f-STM32_USB_Device_Library-2f-Class-2f-MIDI-2f-Src

