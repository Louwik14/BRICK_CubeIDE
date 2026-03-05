################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../tinyusb-master/examples/device/video_capture_2ch/src/main.c \
../tinyusb-master/examples/device/video_capture_2ch/src/usb_descriptors.c 

OBJS += \
./tinyusb-master/examples/device/video_capture_2ch/src/main.o \
./tinyusb-master/examples/device/video_capture_2ch/src/usb_descriptors.o 

C_DEPS += \
./tinyusb-master/examples/device/video_capture_2ch/src/main.d \
./tinyusb-master/examples/device/video_capture_2ch/src/usb_descriptors.d 


# Each subdirectory must supply rules for building sources it contributes
tinyusb-master/examples/device/video_capture_2ch/src/%.o tinyusb-master/examples/device/video_capture_2ch/src/%.su tinyusb-master/examples/device/video_capture_2ch/src/%.cyclo: ../tinyusb-master/examples/device/video_capture_2ch/src/%.c tinyusb-master/examples/device/video_capture_2ch/src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"/BRICK6_CUBE/tinyusb-master" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-tinyusb-2d-master-2f-examples-2f-device-2f-video_capture_2ch-2f-src

clean-tinyusb-2d-master-2f-examples-2f-device-2f-video_capture_2ch-2f-src:
	-$(RM) ./tinyusb-master/examples/device/video_capture_2ch/src/main.cyclo ./tinyusb-master/examples/device/video_capture_2ch/src/main.d ./tinyusb-master/examples/device/video_capture_2ch/src/main.o ./tinyusb-master/examples/device/video_capture_2ch/src/main.su ./tinyusb-master/examples/device/video_capture_2ch/src/usb_descriptors.cyclo ./tinyusb-master/examples/device/video_capture_2ch/src/usb_descriptors.d ./tinyusb-master/examples/device/video_capture_2ch/src/usb_descriptors.o ./tinyusb-master/examples/device/video_capture_2ch/src/usb_descriptors.su

.PHONY: clean-tinyusb-2d-master-2f-examples-2f-device-2f-video_capture_2ch-2f-src

