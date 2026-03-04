################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Src/UI/font.c \
../Src/UI/font4x6.c \
../Src/UI/font5x7.c \
../Src/UI/font5x8_elektron.c \
../Src/UI/ui_tasklet.c 

C_DEPS += \
./Src/UI/font.d \
./Src/UI/font4x6.d \
./Src/UI/font5x7.d \
./Src/UI/font5x8_elektron.d \
./Src/UI/ui_tasklet.d 

OBJS += \
./Src/UI/font.o \
./Src/UI/font4x6.o \
./Src/UI/font5x7.o \
./Src/UI/font5x8_elektron.o \
./Src/UI/ui_tasklet.o 


# Each subdirectory must supply rules for building sources it contributes
Src/UI/%.o Src/UI/%.su Src/UI/%.cyclo: ../Src/UI/%.c Src/UI/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Class/AUDIO/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Class/CDC/Inc" -I../Middlewares/Third_Party/FatFs/src -Og -ffunction-sections -fdata-sections -Wall -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Src-2f-UI

clean-Src-2f-UI:
	-$(RM) ./Src/UI/font.cyclo ./Src/UI/font.d ./Src/UI/font.o ./Src/UI/font.su ./Src/UI/font4x6.cyclo ./Src/UI/font4x6.d ./Src/UI/font4x6.o ./Src/UI/font4x6.su ./Src/UI/font5x7.cyclo ./Src/UI/font5x7.d ./Src/UI/font5x7.o ./Src/UI/font5x7.su ./Src/UI/font5x8_elektron.cyclo ./Src/UI/font5x8_elektron.d ./Src/UI/font5x8_elektron.o ./Src/UI/font5x8_elektron.su ./Src/UI/ui_tasklet.cyclo ./Src/UI/ui_tasklet.d ./Src/UI/ui_tasklet.o ./Src/UI/ui_tasklet.su

.PHONY: clean-Src-2f-UI

