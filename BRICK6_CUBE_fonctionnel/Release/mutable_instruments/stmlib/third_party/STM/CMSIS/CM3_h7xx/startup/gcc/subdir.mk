################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
S_SRCS += \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_h7xx/startup/gcc/startup_stm32h743xx.s \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_h7xx/startup/gcc/startup_stm32h753xx.s 

S_DEPS += \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_h7xx/startup/gcc/startup_stm32h743xx.d \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_h7xx/startup/gcc/startup_stm32h753xx.d 

OBJS += \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_h7xx/startup/gcc/startup_stm32h743xx.o \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_h7xx/startup/gcc/startup_stm32h753xx.o 


# Each subdirectory must supply rules for building sources it contributes
mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_h7xx/startup/gcc/%.o: ../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_h7xx/startup/gcc/%.s mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_h7xx/startup/gcc/subdir.mk
	arm-none-eabi-gcc -mcpu=cortex-m7 -DDEBUG -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -x assembler-with-cpp -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@" "$<"

clean: clean-mutable_instruments-2f-stmlib-2f-third_party-2f-STM-2f-CMSIS-2f-CM3_h7xx-2f-startup-2f-gcc

clean-mutable_instruments-2f-stmlib-2f-third_party-2f-STM-2f-CMSIS-2f-CM3_h7xx-2f-startup-2f-gcc:
	-$(RM) ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_h7xx/startup/gcc/startup_stm32h743xx.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_h7xx/startup/gcc/startup_stm32h743xx.o ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_h7xx/startup/gcc/startup_stm32h753xx.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_h7xx/startup/gcc/startup_stm32h753xx.o

.PHONY: clean-mutable_instruments-2f-stmlib-2f-third_party-2f-STM-2f-CMSIS-2f-CM3_h7xx-2f-startup-2f-gcc

