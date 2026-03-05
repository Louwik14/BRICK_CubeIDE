################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
S_SRCS += \
../tinyusb-master/hw/bsp/ch32f20x/startup_gcc_ch32f20x_d8c.s 

C_SRCS += \
../tinyusb-master/hw/bsp/ch32f20x/ch32f20x_it.c \
../tinyusb-master/hw/bsp/ch32f20x/debug_uart.c \
../tinyusb-master/hw/bsp/ch32f20x/family.c \
../tinyusb-master/hw/bsp/ch32f20x/system_ch32f20x.c 

OBJS += \
./tinyusb-master/hw/bsp/ch32f20x/ch32f20x_it.o \
./tinyusb-master/hw/bsp/ch32f20x/debug_uart.o \
./tinyusb-master/hw/bsp/ch32f20x/family.o \
./tinyusb-master/hw/bsp/ch32f20x/startup_gcc_ch32f20x_d8c.o \
./tinyusb-master/hw/bsp/ch32f20x/system_ch32f20x.o 

S_DEPS += \
./tinyusb-master/hw/bsp/ch32f20x/startup_gcc_ch32f20x_d8c.d 

C_DEPS += \
./tinyusb-master/hw/bsp/ch32f20x/ch32f20x_it.d \
./tinyusb-master/hw/bsp/ch32f20x/debug_uart.d \
./tinyusb-master/hw/bsp/ch32f20x/family.d \
./tinyusb-master/hw/bsp/ch32f20x/system_ch32f20x.d 


# Each subdirectory must supply rules for building sources it contributes
tinyusb-master/hw/bsp/ch32f20x/%.o tinyusb-master/hw/bsp/ch32f20x/%.su tinyusb-master/hw/bsp/ch32f20x/%.cyclo: ../tinyusb-master/hw/bsp/ch32f20x/%.c tinyusb-master/hw/bsp/ch32f20x/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"/BRICK6_CUBE/tinyusb-master" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
tinyusb-master/hw/bsp/ch32f20x/%.o: ../tinyusb-master/hw/bsp/ch32f20x/%.s tinyusb-master/hw/bsp/ch32f20x/subdir.mk
	arm-none-eabi-gcc -mcpu=cortex-m7 -g3 -DDEBUG -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -x assembler-with-cpp -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@" "$<"

clean: clean-tinyusb-2d-master-2f-hw-2f-bsp-2f-ch32f20x

clean-tinyusb-2d-master-2f-hw-2f-bsp-2f-ch32f20x:
	-$(RM) ./tinyusb-master/hw/bsp/ch32f20x/ch32f20x_it.cyclo ./tinyusb-master/hw/bsp/ch32f20x/ch32f20x_it.d ./tinyusb-master/hw/bsp/ch32f20x/ch32f20x_it.o ./tinyusb-master/hw/bsp/ch32f20x/ch32f20x_it.su ./tinyusb-master/hw/bsp/ch32f20x/debug_uart.cyclo ./tinyusb-master/hw/bsp/ch32f20x/debug_uart.d ./tinyusb-master/hw/bsp/ch32f20x/debug_uart.o ./tinyusb-master/hw/bsp/ch32f20x/debug_uart.su ./tinyusb-master/hw/bsp/ch32f20x/family.cyclo ./tinyusb-master/hw/bsp/ch32f20x/family.d ./tinyusb-master/hw/bsp/ch32f20x/family.o ./tinyusb-master/hw/bsp/ch32f20x/family.su ./tinyusb-master/hw/bsp/ch32f20x/startup_gcc_ch32f20x_d8c.d ./tinyusb-master/hw/bsp/ch32f20x/startup_gcc_ch32f20x_d8c.o ./tinyusb-master/hw/bsp/ch32f20x/system_ch32f20x.cyclo ./tinyusb-master/hw/bsp/ch32f20x/system_ch32f20x.d ./tinyusb-master/hw/bsp/ch32f20x/system_ch32f20x.o ./tinyusb-master/hw/bsp/ch32f20x/system_ch32f20x.su

.PHONY: clean-tinyusb-2d-master-2f-hw-2f-bsp-2f-ch32f20x

