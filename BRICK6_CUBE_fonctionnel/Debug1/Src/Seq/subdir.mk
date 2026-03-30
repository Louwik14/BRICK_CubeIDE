################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Src/Seq/seq_boundary_engine.c \
../Src/Seq/seq_clipboard.c \
../Src/Seq/seq_clock_bridge.c \
../Src/Seq/seq_edit.c \
../Src/Seq/seq_led.c \
../Src/Seq/seq_live_rec_capture.c \
../Src/Seq/seq_model.c \
../Src/Seq/seq_output_guard.c \
../Src/Seq/seq_param_iface.c \
../Src/Seq/seq_persistence.c \
../Src/Seq/seq_play_scheduler.c \
../Src/Seq/seq_runtime.c \
../Src/Seq/seq_transport_fsm.c 

C_DEPS += \
./Src/Seq/seq_boundary_engine.d \
./Src/Seq/seq_clipboard.d \
./Src/Seq/seq_clock_bridge.d \
./Src/Seq/seq_edit.d \
./Src/Seq/seq_led.d \
./Src/Seq/seq_live_rec_capture.d \
./Src/Seq/seq_model.d \
./Src/Seq/seq_output_guard.d \
./Src/Seq/seq_param_iface.d \
./Src/Seq/seq_persistence.d \
./Src/Seq/seq_play_scheduler.d \
./Src/Seq/seq_runtime.d \
./Src/Seq/seq_transport_fsm.d 

OBJS += \
./Src/Seq/seq_boundary_engine.o \
./Src/Seq/seq_clipboard.o \
./Src/Seq/seq_clock_bridge.o \
./Src/Seq/seq_edit.o \
./Src/Seq/seq_led.o \
./Src/Seq/seq_live_rec_capture.o \
./Src/Seq/seq_model.o \
./Src/Seq/seq_output_guard.o \
./Src/Seq/seq_param_iface.o \
./Src/Seq/seq_persistence.o \
./Src/Seq/seq_play_scheduler.o \
./Src/Seq/seq_runtime.o \
./Src/Seq/seq_transport_fsm.o 


# Each subdirectory must supply rules for building sources it contributes
Src/Seq/%.o Src/Seq/%.su Src/Seq/%.cyclo: ../Src/Seq/%.c Src/Seq/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Drv_app/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Core" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/MIDI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Param" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Storage" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/UI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/SD" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/U8g2" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/Third_Party/FatFs/src" -Og -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Src-2f-Seq

clean-Src-2f-Seq:
	-$(RM) ./Src/Seq/seq_boundary_engine.cyclo ./Src/Seq/seq_boundary_engine.d ./Src/Seq/seq_boundary_engine.o ./Src/Seq/seq_boundary_engine.su ./Src/Seq/seq_clipboard.cyclo ./Src/Seq/seq_clipboard.d ./Src/Seq/seq_clipboard.o ./Src/Seq/seq_clipboard.su ./Src/Seq/seq_clock_bridge.cyclo ./Src/Seq/seq_clock_bridge.d ./Src/Seq/seq_clock_bridge.o ./Src/Seq/seq_clock_bridge.su ./Src/Seq/seq_edit.cyclo ./Src/Seq/seq_edit.d ./Src/Seq/seq_edit.o ./Src/Seq/seq_edit.su ./Src/Seq/seq_led.cyclo ./Src/Seq/seq_led.d ./Src/Seq/seq_led.o ./Src/Seq/seq_led.su ./Src/Seq/seq_live_rec_capture.cyclo ./Src/Seq/seq_live_rec_capture.d ./Src/Seq/seq_live_rec_capture.o ./Src/Seq/seq_live_rec_capture.su ./Src/Seq/seq_model.cyclo ./Src/Seq/seq_model.d ./Src/Seq/seq_model.o ./Src/Seq/seq_model.su ./Src/Seq/seq_output_guard.cyclo ./Src/Seq/seq_output_guard.d ./Src/Seq/seq_output_guard.o ./Src/Seq/seq_output_guard.su ./Src/Seq/seq_param_iface.cyclo ./Src/Seq/seq_param_iface.d ./Src/Seq/seq_param_iface.o ./Src/Seq/seq_param_iface.su ./Src/Seq/seq_persistence.cyclo ./Src/Seq/seq_persistence.d ./Src/Seq/seq_persistence.o ./Src/Seq/seq_persistence.su ./Src/Seq/seq_play_scheduler.cyclo ./Src/Seq/seq_play_scheduler.d ./Src/Seq/seq_play_scheduler.o ./Src/Seq/seq_play_scheduler.su ./Src/Seq/seq_runtime.cyclo ./Src/Seq/seq_runtime.d ./Src/Seq/seq_runtime.o ./Src/Seq/seq_runtime.su ./Src/Seq/seq_transport_fsm.cyclo ./Src/Seq/seq_transport_fsm.d ./Src/Seq/seq_transport_fsm.o ./Src/Seq/seq_transport_fsm.su

.PHONY: clean-Src-2f-Seq

