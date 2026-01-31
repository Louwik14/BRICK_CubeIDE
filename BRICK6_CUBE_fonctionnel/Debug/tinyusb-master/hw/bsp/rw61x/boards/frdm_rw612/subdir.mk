################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../tinyusb-master/hw/bsp/rw61x/boards/frdm_rw612/clock_config.c \
../tinyusb-master/hw/bsp/rw61x/boards/frdm_rw612/pin_mux.c 

OBJS += \
./tinyusb-master/hw/bsp/rw61x/boards/frdm_rw612/clock_config.o \
./tinyusb-master/hw/bsp/rw61x/boards/frdm_rw612/pin_mux.o 

C_DEPS += \
./tinyusb-master/hw/bsp/rw61x/boards/frdm_rw612/clock_config.d \
./tinyusb-master/hw/bsp/rw61x/boards/frdm_rw612/pin_mux.d 


# Each subdirectory must supply rules for building sources it contributes
tinyusb-master/hw/bsp/rw61x/boards/frdm_rw612/%.o tinyusb-master/hw/bsp/rw61x/boards/frdm_rw612/%.su tinyusb-master/hw/bsp/rw61x/boards/frdm_rw612/%.cyclo: ../tinyusb-master/hw/bsp/rw61x/boards/frdm_rw612/%.c tinyusb-master/hw/bsp/rw61x/boards/frdm_rw612/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"/BRICK6_CUBE/tinyusb-master" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-tinyusb-2d-master-2f-hw-2f-bsp-2f-rw61x-2f-boards-2f-frdm_rw612

clean-tinyusb-2d-master-2f-hw-2f-bsp-2f-rw61x-2f-boards-2f-frdm_rw612:
	-$(RM) ./tinyusb-master/hw/bsp/rw61x/boards/frdm_rw612/clock_config.cyclo ./tinyusb-master/hw/bsp/rw61x/boards/frdm_rw612/clock_config.d ./tinyusb-master/hw/bsp/rw61x/boards/frdm_rw612/clock_config.o ./tinyusb-master/hw/bsp/rw61x/boards/frdm_rw612/clock_config.su ./tinyusb-master/hw/bsp/rw61x/boards/frdm_rw612/pin_mux.cyclo ./tinyusb-master/hw/bsp/rw61x/boards/frdm_rw612/pin_mux.d ./tinyusb-master/hw/bsp/rw61x/boards/frdm_rw612/pin_mux.o ./tinyusb-master/hw/bsp/rw61x/boards/frdm_rw612/pin_mux.su

.PHONY: clean-tinyusb-2d-master-2f-hw-2f-bsp-2f-rw61x-2f-boards-2f-frdm_rw612

