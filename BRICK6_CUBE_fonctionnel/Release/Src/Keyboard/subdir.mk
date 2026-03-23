################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Src/Keyboard/kbd_chords_dict.c \
../Src/Keyboard/kbd_input_mapper.c \
../Src/Keyboard/keyboard_runtime.c \
../Src/Keyboard/ui_keyboard_app.c 

C_DEPS += \
./Src/Keyboard/kbd_chords_dict.d \
./Src/Keyboard/kbd_input_mapper.d \
./Src/Keyboard/keyboard_runtime.d \
./Src/Keyboard/ui_keyboard_app.d 

OBJS += \
./Src/Keyboard/kbd_chords_dict.o \
./Src/Keyboard/kbd_input_mapper.o \
./Src/Keyboard/keyboard_runtime.o \
./Src/Keyboard/ui_keyboard_app.o 


# Each subdirectory must supply rules for building sources it contributes
Src/Keyboard/%.o Src/Keyboard/%.su Src/Keyboard/%.cyclo: ../Src/Keyboard/%.c Src/Keyboard/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Drv_app/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Core" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/MIDI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Param" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Storage" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/SD" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/UI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/U8g2" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/Third_Party/FatFs/src" -O3 -ffunction-sections -fdata-sections -Wall -fno-math-errno -ffast-math -fsingle-precision-constant -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Src-2f-Keyboard

clean-Src-2f-Keyboard:
	-$(RM) ./Src/Keyboard/kbd_chords_dict.cyclo ./Src/Keyboard/kbd_chords_dict.d ./Src/Keyboard/kbd_chords_dict.o ./Src/Keyboard/kbd_chords_dict.su ./Src/Keyboard/kbd_input_mapper.cyclo ./Src/Keyboard/kbd_input_mapper.d ./Src/Keyboard/kbd_input_mapper.o ./Src/Keyboard/kbd_input_mapper.su ./Src/Keyboard/keyboard_runtime.cyclo ./Src/Keyboard/keyboard_runtime.d ./Src/Keyboard/keyboard_runtime.o ./Src/Keyboard/keyboard_runtime.su ./Src/Keyboard/ui_keyboard_app.cyclo ./Src/Keyboard/ui_keyboard_app.d ./Src/Keyboard/ui_keyboard_app.o ./Src/Keyboard/ui_keyboard_app.su

.PHONY: clean-Src-2f-Keyboard

