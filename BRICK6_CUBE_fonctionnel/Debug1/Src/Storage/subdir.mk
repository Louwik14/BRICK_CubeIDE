################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Src/Storage/pattern_live_ram.c \
../Src/Storage/pattern_sd_bank.c \
../Src/Storage/sd_access_gate.c \
../Src/Storage/undo_v1.c \
../Src/Storage/wav_loader.c \
../Src/Storage/wav_parser.c 

C_DEPS += \
./Src/Storage/pattern_live_ram.d \
./Src/Storage/pattern_sd_bank.d \
./Src/Storage/sd_access_gate.d \
./Src/Storage/undo_v1.d \
./Src/Storage/wav_loader.d \
./Src/Storage/wav_parser.d 

OBJS += \
./Src/Storage/pattern_live_ram.o \
./Src/Storage/pattern_sd_bank.o \
./Src/Storage/sd_access_gate.o \
./Src/Storage/undo_v1.o \
./Src/Storage/wav_loader.o \
./Src/Storage/wav_parser.o 


# Each subdirectory must supply rules for building sources it contributes
Src/Storage/%.o Src/Storage/%.su Src/Storage/%.cyclo: ../Src/Storage/%.c Src/Storage/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Drv_app/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Core" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/MIDI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Param" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Storage" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/UI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/SD" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/U8g2" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/Third_Party/FatFs/src" -Og -ffunction-sections -fdata-sections -Wall -fno-math-errno -fsingle-precision-constant -ffast-math -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Src-2f-Storage

clean-Src-2f-Storage:
	-$(RM) ./Src/Storage/pattern_live_ram.cyclo ./Src/Storage/pattern_live_ram.d ./Src/Storage/pattern_live_ram.o ./Src/Storage/pattern_live_ram.su ./Src/Storage/pattern_sd_bank.cyclo ./Src/Storage/pattern_sd_bank.d ./Src/Storage/pattern_sd_bank.o ./Src/Storage/pattern_sd_bank.su ./Src/Storage/sd_access_gate.cyclo ./Src/Storage/sd_access_gate.d ./Src/Storage/sd_access_gate.o ./Src/Storage/sd_access_gate.su ./Src/Storage/undo_v1.cyclo ./Src/Storage/undo_v1.d ./Src/Storage/undo_v1.o ./Src/Storage/undo_v1.su ./Src/Storage/wav_loader.cyclo ./Src/Storage/wav_loader.d ./Src/Storage/wav_loader.o ./Src/Storage/wav_loader.su ./Src/Storage/wav_parser.cyclo ./Src/Storage/wav_parser.d ./Src/Storage/wav_parser.o ./Src/Storage/wav_parser.su

.PHONY: clean-Src-2f-Storage

