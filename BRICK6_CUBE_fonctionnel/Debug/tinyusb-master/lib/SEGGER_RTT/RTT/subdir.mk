################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../tinyusb-master/lib/SEGGER_RTT/RTT/SEGGER_RTT.c \
../tinyusb-master/lib/SEGGER_RTT/RTT/SEGGER_RTT_printf.c 

S_UPPER_SRCS += \
../tinyusb-master/lib/SEGGER_RTT/RTT/SEGGER_RTT_ASM_ARMv7M.S 

OBJS += \
./tinyusb-master/lib/SEGGER_RTT/RTT/SEGGER_RTT.o \
./tinyusb-master/lib/SEGGER_RTT/RTT/SEGGER_RTT_ASM_ARMv7M.o \
./tinyusb-master/lib/SEGGER_RTT/RTT/SEGGER_RTT_printf.o 

S_UPPER_DEPS += \
./tinyusb-master/lib/SEGGER_RTT/RTT/SEGGER_RTT_ASM_ARMv7M.d 

C_DEPS += \
./tinyusb-master/lib/SEGGER_RTT/RTT/SEGGER_RTT.d \
./tinyusb-master/lib/SEGGER_RTT/RTT/SEGGER_RTT_printf.d 


# Each subdirectory must supply rules for building sources it contributes
tinyusb-master/lib/SEGGER_RTT/RTT/%.o tinyusb-master/lib/SEGGER_RTT/RTT/%.su tinyusb-master/lib/SEGGER_RTT/RTT/%.cyclo: ../tinyusb-master/lib/SEGGER_RTT/RTT/%.c tinyusb-master/lib/SEGGER_RTT/RTT/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"/BRICK6_CUBE/tinyusb-master" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
tinyusb-master/lib/SEGGER_RTT/RTT/%.o: ../tinyusb-master/lib/SEGGER_RTT/RTT/%.S tinyusb-master/lib/SEGGER_RTT/RTT/subdir.mk
	arm-none-eabi-gcc -mcpu=cortex-m7 -g3 -DDEBUG -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -x assembler-with-cpp -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@" "$<"

clean: clean-tinyusb-2d-master-2f-lib-2f-SEGGER_RTT-2f-RTT

clean-tinyusb-2d-master-2f-lib-2f-SEGGER_RTT-2f-RTT:
	-$(RM) ./tinyusb-master/lib/SEGGER_RTT/RTT/SEGGER_RTT.cyclo ./tinyusb-master/lib/SEGGER_RTT/RTT/SEGGER_RTT.d ./tinyusb-master/lib/SEGGER_RTT/RTT/SEGGER_RTT.o ./tinyusb-master/lib/SEGGER_RTT/RTT/SEGGER_RTT.su ./tinyusb-master/lib/SEGGER_RTT/RTT/SEGGER_RTT_ASM_ARMv7M.d ./tinyusb-master/lib/SEGGER_RTT/RTT/SEGGER_RTT_ASM_ARMv7M.o ./tinyusb-master/lib/SEGGER_RTT/RTT/SEGGER_RTT_printf.cyclo ./tinyusb-master/lib/SEGGER_RTT/RTT/SEGGER_RTT_printf.d ./tinyusb-master/lib/SEGGER_RTT/RTT/SEGGER_RTT_printf.o ./tinyusb-master/lib/SEGGER_RTT/RTT/SEGGER_RTT_printf.su

.PHONY: clean-tinyusb-2d-master-2f-lib-2f-SEGGER_RTT-2f-RTT

