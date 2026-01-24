################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
C:/Users/developpeur/STM32Cube/Repository/STM32Cube_FW_H7_V1.12.1/Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval.c \
C:/Users/developpeur/STM32Cube/Repository/STM32Cube_FW_H7_V1.12.1/Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval_bus.c \
C:/Users/developpeur/STM32Cube/Repository/STM32Cube_FW_H7_V1.12.1/Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval_io.c 

OBJS += \
./Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval.o \
./Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval_bus.o \
./Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval_io.o 

C_DEPS += \
./Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval.d \
./Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval_bus.d \
./Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval_io.d 


# Each subdirectory must supply rules for building sources it contributes
Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval.o: C:/Users/developpeur/STM32Cube/Repository/STM32Cube_FW_H7_V1.12.1/Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval.c Drivers/BSP/STM32H743I-EVAL/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DSTM32H743xx -DUSE_HAL_DRIVER -DUSE_PWR_LDO_SUPPLY -DUSE_STM32H743I_EVAL -c -I../../Inc -I../../../../../../../Drivers/CMSIS/Include -I../../../../../../../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../../../../../../../Drivers/STM32H7xx_HAL_Driver/Inc -I../../../../../../../Drivers/BSP/STM32H743I-EVAL -I../../../../../../../Drivers/BSP/Components/Common -I../../../../../../../Utilities/Fonts -I../../../../../../../Utilities/CPU -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval_bus.o: C:/Users/developpeur/STM32Cube/Repository/STM32Cube_FW_H7_V1.12.1/Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval_bus.c Drivers/BSP/STM32H743I-EVAL/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DSTM32H743xx -DUSE_HAL_DRIVER -DUSE_PWR_LDO_SUPPLY -DUSE_STM32H743I_EVAL -c -I../../Inc -I../../../../../../../Drivers/CMSIS/Include -I../../../../../../../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../../../../../../../Drivers/STM32H7xx_HAL_Driver/Inc -I../../../../../../../Drivers/BSP/STM32H743I-EVAL -I../../../../../../../Drivers/BSP/Components/Common -I../../../../../../../Utilities/Fonts -I../../../../../../../Utilities/CPU -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval_io.o: C:/Users/developpeur/STM32Cube/Repository/STM32Cube_FW_H7_V1.12.1/Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval_io.c Drivers/BSP/STM32H743I-EVAL/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DSTM32H743xx -DUSE_HAL_DRIVER -DUSE_PWR_LDO_SUPPLY -DUSE_STM32H743I_EVAL -c -I../../Inc -I../../../../../../../Drivers/CMSIS/Include -I../../../../../../../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../../../../../../../Drivers/STM32H7xx_HAL_Driver/Inc -I../../../../../../../Drivers/BSP/STM32H743I-EVAL -I../../../../../../../Drivers/BSP/Components/Common -I../../../../../../../Utilities/Fonts -I../../../../../../../Utilities/CPU -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Drivers-2f-BSP-2f-STM32H743I-2d-EVAL

clean-Drivers-2f-BSP-2f-STM32H743I-2d-EVAL:
	-$(RM) ./Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval.cyclo ./Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval.d ./Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval.o ./Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval.su ./Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval_bus.cyclo ./Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval_bus.d ./Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval_bus.o ./Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval_bus.su ./Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval_io.cyclo ./Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval_io.d ./Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval_io.o ./Drivers/BSP/STM32H743I-EVAL/stm32h743i_eval_io.su

.PHONY: clean-Drivers-2f-BSP-2f-STM32H743I-2d-EVAL

