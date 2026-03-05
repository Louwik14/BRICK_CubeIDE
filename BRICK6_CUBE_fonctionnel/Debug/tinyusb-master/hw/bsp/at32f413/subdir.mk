################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../tinyusb-master/hw/bsp/at32f413/at32f413_clock.c \
../tinyusb-master/hw/bsp/at32f413/at32f413_int.c \
../tinyusb-master/hw/bsp/at32f413/family.c 

OBJS += \
./tinyusb-master/hw/bsp/at32f413/at32f413_clock.o \
./tinyusb-master/hw/bsp/at32f413/at32f413_int.o \
./tinyusb-master/hw/bsp/at32f413/family.o 

C_DEPS += \
./tinyusb-master/hw/bsp/at32f413/at32f413_clock.d \
./tinyusb-master/hw/bsp/at32f413/at32f413_int.d \
./tinyusb-master/hw/bsp/at32f413/family.d 


# Each subdirectory must supply rules for building sources it contributes
tinyusb-master/hw/bsp/at32f413/%.o tinyusb-master/hw/bsp/at32f413/%.su tinyusb-master/hw/bsp/at32f413/%.cyclo: ../tinyusb-master/hw/bsp/at32f413/%.c tinyusb-master/hw/bsp/at32f413/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"/BRICK6_CUBE/tinyusb-master" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-tinyusb-2d-master-2f-hw-2f-bsp-2f-at32f413

clean-tinyusb-2d-master-2f-hw-2f-bsp-2f-at32f413:
	-$(RM) ./tinyusb-master/hw/bsp/at32f413/at32f413_clock.cyclo ./tinyusb-master/hw/bsp/at32f413/at32f413_clock.d ./tinyusb-master/hw/bsp/at32f413/at32f413_clock.o ./tinyusb-master/hw/bsp/at32f413/at32f413_clock.su ./tinyusb-master/hw/bsp/at32f413/at32f413_int.cyclo ./tinyusb-master/hw/bsp/at32f413/at32f413_int.d ./tinyusb-master/hw/bsp/at32f413/at32f413_int.o ./tinyusb-master/hw/bsp/at32f413/at32f413_int.su ./tinyusb-master/hw/bsp/at32f413/family.cyclo ./tinyusb-master/hw/bsp/at32f413/family.d ./tinyusb-master/hw/bsp/at32f413/family.o ./tinyusb-master/hw/bsp/at32f413/family.su

.PHONY: clean-tinyusb-2d-master-2f-hw-2f-bsp-2f-at32f413

