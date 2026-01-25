################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
C:/Users/developpeur/Documents/Middlewares/ST/STM32_USB_Device_Library/Class/AUDIO/Src/usbd_audio.c 

OBJS += \
./Middlewares/STM32_USBD_Library/Class/usbd_audio.o 

C_DEPS += \
./Middlewares/STM32_USBD_Library/Class/usbd_audio.d 


# Each subdirectory must supply rules for building sources it contributes
Middlewares/STM32_USBD_Library/Class/usbd_audio.o: C:/Users/developpeur/Documents/Middlewares/ST/STM32_USB_Device_Library/Class/AUDIO/Src/usbd_audio.c Middlewares/STM32_USBD_Library/Class/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_USB_FS -DUSE_HAL_DRIVER -DUSE_PWR_LDO_SUPPLY -DUSE_IOEXPANDER -DSTM32H743xx -c -I../../Inc -I../../../../../../../Drivers/CMSIS/Include -I../../../../../../../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../../../../../../../Drivers/STM32H7xx_HAL_Driver/Inc -I../../../../../../../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../../../../../../../Middlewares/ST/STM32_USB_Device_Library/Class/AUDIO/Inc -I../../../../../../../Drivers/BSP/STM32H743I-EVAL -I../../../../../../../Drivers/BSP/Components/Common -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Middlewares-2f-STM32_USBD_Library-2f-Class

clean-Middlewares-2f-STM32_USBD_Library-2f-Class:
	-$(RM) ./Middlewares/STM32_USBD_Library/Class/usbd_audio.cyclo ./Middlewares/STM32_USBD_Library/Class/usbd_audio.d ./Middlewares/STM32_USBD_Library/Class/usbd_audio.o ./Middlewares/STM32_USBD_Library/Class/usbd_audio.su

.PHONY: clean-Middlewares-2f-STM32_USBD_Library-2f-Class

