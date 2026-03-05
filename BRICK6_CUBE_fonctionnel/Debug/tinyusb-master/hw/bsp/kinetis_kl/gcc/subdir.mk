################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
S_UPPER_SRCS += \
../tinyusb-master/hw/bsp/kinetis_kl/gcc/startup_MKL25Z4.S 

OBJS += \
./tinyusb-master/hw/bsp/kinetis_kl/gcc/startup_MKL25Z4.o 

S_UPPER_DEPS += \
./tinyusb-master/hw/bsp/kinetis_kl/gcc/startup_MKL25Z4.d 


# Each subdirectory must supply rules for building sources it contributes
tinyusb-master/hw/bsp/kinetis_kl/gcc/%.o: ../tinyusb-master/hw/bsp/kinetis_kl/gcc/%.S tinyusb-master/hw/bsp/kinetis_kl/gcc/subdir.mk
	arm-none-eabi-gcc -mcpu=cortex-m7 -g3 -DDEBUG -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -x assembler-with-cpp -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@" "$<"

clean: clean-tinyusb-2d-master-2f-hw-2f-bsp-2f-kinetis_kl-2f-gcc

clean-tinyusb-2d-master-2f-hw-2f-bsp-2f-kinetis_kl-2f-gcc:
	-$(RM) ./tinyusb-master/hw/bsp/kinetis_kl/gcc/startup_MKL25Z4.d ./tinyusb-master/hw/bsp/kinetis_kl/gcc/startup_MKL25Z4.o

.PHONY: clean-tinyusb-2d-master-2f-hw-2f-bsp-2f-kinetis_kl-2f-gcc

