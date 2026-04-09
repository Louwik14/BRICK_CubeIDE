################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
S_SRCS += \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g431xx.s \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g441xx.s \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g471xx.s \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g473xx.s \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g474xx.s \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g483xx.s \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g484xx.s \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32gbk1cb.s 

S_DEPS += \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g431xx.d \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g441xx.d \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g471xx.d \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g473xx.d \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g474xx.d \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g483xx.d \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g484xx.d \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32gbk1cb.d 

OBJS += \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g431xx.o \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g441xx.o \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g471xx.o \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g473xx.o \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g474xx.o \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g483xx.o \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g484xx.o \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32gbk1cb.o 


# Each subdirectory must supply rules for building sources it contributes
mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/%.o: ../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/%.s mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/subdir.mk
	arm-none-eabi-gcc -mcpu=cortex-m7 -DDEBUG -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -x assembler-with-cpp -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@" "$<"

clean: clean-mutable_instruments-2f-stmlib-2f-third_party-2f-STM-2f-CMSIS-2f-CM3_g4xx-2f-startup-2f-gcc

clean-mutable_instruments-2f-stmlib-2f-third_party-2f-STM-2f-CMSIS-2f-CM3_g4xx-2f-startup-2f-gcc:
	-$(RM) ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g431xx.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g431xx.o ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g441xx.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g441xx.o ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g471xx.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g471xx.o ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g473xx.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g473xx.o ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g474xx.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g474xx.o ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g483xx.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g483xx.o ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g484xx.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32g484xx.o ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32gbk1cb.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_g4xx/startup/gcc/startup_stm32gbk1cb.o

.PHONY: clean-mutable_instruments-2f-stmlib-2f-third_party-2f-STM-2f-CMSIS-2f-CM3_g4xx-2f-startup-2f-gcc

