################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../tinyusb/src/portable/ohci/ohci.c 

OBJS += \
./tinyusb/src/portable/ohci/ohci.o 

C_DEPS += \
./tinyusb/src/portable/ohci/ohci.d 


# Each subdirectory must supply rules for building sources it contributes
tinyusb/src/portable/ohci/%.o tinyusb/src/portable/ohci/%.su tinyusb/src/portable/ohci/%.cyclo: ../tinyusb/src/portable/ohci/%.c tinyusb/src/portable/ohci/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb/src" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-tinyusb-2f-src-2f-portable-2f-ohci

clean-tinyusb-2f-src-2f-portable-2f-ohci:
	-$(RM) ./tinyusb/src/portable/ohci/ohci.cyclo ./tinyusb/src/portable/ohci/ohci.d ./tinyusb/src/portable/ohci/ohci.o ./tinyusb/src/portable/ohci/ohci.su

.PHONY: clean-tinyusb-2f-src-2f-portable-2f-ohci

