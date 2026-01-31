################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
S_UPPER_SRCS += \
../App/TinyUSB/hw/mcu/bridgetek/ft9xx/scripts/crt0.S 

OBJS += \
./App/TinyUSB/hw/mcu/bridgetek/ft9xx/scripts/crt0.o 

S_UPPER_DEPS += \
./App/TinyUSB/hw/mcu/bridgetek/ft9xx/scripts/crt0.d 


# Each subdirectory must supply rules for building sources it contributes
App/TinyUSB/hw/mcu/bridgetek/ft9xx/scripts/%.o: ../App/TinyUSB/hw/mcu/bridgetek/ft9xx/scripts/%.S App/TinyUSB/hw/mcu/bridgetek/ft9xx/scripts/subdir.mk
	arm-none-eabi-gcc -mcpu=cortex-m7 -g3 -DDEBUG -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -x assembler-with-cpp -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@" "$<"

clean: clean-App-2f-TinyUSB-2f-hw-2f-mcu-2f-bridgetek-2f-ft9xx-2f-scripts

clean-App-2f-TinyUSB-2f-hw-2f-mcu-2f-bridgetek-2f-ft9xx-2f-scripts:
	-$(RM) ./App/TinyUSB/hw/mcu/bridgetek/ft9xx/scripts/crt0.d ./App/TinyUSB/hw/mcu/bridgetek/ft9xx/scripts/crt0.o

.PHONY: clean-App-2f-TinyUSB-2f-hw-2f-mcu-2f-bridgetek-2f-ft9xx-2f-scripts

