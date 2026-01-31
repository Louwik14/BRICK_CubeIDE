################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_api.c \
../App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_rmt_dev.c \
../App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_rmt_dev_idf4.c \
../App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_rmt_encoder.c \
../App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_spi_dev.c 

OBJS += \
./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_api.o \
./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_rmt_dev.o \
./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_rmt_dev_idf4.o \
./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_rmt_encoder.o \
./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_spi_dev.o 

C_DEPS += \
./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_api.d \
./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_rmt_dev.d \
./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_rmt_dev_idf4.d \
./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_rmt_encoder.d \
./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_spi_dev.d 


# Each subdirectory must supply rules for building sources it contributes
App/TinyUSB/hw/bsp/espressif/components/led_strip/src/%.o App/TinyUSB/hw/bsp/espressif/components/led_strip/src/%.su App/TinyUSB/hw/bsp/espressif/components/led_strip/src/%.cyclo: ../App/TinyUSB/hw/bsp/espressif/components/led_strip/src/%.c App/TinyUSB/hw/bsp/espressif/components/led_strip/src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/src/common" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/src" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/src/class" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/src/portable/synopsys/dwc2" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/src/device" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/TinyUSB/hw" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-App-2f-TinyUSB-2f-hw-2f-bsp-2f-espressif-2f-components-2f-led_strip-2f-src

clean-App-2f-TinyUSB-2f-hw-2f-bsp-2f-espressif-2f-components-2f-led_strip-2f-src:
	-$(RM) ./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_api.cyclo ./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_api.d ./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_api.o ./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_api.su ./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_rmt_dev.cyclo ./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_rmt_dev.d ./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_rmt_dev.o ./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_rmt_dev.su ./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_rmt_dev_idf4.cyclo ./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_rmt_dev_idf4.d ./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_rmt_dev_idf4.o ./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_rmt_dev_idf4.su ./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_rmt_encoder.cyclo ./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_rmt_encoder.d ./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_rmt_encoder.o ./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_rmt_encoder.su ./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_spi_dev.cyclo ./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_spi_dev.d ./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_spi_dev.o ./App/TinyUSB/hw/bsp/espressif/components/led_strip/src/led_strip_spi_dev.su

.PHONY: clean-App-2f-TinyUSB-2f-hw-2f-bsp-2f-espressif-2f-components-2f-led_strip-2f-src

