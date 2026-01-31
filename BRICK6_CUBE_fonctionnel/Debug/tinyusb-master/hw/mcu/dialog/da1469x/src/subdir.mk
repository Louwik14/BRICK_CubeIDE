################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../tinyusb-master/hw/mcu/dialog/da1469x/src/da1469x_clock.c \
../tinyusb-master/hw/mcu/dialog/da1469x/src/hal_gpio.c \
../tinyusb-master/hw/mcu/dialog/da1469x/src/hal_system.c \
../tinyusb-master/hw/mcu/dialog/da1469x/src/hal_system_start.c \
../tinyusb-master/hw/mcu/dialog/da1469x/src/system_da1469x.c 

OBJS += \
./tinyusb-master/hw/mcu/dialog/da1469x/src/da1469x_clock.o \
./tinyusb-master/hw/mcu/dialog/da1469x/src/hal_gpio.o \
./tinyusb-master/hw/mcu/dialog/da1469x/src/hal_system.o \
./tinyusb-master/hw/mcu/dialog/da1469x/src/hal_system_start.o \
./tinyusb-master/hw/mcu/dialog/da1469x/src/system_da1469x.o 

C_DEPS += \
./tinyusb-master/hw/mcu/dialog/da1469x/src/da1469x_clock.d \
./tinyusb-master/hw/mcu/dialog/da1469x/src/hal_gpio.d \
./tinyusb-master/hw/mcu/dialog/da1469x/src/hal_system.d \
./tinyusb-master/hw/mcu/dialog/da1469x/src/hal_system_start.d \
./tinyusb-master/hw/mcu/dialog/da1469x/src/system_da1469x.d 


# Each subdirectory must supply rules for building sources it contributes
tinyusb-master/hw/mcu/dialog/da1469x/src/%.o tinyusb-master/hw/mcu/dialog/da1469x/src/%.su tinyusb-master/hw/mcu/dialog/da1469x/src/%.cyclo: ../tinyusb-master/hw/mcu/dialog/da1469x/src/%.c tinyusb-master/hw/mcu/dialog/da1469x/src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"/BRICK6_CUBE/tinyusb-master" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-tinyusb-2d-master-2f-hw-2f-mcu-2f-dialog-2f-da1469x-2f-src

clean-tinyusb-2d-master-2f-hw-2f-mcu-2f-dialog-2f-da1469x-2f-src:
	-$(RM) ./tinyusb-master/hw/mcu/dialog/da1469x/src/da1469x_clock.cyclo ./tinyusb-master/hw/mcu/dialog/da1469x/src/da1469x_clock.d ./tinyusb-master/hw/mcu/dialog/da1469x/src/da1469x_clock.o ./tinyusb-master/hw/mcu/dialog/da1469x/src/da1469x_clock.su ./tinyusb-master/hw/mcu/dialog/da1469x/src/hal_gpio.cyclo ./tinyusb-master/hw/mcu/dialog/da1469x/src/hal_gpio.d ./tinyusb-master/hw/mcu/dialog/da1469x/src/hal_gpio.o ./tinyusb-master/hw/mcu/dialog/da1469x/src/hal_gpio.su ./tinyusb-master/hw/mcu/dialog/da1469x/src/hal_system.cyclo ./tinyusb-master/hw/mcu/dialog/da1469x/src/hal_system.d ./tinyusb-master/hw/mcu/dialog/da1469x/src/hal_system.o ./tinyusb-master/hw/mcu/dialog/da1469x/src/hal_system.su ./tinyusb-master/hw/mcu/dialog/da1469x/src/hal_system_start.cyclo ./tinyusb-master/hw/mcu/dialog/da1469x/src/hal_system_start.d ./tinyusb-master/hw/mcu/dialog/da1469x/src/hal_system_start.o ./tinyusb-master/hw/mcu/dialog/da1469x/src/hal_system_start.su ./tinyusb-master/hw/mcu/dialog/da1469x/src/system_da1469x.cyclo ./tinyusb-master/hw/mcu/dialog/da1469x/src/system_da1469x.d ./tinyusb-master/hw/mcu/dialog/da1469x/src/system_da1469x.o ./tinyusb-master/hw/mcu/dialog/da1469x/src/system_da1469x.su

.PHONY: clean-tinyusb-2d-master-2f-hw-2f-mcu-2f-dialog-2f-da1469x-2f-src

