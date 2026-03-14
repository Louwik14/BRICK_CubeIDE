################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Drivers/Drv_app/Src/cs42448.c \
../Drivers/Drv_app/Src/drv_display.c \
../Drivers/Drv_app/Src/drv_encoders.c \
../Drivers/Drv_app/Src/led_rgb.c \
../Drivers/Drv_app/Src/sdram.c \
../Drivers/Drv_app/Src/sdram_alloc.c \
../Drivers/Drv_app/Src/w9825g6kh.c 

C_DEPS += \
./Drivers/Drv_app/Src/cs42448.d \
./Drivers/Drv_app/Src/drv_display.d \
./Drivers/Drv_app/Src/drv_encoders.d \
./Drivers/Drv_app/Src/led_rgb.d \
./Drivers/Drv_app/Src/sdram.d \
./Drivers/Drv_app/Src/sdram_alloc.d \
./Drivers/Drv_app/Src/w9825g6kh.d 

OBJS += \
./Drivers/Drv_app/Src/cs42448.o \
./Drivers/Drv_app/Src/drv_display.o \
./Drivers/Drv_app/Src/drv_encoders.o \
./Drivers/Drv_app/Src/led_rgb.o \
./Drivers/Drv_app/Src/sdram.o \
./Drivers/Drv_app/Src/sdram_alloc.o \
./Drivers/Drv_app/Src/w9825g6kh.o 


# Each subdirectory must supply rules for building sources it contributes
Drivers/Drv_app/Src/%.o Drivers/Drv_app/Src/%.su Drivers/Drv_app/Src/%.cyclo: ../Drivers/Drv_app/Src/%.c Drivers/Drv_app/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Drv_app/Inc" -Og -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Drivers-2f-Drv_app-2f-Src

clean-Drivers-2f-Drv_app-2f-Src:
	-$(RM) ./Drivers/Drv_app/Src/cs42448.cyclo ./Drivers/Drv_app/Src/cs42448.d ./Drivers/Drv_app/Src/cs42448.o ./Drivers/Drv_app/Src/cs42448.su ./Drivers/Drv_app/Src/drv_display.cyclo ./Drivers/Drv_app/Src/drv_display.d ./Drivers/Drv_app/Src/drv_display.o ./Drivers/Drv_app/Src/drv_display.su ./Drivers/Drv_app/Src/drv_encoders.cyclo ./Drivers/Drv_app/Src/drv_encoders.d ./Drivers/Drv_app/Src/drv_encoders.o ./Drivers/Drv_app/Src/drv_encoders.su ./Drivers/Drv_app/Src/led_rgb.cyclo ./Drivers/Drv_app/Src/led_rgb.d ./Drivers/Drv_app/Src/led_rgb.o ./Drivers/Drv_app/Src/led_rgb.su ./Drivers/Drv_app/Src/sdram.cyclo ./Drivers/Drv_app/Src/sdram.d ./Drivers/Drv_app/Src/sdram.o ./Drivers/Drv_app/Src/sdram.su ./Drivers/Drv_app/Src/sdram_alloc.cyclo ./Drivers/Drv_app/Src/sdram_alloc.d ./Drivers/Drv_app/Src/sdram_alloc.o ./Drivers/Drv_app/Src/sdram_alloc.su ./Drivers/Drv_app/Src/w9825g6kh.cyclo ./Drivers/Drv_app/Src/w9825g6kh.d ./Drivers/Drv_app/Src/w9825g6kh.o ./Drivers/Drv_app/Src/w9825g6kh.su

.PHONY: clean-Drivers-2f-Drv_app-2f-Src

