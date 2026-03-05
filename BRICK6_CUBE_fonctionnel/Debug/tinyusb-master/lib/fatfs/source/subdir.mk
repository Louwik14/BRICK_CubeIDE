################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../tinyusb-master/lib/fatfs/source/diskio.c \
../tinyusb-master/lib/fatfs/source/ff.c \
../tinyusb-master/lib/fatfs/source/ffsystem.c \
../tinyusb-master/lib/fatfs/source/ffunicode.c 

OBJS += \
./tinyusb-master/lib/fatfs/source/diskio.o \
./tinyusb-master/lib/fatfs/source/ff.o \
./tinyusb-master/lib/fatfs/source/ffsystem.o \
./tinyusb-master/lib/fatfs/source/ffunicode.o 

C_DEPS += \
./tinyusb-master/lib/fatfs/source/diskio.d \
./tinyusb-master/lib/fatfs/source/ff.d \
./tinyusb-master/lib/fatfs/source/ffsystem.d \
./tinyusb-master/lib/fatfs/source/ffunicode.d 


# Each subdirectory must supply rules for building sources it contributes
tinyusb-master/lib/fatfs/source/%.o tinyusb-master/lib/fatfs/source/%.su tinyusb-master/lib/fatfs/source/%.cyclo: ../tinyusb-master/lib/fatfs/source/%.c tinyusb-master/lib/fatfs/source/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"/BRICK6_CUBE/tinyusb-master" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-tinyusb-2d-master-2f-lib-2f-fatfs-2f-source

clean-tinyusb-2d-master-2f-lib-2f-fatfs-2f-source:
	-$(RM) ./tinyusb-master/lib/fatfs/source/diskio.cyclo ./tinyusb-master/lib/fatfs/source/diskio.d ./tinyusb-master/lib/fatfs/source/diskio.o ./tinyusb-master/lib/fatfs/source/diskio.su ./tinyusb-master/lib/fatfs/source/ff.cyclo ./tinyusb-master/lib/fatfs/source/ff.d ./tinyusb-master/lib/fatfs/source/ff.o ./tinyusb-master/lib/fatfs/source/ff.su ./tinyusb-master/lib/fatfs/source/ffsystem.cyclo ./tinyusb-master/lib/fatfs/source/ffsystem.d ./tinyusb-master/lib/fatfs/source/ffsystem.o ./tinyusb-master/lib/fatfs/source/ffsystem.su ./tinyusb-master/lib/fatfs/source/ffunicode.cyclo ./tinyusb-master/lib/fatfs/source/ffunicode.d ./tinyusb-master/lib/fatfs/source/ffunicode.o ./tinyusb-master/lib/fatfs/source/ffunicode.su

.PHONY: clean-tinyusb-2d-master-2f-lib-2f-fatfs-2f-source

