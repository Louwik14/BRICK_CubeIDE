################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Src/App/Hall/hall_adc.c \
../Src/App/Hall/hall_calibration.c \
../Src/App/Hall/hall_engine.c \
../Src/App/Hall/hall_filter_asc.c \
../Src/App/Hall/hall_juno_midi.c \
../Src/App/Hall/hall_loop.c \
../Src/App/Hall/hall_note_midi.c 

C_DEPS += \
./Src/App/Hall/hall_adc.d \
./Src/App/Hall/hall_calibration.d \
./Src/App/Hall/hall_engine.d \
./Src/App/Hall/hall_filter_asc.d \
./Src/App/Hall/hall_juno_midi.d \
./Src/App/Hall/hall_loop.d \
./Src/App/Hall/hall_note_midi.d 

OBJS += \
./Src/App/Hall/hall_adc.o \
./Src/App/Hall/hall_calibration.o \
./Src/App/Hall/hall_engine.o \
./Src/App/Hall/hall_filter_asc.o \
./Src/App/Hall/hall_juno_midi.o \
./Src/App/Hall/hall_loop.o \
./Src/App/Hall/hall_note_midi.o 


# Each subdirectory must supply rules for building sources it contributes
Src/App/Hall/%.o Src/App/Hall/%.su Src/App/Hall/%.cyclo: ../Src/App/Hall/%.c Src/App/Hall/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Drv_app/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Core" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/MIDI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Param" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Storage" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/SD" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/UI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/U8g2" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/Third_Party/FatFs/src" -O3 -ffunction-sections -fdata-sections -Wall -fno-math-errno -ffast-math -fsingle-precision-constant -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Src-2f-App-2f-Hall

clean-Src-2f-App-2f-Hall:
	-$(RM) ./Src/App/Hall/hall_adc.cyclo ./Src/App/Hall/hall_adc.d ./Src/App/Hall/hall_adc.o ./Src/App/Hall/hall_adc.su ./Src/App/Hall/hall_calibration.cyclo ./Src/App/Hall/hall_calibration.d ./Src/App/Hall/hall_calibration.o ./Src/App/Hall/hall_calibration.su ./Src/App/Hall/hall_engine.cyclo ./Src/App/Hall/hall_engine.d ./Src/App/Hall/hall_engine.o ./Src/App/Hall/hall_engine.su ./Src/App/Hall/hall_filter_asc.cyclo ./Src/App/Hall/hall_filter_asc.d ./Src/App/Hall/hall_filter_asc.o ./Src/App/Hall/hall_filter_asc.su ./Src/App/Hall/hall_juno_midi.cyclo ./Src/App/Hall/hall_juno_midi.d ./Src/App/Hall/hall_juno_midi.o ./Src/App/Hall/hall_juno_midi.su ./Src/App/Hall/hall_loop.cyclo ./Src/App/Hall/hall_loop.d ./Src/App/Hall/hall_loop.o ./Src/App/Hall/hall_loop.su ./Src/App/Hall/hall_note_midi.cyclo ./Src/App/Hall/hall_note_midi.d ./Src/App/Hall/hall_note_midi.o ./Src/App/Hall/hall_note_midi.su

.PHONY: clean-Src-2f-App-2f-Hall

