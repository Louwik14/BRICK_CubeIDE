################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../App/TinyUSB/hw/bsp/imxrt/boards/metro_m7_1011_sd/board/clock_config.c \
../App/TinyUSB/hw/bsp/imxrt/boards/metro_m7_1011_sd/board/pin_mux.c 

OBJS += \
./App/TinyUSB/hw/bsp/imxrt/boards/metro_m7_1011_sd/board/clock_config.o \
./App/TinyUSB/hw/bsp/imxrt/boards/metro_m7_1011_sd/board/pin_mux.o 

C_DEPS += \
./App/TinyUSB/hw/bsp/imxrt/boards/metro_m7_1011_sd/board/clock_config.d \
./App/TinyUSB/hw/bsp/imxrt/boards/metro_m7_1011_sd/board/pin_mux.d 


# Each subdirectory must supply rules for building sources it contributes
App/TinyUSB/hw/bsp/imxrt/boards/metro_m7_1011_sd/board/%.o App/TinyUSB/hw/bsp/imxrt/boards/metro_m7_1011_sd/board/%.su App/TinyUSB/hw/bsp/imxrt/boards/metro_m7_1011_sd/board/%.cyclo: ../App/TinyUSB/hw/bsp/imxrt/boards/metro_m7_1011_sd/board/%.c App/TinyUSB/hw/bsp/imxrt/boards/metro_m7_1011_sd/board/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/src/common" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/src" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/src/class" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/src/portable/synopsys/dwc2" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/src/device" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/hw" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-App-2f-TinyUSB-2f-hw-2f-bsp-2f-imxrt-2f-boards-2f-metro_m7_1011_sd-2f-board

clean-App-2f-TinyUSB-2f-hw-2f-bsp-2f-imxrt-2f-boards-2f-metro_m7_1011_sd-2f-board:
	-$(RM) ./App/TinyUSB/hw/bsp/imxrt/boards/metro_m7_1011_sd/board/clock_config.cyclo ./App/TinyUSB/hw/bsp/imxrt/boards/metro_m7_1011_sd/board/clock_config.d ./App/TinyUSB/hw/bsp/imxrt/boards/metro_m7_1011_sd/board/clock_config.o ./App/TinyUSB/hw/bsp/imxrt/boards/metro_m7_1011_sd/board/clock_config.su ./App/TinyUSB/hw/bsp/imxrt/boards/metro_m7_1011_sd/board/pin_mux.cyclo ./App/TinyUSB/hw/bsp/imxrt/boards/metro_m7_1011_sd/board/pin_mux.d ./App/TinyUSB/hw/bsp/imxrt/boards/metro_m7_1011_sd/board/pin_mux.o ./App/TinyUSB/hw/bsp/imxrt/boards/metro_m7_1011_sd/board/pin_mux.su

.PHONY: clean-App-2f-TinyUSB-2f-hw-2f-bsp-2f-imxrt-2f-boards-2f-metro_m7_1011_sd-2f-board

