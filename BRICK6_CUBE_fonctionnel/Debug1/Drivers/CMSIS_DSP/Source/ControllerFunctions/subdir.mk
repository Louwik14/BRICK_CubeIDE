################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_init_f32.c \
../Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_init_q15.c \
../Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_init_q31.c \
../Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_reset_f32.c \
../Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_reset_q15.c \
../Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_reset_q31.c \
../Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_sin_cos_f32.c \
../Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_sin_cos_q31.c 

C_DEPS += \
./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_init_f32.d \
./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_init_q15.d \
./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_init_q31.d \
./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_reset_f32.d \
./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_reset_q15.d \
./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_reset_q31.d \
./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_sin_cos_f32.d \
./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_sin_cos_q31.d 

OBJS += \
./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_init_f32.o \
./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_init_q15.o \
./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_init_q31.o \
./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_reset_f32.o \
./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_reset_q15.o \
./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_reset_q31.o \
./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_sin_cos_f32.o \
./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_sin_cos_q31.o 


# Each subdirectory must supply rules for building sources it contributes
Drivers/CMSIS_DSP/Source/ControllerFunctions/%.o Drivers/CMSIS_DSP/Source/ControllerFunctions/%.su Drivers/CMSIS_DSP/Source/ControllerFunctions/%.cyclo: ../Drivers/CMSIS_DSP/Source/ControllerFunctions/%.c Drivers/CMSIS_DSP/Source/ControllerFunctions/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Drv_app/Inc" -Og -ffunction-sections -fdata-sections -Wall -fno-math-errno -fsingle-precision-constant -ffast-math -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Drivers-2f-CMSIS_DSP-2f-Source-2f-ControllerFunctions

clean-Drivers-2f-CMSIS_DSP-2f-Source-2f-ControllerFunctions:
	-$(RM) ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_init_f32.cyclo ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_init_f32.d ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_init_f32.o ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_init_f32.su ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_init_q15.cyclo ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_init_q15.d ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_init_q15.o ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_init_q15.su ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_init_q31.cyclo ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_init_q31.d ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_init_q31.o ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_init_q31.su ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_reset_f32.cyclo ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_reset_f32.d ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_reset_f32.o ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_reset_f32.su ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_reset_q15.cyclo ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_reset_q15.d ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_reset_q15.o ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_reset_q15.su ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_reset_q31.cyclo ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_reset_q31.d ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_reset_q31.o ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_pid_reset_q31.su ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_sin_cos_f32.cyclo ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_sin_cos_f32.d ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_sin_cos_f32.o ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_sin_cos_f32.su ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_sin_cos_q31.cyclo ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_sin_cos_q31.d ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_sin_cos_q31.o ./Drivers/CMSIS_DSP/Source/ControllerFunctions/arm_sin_cos_q31.su

.PHONY: clean-Drivers-2f-CMSIS_DSP-2f-Source-2f-ControllerFunctions

