################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Src/Storage/sd_callbacks.c \
../Src/Storage/sd_owner.c \
../Src/Storage/sd_stream.c \
../Src/Storage/wav_loader.c 

C_DEPS += \
./Src/Storage/sd_callbacks.d \
./Src/Storage/sd_owner.d \
./Src/Storage/sd_stream.d \
./Src/Storage/wav_loader.d 

OBJS += \
./Src/Storage/sd_callbacks.o \
./Src/Storage/sd_owner.o \
./Src/Storage/sd_stream.o \
./Src/Storage/wav_loader.o 


# Each subdirectory must supply rules for building sources it contributes
Src/Storage/%.o Src/Storage/%.su Src/Storage/%.cyclo: ../Src/Storage/%.c Src/Storage/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Class/AUDIO/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Class/CDC/Inc" -I../Middlewares/Third_Party/FatFs/src -Og -ffunction-sections -fdata-sections -Wall -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Src-2f-Storage

clean-Src-2f-Storage:
	-$(RM) ./Src/Storage/sd_callbacks.cyclo ./Src/Storage/sd_callbacks.d ./Src/Storage/sd_callbacks.o ./Src/Storage/sd_callbacks.su ./Src/Storage/sd_owner.cyclo ./Src/Storage/sd_owner.d ./Src/Storage/sd_owner.o ./Src/Storage/sd_owner.su ./Src/Storage/sd_stream.cyclo ./Src/Storage/sd_stream.d ./Src/Storage/sd_stream.o ./Src/Storage/sd_stream.su ./Src/Storage/wav_loader.cyclo ./Src/Storage/wav_loader.d ./Src/Storage/wav_loader.o ./Src/Storage/wav_loader.su

.PHONY: clean-Src-2f-Storage

