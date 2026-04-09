################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
S_SRCS += \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f10x/startup/iar/startup_stm32f10x_cl.s \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f10x/startup/iar/startup_stm32f10x_hd.s \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f10x/startup/iar/startup_stm32f10x_ld.s \
../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f10x/startup/iar/startup_stm32f10x_md.s 

S_DEPS += \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f10x/startup/iar/startup_stm32f10x_cl.d \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f10x/startup/iar/startup_stm32f10x_hd.d \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f10x/startup/iar/startup_stm32f10x_ld.d \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f10x/startup/iar/startup_stm32f10x_md.d 

OBJS += \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f10x/startup/iar/startup_stm32f10x_cl.o \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f10x/startup/iar/startup_stm32f10x_hd.o \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f10x/startup/iar/startup_stm32f10x_ld.o \
./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f10x/startup/iar/startup_stm32f10x_md.o 


# Each subdirectory must supply rules for building sources it contributes
mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f10x/startup/iar/%.o: ../mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f10x/startup/iar/%.s mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f10x/startup/iar/subdir.mk
	arm-none-eabi-gcc -mcpu=cortex-m7 -DDEBUG -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -x assembler-with-cpp -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@" "$<"

clean: clean-mutable_instruments-2f-stmlib-2f-third_party-2f-STM-2f-CMSIS-2f-CM3_f10x-2f-startup-2f-iar

clean-mutable_instruments-2f-stmlib-2f-third_party-2f-STM-2f-CMSIS-2f-CM3_f10x-2f-startup-2f-iar:
	-$(RM) ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f10x/startup/iar/startup_stm32f10x_cl.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f10x/startup/iar/startup_stm32f10x_cl.o ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f10x/startup/iar/startup_stm32f10x_hd.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f10x/startup/iar/startup_stm32f10x_hd.o ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f10x/startup/iar/startup_stm32f10x_ld.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f10x/startup/iar/startup_stm32f10x_ld.o ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f10x/startup/iar/startup_stm32f10x_md.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/CM3_f10x/startup/iar/startup_stm32f10x_md.o

.PHONY: clean-mutable_instruments-2f-stmlib-2f-third_party-2f-STM-2f-CMSIS-2f-CM3_f10x-2f-startup-2f-iar

