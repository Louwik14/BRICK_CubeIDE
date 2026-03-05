################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../App/TinyUSB/hw/bsp/kinetis_k/family.c 

OBJS += \
./App/TinyUSB/hw/bsp/kinetis_k/family.o 

C_DEPS += \
./App/TinyUSB/hw/bsp/kinetis_k/family.d 


# Each subdirectory must supply rules for building sources it contributes
App/TinyUSB/hw/bsp/kinetis_k/%.o App/TinyUSB/hw/bsp/kinetis_k/%.su App/TinyUSB/hw/bsp/kinetis_k/%.cyclo: ../App/TinyUSB/hw/bsp/kinetis_k/%.c App/TinyUSB/hw/bsp/kinetis_k/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/src/common" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/src" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/src/class" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/src/portable/synopsys/dwc2" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/src/device" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/hw" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-App-2f-TinyUSB-2f-hw-2f-bsp-2f-kinetis_k

clean-App-2f-TinyUSB-2f-hw-2f-bsp-2f-kinetis_k:
	-$(RM) ./App/TinyUSB/hw/bsp/kinetis_k/family.cyclo ./App/TinyUSB/hw/bsp/kinetis_k/family.d ./App/TinyUSB/hw/bsp/kinetis_k/family.o ./App/TinyUSB/hw/bsp/kinetis_k/family.su

.PHONY: clean-App-2f-TinyUSB-2f-hw-2f-bsp-2f-kinetis_k

