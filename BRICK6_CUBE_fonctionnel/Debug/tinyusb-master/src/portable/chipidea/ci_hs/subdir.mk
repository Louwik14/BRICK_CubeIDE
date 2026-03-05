################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../tinyusb-master/src/portable/chipidea/ci_hs/dcd_ci_hs.c \
../tinyusb-master/src/portable/chipidea/ci_hs/hcd_ci_hs.c 

OBJS += \
./tinyusb-master/src/portable/chipidea/ci_hs/dcd_ci_hs.o \
./tinyusb-master/src/portable/chipidea/ci_hs/hcd_ci_hs.o 

C_DEPS += \
./tinyusb-master/src/portable/chipidea/ci_hs/dcd_ci_hs.d \
./tinyusb-master/src/portable/chipidea/ci_hs/hcd_ci_hs.d 


# Each subdirectory must supply rules for building sources it contributes
tinyusb-master/src/portable/chipidea/ci_hs/%.o tinyusb-master/src/portable/chipidea/ci_hs/%.su tinyusb-master/src/portable/chipidea/ci_hs/%.cyclo: ../tinyusb-master/src/portable/chipidea/ci_hs/%.c tinyusb-master/src/portable/chipidea/ci_hs/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"/BRICK6_CUBE/tinyusb-master" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-tinyusb-2d-master-2f-src-2f-portable-2f-chipidea-2f-ci_hs

clean-tinyusb-2d-master-2f-src-2f-portable-2f-chipidea-2f-ci_hs:
	-$(RM) ./tinyusb-master/src/portable/chipidea/ci_hs/dcd_ci_hs.cyclo ./tinyusb-master/src/portable/chipidea/ci_hs/dcd_ci_hs.d ./tinyusb-master/src/portable/chipidea/ci_hs/dcd_ci_hs.o ./tinyusb-master/src/portable/chipidea/ci_hs/dcd_ci_hs.su ./tinyusb-master/src/portable/chipidea/ci_hs/hcd_ci_hs.cyclo ./tinyusb-master/src/portable/chipidea/ci_hs/hcd_ci_hs.d ./tinyusb-master/src/portable/chipidea/ci_hs/hcd_ci_hs.o ./tinyusb-master/src/portable/chipidea/ci_hs/hcd_ci_hs.su

.PHONY: clean-tinyusb-2d-master-2f-src-2f-portable-2f-chipidea-2f-ci_hs

