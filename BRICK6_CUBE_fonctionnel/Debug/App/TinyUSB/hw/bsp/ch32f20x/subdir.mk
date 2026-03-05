################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
S_SRCS += \
../App/TinyUSB/hw/bsp/ch32f20x/startup_gcc_ch32f20x_d8c.s 

C_SRCS += \
../App/TinyUSB/hw/bsp/ch32f20x/ch32f20x_it.c \
../App/TinyUSB/hw/bsp/ch32f20x/debug_uart.c \
../App/TinyUSB/hw/bsp/ch32f20x/family.c \
../App/TinyUSB/hw/bsp/ch32f20x/system_ch32f20x.c 

OBJS += \
./App/TinyUSB/hw/bsp/ch32f20x/ch32f20x_it.o \
./App/TinyUSB/hw/bsp/ch32f20x/debug_uart.o \
./App/TinyUSB/hw/bsp/ch32f20x/family.o \
./App/TinyUSB/hw/bsp/ch32f20x/startup_gcc_ch32f20x_d8c.o \
./App/TinyUSB/hw/bsp/ch32f20x/system_ch32f20x.o 

S_DEPS += \
./App/TinyUSB/hw/bsp/ch32f20x/startup_gcc_ch32f20x_d8c.d 

C_DEPS += \
./App/TinyUSB/hw/bsp/ch32f20x/ch32f20x_it.d \
./App/TinyUSB/hw/bsp/ch32f20x/debug_uart.d \
./App/TinyUSB/hw/bsp/ch32f20x/family.d \
./App/TinyUSB/hw/bsp/ch32f20x/system_ch32f20x.d 


# Each subdirectory must supply rules for building sources it contributes
App/TinyUSB/hw/bsp/ch32f20x/%.o App/TinyUSB/hw/bsp/ch32f20x/%.su App/TinyUSB/hw/bsp/ch32f20x/%.cyclo: ../App/TinyUSB/hw/bsp/ch32f20x/%.c App/TinyUSB/hw/bsp/ch32f20x/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/src/common" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/src" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/src/class" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/src/portable/synopsys/dwc2" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/src/device" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/hw" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
App/TinyUSB/hw/bsp/ch32f20x/%.o: ../App/TinyUSB/hw/bsp/ch32f20x/%.s App/TinyUSB/hw/bsp/ch32f20x/subdir.mk
	arm-none-eabi-gcc -mcpu=cortex-m7 -g3 -DDEBUG -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -x assembler-with-cpp -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@" "$<"

clean: clean-App-2f-TinyUSB-2f-hw-2f-bsp-2f-ch32f20x

clean-App-2f-TinyUSB-2f-hw-2f-bsp-2f-ch32f20x:
	-$(RM) ./App/TinyUSB/hw/bsp/ch32f20x/ch32f20x_it.cyclo ./App/TinyUSB/hw/bsp/ch32f20x/ch32f20x_it.d ./App/TinyUSB/hw/bsp/ch32f20x/ch32f20x_it.o ./App/TinyUSB/hw/bsp/ch32f20x/ch32f20x_it.su ./App/TinyUSB/hw/bsp/ch32f20x/debug_uart.cyclo ./App/TinyUSB/hw/bsp/ch32f20x/debug_uart.d ./App/TinyUSB/hw/bsp/ch32f20x/debug_uart.o ./App/TinyUSB/hw/bsp/ch32f20x/debug_uart.su ./App/TinyUSB/hw/bsp/ch32f20x/family.cyclo ./App/TinyUSB/hw/bsp/ch32f20x/family.d ./App/TinyUSB/hw/bsp/ch32f20x/family.o ./App/TinyUSB/hw/bsp/ch32f20x/family.su ./App/TinyUSB/hw/bsp/ch32f20x/startup_gcc_ch32f20x_d8c.d ./App/TinyUSB/hw/bsp/ch32f20x/startup_gcc_ch32f20x_d8c.o ./App/TinyUSB/hw/bsp/ch32f20x/system_ch32f20x.cyclo ./App/TinyUSB/hw/bsp/ch32f20x/system_ch32f20x.d ./App/TinyUSB/hw/bsp/ch32f20x/system_ch32f20x.o ./App/TinyUSB/hw/bsp/ch32f20x/system_ch32f20x.su

.PHONY: clean-App-2f-TinyUSB-2f-hw-2f-bsp-2f-ch32f20x

