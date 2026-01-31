################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../tinyusb-master/lib/networking/dhserver.c \
../tinyusb-master/lib/networking/dnserver.c \
../tinyusb-master/lib/networking/rndis_reports.c 

OBJS += \
./tinyusb-master/lib/networking/dhserver.o \
./tinyusb-master/lib/networking/dnserver.o \
./tinyusb-master/lib/networking/rndis_reports.o 

C_DEPS += \
./tinyusb-master/lib/networking/dhserver.d \
./tinyusb-master/lib/networking/dnserver.d \
./tinyusb-master/lib/networking/rndis_reports.d 


# Each subdirectory must supply rules for building sources it contributes
tinyusb-master/lib/networking/%.o tinyusb-master/lib/networking/%.su tinyusb-master/lib/networking/%.cyclo: ../tinyusb-master/lib/networking/%.c tinyusb-master/lib/networking/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"/BRICK6_CUBE/tinyusb-master" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-tinyusb-2d-master-2f-lib-2f-networking

clean-tinyusb-2d-master-2f-lib-2f-networking:
	-$(RM) ./tinyusb-master/lib/networking/dhserver.cyclo ./tinyusb-master/lib/networking/dhserver.d ./tinyusb-master/lib/networking/dhserver.o ./tinyusb-master/lib/networking/dhserver.su ./tinyusb-master/lib/networking/dnserver.cyclo ./tinyusb-master/lib/networking/dnserver.d ./tinyusb-master/lib/networking/dnserver.o ./tinyusb-master/lib/networking/dnserver.su ./tinyusb-master/lib/networking/rndis_reports.cyclo ./tinyusb-master/lib/networking/rndis_reports.d ./tinyusb-master/lib/networking/rndis_reports.o ./tinyusb-master/lib/networking/rndis_reports.su

.PHONY: clean-tinyusb-2d-master-2f-lib-2f-networking

