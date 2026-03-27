################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Src/Seq/seq_clipboard.c \
../Src/Seq/seq_edit.c \
../Src/Seq/seq_led.c \
../Src/Seq/seq_model.c \
../Src/Seq/seq_param_iface.c \
../Src/Seq/seq_persistence.c \
../Src/Seq/seq_runtime.c 

C_DEPS += \
./Src/Seq/seq_clipboard.d \
./Src/Seq/seq_edit.d \
./Src/Seq/seq_led.d \
./Src/Seq/seq_model.d \
./Src/Seq/seq_param_iface.d \
./Src/Seq/seq_persistence.d \
./Src/Seq/seq_runtime.d 

OBJS += \
./Src/Seq/seq_clipboard.o \
./Src/Seq/seq_edit.o \
./Src/Seq/seq_led.o \
./Src/Seq/seq_model.o \
./Src/Seq/seq_param_iface.o \
./Src/Seq/seq_persistence.o \
./Src/Seq/seq_runtime.o 


# Each subdirectory must supply rules for building sources it contributes
Src/Seq/%.o Src/Seq/%.su Src/Seq/%.cyclo: ../Src/Seq/%.c Src/Seq/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Drv_app/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Core" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/MIDI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Param" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Storage" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/SD" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/UI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/U8g2" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/Third_Party/FatFs/src" -O3 -ffunction-sections -fdata-sections -Wall -fno-math-errno -ffast-math -fsingle-precision-constant -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Src-2f-Seq

clean-Src-2f-Seq:
	-$(RM) ./Src/Seq/seq_clipboard.cyclo ./Src/Seq/seq_clipboard.d ./Src/Seq/seq_clipboard.o ./Src/Seq/seq_clipboard.su ./Src/Seq/seq_edit.cyclo ./Src/Seq/seq_edit.d ./Src/Seq/seq_edit.o ./Src/Seq/seq_edit.su ./Src/Seq/seq_led.cyclo ./Src/Seq/seq_led.d ./Src/Seq/seq_led.o ./Src/Seq/seq_led.su ./Src/Seq/seq_model.cyclo ./Src/Seq/seq_model.d ./Src/Seq/seq_model.o ./Src/Seq/seq_model.su ./Src/Seq/seq_param_iface.cyclo ./Src/Seq/seq_param_iface.d ./Src/Seq/seq_param_iface.o ./Src/Seq/seq_param_iface.su ./Src/Seq/seq_persistence.cyclo ./Src/Seq/seq_persistence.d ./Src/Seq/seq_persistence.o ./Src/Seq/seq_persistence.su ./Src/Seq/seq_runtime.cyclo ./Src/Seq/seq_runtime.d ./Src/Seq/seq_runtime.o ./Src/Seq/seq_runtime.su

.PHONY: clean-Src-2f-Seq

