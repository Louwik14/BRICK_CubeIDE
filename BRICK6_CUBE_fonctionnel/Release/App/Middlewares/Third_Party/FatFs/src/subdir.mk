################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../App/Middlewares/Third_Party/FatFs/src/diskio.c \
../App/Middlewares/Third_Party/FatFs/src/ff.c \
../App/Middlewares/Third_Party/FatFs/src/ff_gen_drv.c 

C_DEPS += \
./App/Middlewares/Third_Party/FatFs/src/diskio.d \
./App/Middlewares/Third_Party/FatFs/src/ff.d \
./App/Middlewares/Third_Party/FatFs/src/ff_gen_drv.d 

OBJS += \
./App/Middlewares/Third_Party/FatFs/src/diskio.o \
./App/Middlewares/Third_Party/FatFs/src/ff.o \
./App/Middlewares/Third_Party/FatFs/src/ff_gen_drv.o 


# Each subdirectory must supply rules for building sources it contributes
App/Middlewares/Third_Party/FatFs/src/%.o App/Middlewares/Third_Party/FatFs/src/%.su App/Middlewares/Third_Party/FatFs/src/%.cyclo: ../App/Middlewares/Third_Party/FatFs/src/%.c App/Middlewares/Third_Party/FatFs/src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Drv_app/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Core" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/MIDI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Param" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Storage" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/SD" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/UI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/Third_Party/FatFs/src" -O3 -ffunction-sections -fdata-sections -Wall -fno-math-errno -ffast-math -fsingle-precision-constant -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-App-2f-Middlewares-2f-Third_Party-2f-FatFs-2f-src

clean-App-2f-Middlewares-2f-Third_Party-2f-FatFs-2f-src:
	-$(RM) ./App/Middlewares/Third_Party/FatFs/src/diskio.cyclo ./App/Middlewares/Third_Party/FatFs/src/diskio.d ./App/Middlewares/Third_Party/FatFs/src/diskio.o ./App/Middlewares/Third_Party/FatFs/src/diskio.su ./App/Middlewares/Third_Party/FatFs/src/ff.cyclo ./App/Middlewares/Third_Party/FatFs/src/ff.d ./App/Middlewares/Third_Party/FatFs/src/ff.o ./App/Middlewares/Third_Party/FatFs/src/ff.su ./App/Middlewares/Third_Party/FatFs/src/ff_gen_drv.cyclo ./App/Middlewares/Third_Party/FatFs/src/ff_gen_drv.d ./App/Middlewares/Third_Party/FatFs/src/ff_gen_drv.o ./App/Middlewares/Third_Party/FatFs/src/ff_gen_drv.su

.PHONY: clean-App-2f-Middlewares-2f-Third_Party-2f-FatFs-2f-src

