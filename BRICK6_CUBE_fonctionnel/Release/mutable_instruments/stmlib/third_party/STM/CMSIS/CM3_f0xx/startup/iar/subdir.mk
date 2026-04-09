################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
S_SRCS += \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f030.s \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f030xc.s \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f031.s \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f042.s \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f051.s \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f070x6.s \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f070xb.s \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f072.s \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f091.s \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f0xx.s \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f0xx_ld.s 

S_DEPS += \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f030.d \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f030xc.d \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f031.d \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f042.d \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f051.d \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f070x6.d \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f070xb.d \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f072.d \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f091.d \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f0xx.d \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f0xx_ld.d 

OBJS += \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f030.o \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f030xc.o \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f031.o \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f042.o \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f051.o \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f070x6.o \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f070xb.o \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f072.o \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f091.o \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f0xx.o \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f0xx_ld.o 


# Each subdirectory must supply rules for building sources it contributes
mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/%.o: ../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/%.s mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/subdir.mk
	arm-none-eabi-gcc -mcpu=cortex-m7 -DDEBUG -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -x assembler-with-cpp -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@" "$<"

clean: clean-mutable_instruments-2f-stmlib-2f-third_party-2f-STM-2f-CMSIS-2f-CM3_f0xx-2f-startup-2f-iar

clean-mutable_instruments-2f-stmlib-2f-third_party-2f-STM-2f-CMSIS-2f-CM3_f0xx-2f-startup-2f-iar:
	-$(RM) ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f030.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f030.o ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f030xc.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f030xc.o ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f031.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f031.o ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f042.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f042.o ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f051.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f051.o ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f070x6.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f070x6.o ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f070xb.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f070xb.o ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f072.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f072.o ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f091.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f091.o ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f0xx.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f0xx.o ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f0xx_ld.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f0xx/startup/iar/startup_stm32f0xx_ld.o

.PHONY: clean-mutable_instruments-2f-stmlib-2f-third_party-2f-STM-2f-CMSIS-2f-CM3_f0xx-2f-startup-2f-iar

