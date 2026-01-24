################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
C:/Users/developpeur/STM32Cube/Repository/STM32Cube_FW_H7_V1.12.1/Projects/STM32H743I-EVAL/Examples/SD/SD_ReadWrite_IT/Src/main.c \
C:/Users/developpeur/STM32Cube/Repository/STM32Cube_FW_H7_V1.12.1/Projects/STM32H743I-EVAL/Examples/SD/SD_ReadWrite_IT/Src/stm32h7xx_hal_msp.c \
C:/Users/developpeur/STM32Cube/Repository/STM32Cube_FW_H7_V1.12.1/Projects/STM32H743I-EVAL/Examples/SD/SD_ReadWrite_IT/Src/stm32h7xx_it.c \
../Example/User/syscalls.c \
../Example/User/sysmem.c 

OBJS += \
./Example/User/main.o \
./Example/User/stm32h7xx_hal_msp.o \
./Example/User/stm32h7xx_it.o \
./Example/User/syscalls.o \
./Example/User/sysmem.o 

C_DEPS += \
./Example/User/main.d \
./Example/User/stm32h7xx_hal_msp.d \
./Example/User/stm32h7xx_it.d \
./Example/User/syscalls.d \
./Example/User/sysmem.d 


# Each subdirectory must supply rules for building sources it contributes
Example/User/main.o: C:/Users/developpeur/STM32Cube/Repository/STM32Cube_FW_H7_V1.12.1/Projects/STM32H743I-EVAL/Examples/SD/SD_ReadWrite_IT/Src/main.c Example/User/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_IOEXPANDER -DUSE_HAL_DRIVER -DUSE_PWR_LDO_SUPPLY -DSTM32H743xx -DUSE_STM32H743I_EVAL -c -I../../Inc -I../../../../../../../Drivers/CMSIS/Include -I../../../../../../../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../../../../../../../Drivers/STM32H7xx_HAL_Driver/Inc -I../../../../../../../Drivers/BSP/STM32H743I-EVAL -I../../../../../../../Drivers/BSP/Components/Common -I../../../../../../../Utilities/Fonts -I../../../../../../../Utilities/CPU -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Example/User/stm32h7xx_hal_msp.o: C:/Users/developpeur/STM32Cube/Repository/STM32Cube_FW_H7_V1.12.1/Projects/STM32H743I-EVAL/Examples/SD/SD_ReadWrite_IT/Src/stm32h7xx_hal_msp.c Example/User/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_IOEXPANDER -DUSE_HAL_DRIVER -DUSE_PWR_LDO_SUPPLY -DSTM32H743xx -DUSE_STM32H743I_EVAL -c -I../../Inc -I../../../../../../../Drivers/CMSIS/Include -I../../../../../../../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../../../../../../../Drivers/STM32H7xx_HAL_Driver/Inc -I../../../../../../../Drivers/BSP/STM32H743I-EVAL -I../../../../../../../Drivers/BSP/Components/Common -I../../../../../../../Utilities/Fonts -I../../../../../../../Utilities/CPU -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Example/User/stm32h7xx_it.o: C:/Users/developpeur/STM32Cube/Repository/STM32Cube_FW_H7_V1.12.1/Projects/STM32H743I-EVAL/Examples/SD/SD_ReadWrite_IT/Src/stm32h7xx_it.c Example/User/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_IOEXPANDER -DUSE_HAL_DRIVER -DUSE_PWR_LDO_SUPPLY -DSTM32H743xx -DUSE_STM32H743I_EVAL -c -I../../Inc -I../../../../../../../Drivers/CMSIS/Include -I../../../../../../../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../../../../../../../Drivers/STM32H7xx_HAL_Driver/Inc -I../../../../../../../Drivers/BSP/STM32H743I-EVAL -I../../../../../../../Drivers/BSP/Components/Common -I../../../../../../../Utilities/Fonts -I../../../../../../../Utilities/CPU -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Example/User/%.o Example/User/%.su Example/User/%.cyclo: ../Example/User/%.c Example/User/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_IOEXPANDER -DUSE_HAL_DRIVER -DUSE_PWR_LDO_SUPPLY -DSTM32H743xx -DUSE_STM32H743I_EVAL -c -I../../Inc -I../../../../../../../Drivers/CMSIS/Include -I../../../../../../../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../../../../../../../Drivers/STM32H7xx_HAL_Driver/Inc -I../../../../../../../Drivers/BSP/STM32H743I-EVAL -I../../../../../../../Drivers/BSP/Components/Common -I../../../../../../../Utilities/Fonts -I../../../../../../../Utilities/CPU -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Example-2f-User

clean-Example-2f-User:
	-$(RM) ./Example/User/main.cyclo ./Example/User/main.d ./Example/User/main.o ./Example/User/main.su ./Example/User/stm32h7xx_hal_msp.cyclo ./Example/User/stm32h7xx_hal_msp.d ./Example/User/stm32h7xx_hal_msp.o ./Example/User/stm32h7xx_hal_msp.su ./Example/User/stm32h7xx_it.cyclo ./Example/User/stm32h7xx_it.d ./Example/User/stm32h7xx_it.o ./Example/User/stm32h7xx_it.su ./Example/User/syscalls.cyclo ./Example/User/syscalls.d ./Example/User/syscalls.o ./Example/User/syscalls.su ./Example/User/sysmem.cyclo ./Example/User/sysmem.d ./Example/User/sysmem.o ./Example/User/sysmem.su

.PHONY: clean-Example-2f-User

