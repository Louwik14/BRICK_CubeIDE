################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Drivers/CMSIS_DSP/Source/SVMFunctions/SVMFunctions.c \
../Drivers/CMSIS_DSP/Source/SVMFunctions/SVMFunctionsF16.c \
../Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_init_f16.c \
../Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_init_f32.c \
../Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_predict_f16.c \
../Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_predict_f32.c \
../Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_init_f16.c \
../Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_init_f32.c \
../Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_predict_f16.c \
../Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_predict_f32.c \
../Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_init_f16.c \
../Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_init_f32.c \
../Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_predict_f16.c \
../Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_predict_f32.c \
../Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_init_f16.c \
../Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_init_f32.c \
../Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_predict_f16.c \
../Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_predict_f32.c 

OBJS += \
./Drivers/CMSIS_DSP/Source/SVMFunctions/SVMFunctions.o \
./Drivers/CMSIS_DSP/Source/SVMFunctions/SVMFunctionsF16.o \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_init_f16.o \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_init_f32.o \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_predict_f16.o \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_predict_f32.o \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_init_f16.o \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_init_f32.o \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_predict_f16.o \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_predict_f32.o \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_init_f16.o \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_init_f32.o \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_predict_f16.o \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_predict_f32.o \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_init_f16.o \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_init_f32.o \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_predict_f16.o \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_predict_f32.o 

C_DEPS += \
./Drivers/CMSIS_DSP/Source/SVMFunctions/SVMFunctions.d \
./Drivers/CMSIS_DSP/Source/SVMFunctions/SVMFunctionsF16.d \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_init_f16.d \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_init_f32.d \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_predict_f16.d \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_predict_f32.d \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_init_f16.d \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_init_f32.d \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_predict_f16.d \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_predict_f32.d \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_init_f16.d \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_init_f32.d \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_predict_f16.d \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_predict_f32.d \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_init_f16.d \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_init_f32.d \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_predict_f16.d \
./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_predict_f32.d 


# Each subdirectory must supply rules for building sources it contributes
Drivers/CMSIS_DSP/Source/SVMFunctions/%.o Drivers/CMSIS_DSP/Source/SVMFunctions/%.su Drivers/CMSIS_DSP/Source/SVMFunctions/%.cyclo: ../Drivers/CMSIS_DSP/Source/SVMFunctions/%.c Drivers/CMSIS_DSP/Source/SVMFunctions/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -Og -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Drivers-2f-CMSIS_DSP-2f-Source-2f-SVMFunctions

clean-Drivers-2f-CMSIS_DSP-2f-Source-2f-SVMFunctions:
	-$(RM) ./Drivers/CMSIS_DSP/Source/SVMFunctions/SVMFunctions.cyclo ./Drivers/CMSIS_DSP/Source/SVMFunctions/SVMFunctions.d ./Drivers/CMSIS_DSP/Source/SVMFunctions/SVMFunctions.o ./Drivers/CMSIS_DSP/Source/SVMFunctions/SVMFunctions.su ./Drivers/CMSIS_DSP/Source/SVMFunctions/SVMFunctionsF16.cyclo ./Drivers/CMSIS_DSP/Source/SVMFunctions/SVMFunctionsF16.d ./Drivers/CMSIS_DSP/Source/SVMFunctions/SVMFunctionsF16.o ./Drivers/CMSIS_DSP/Source/SVMFunctions/SVMFunctionsF16.su ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_init_f16.cyclo ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_init_f16.d ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_init_f16.o ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_init_f16.su ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_init_f32.cyclo ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_init_f32.d ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_init_f32.o ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_init_f32.su ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_predict_f16.cyclo ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_predict_f16.d ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_predict_f16.o ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_predict_f16.su ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_predict_f32.cyclo ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_predict_f32.d ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_predict_f32.o ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_linear_predict_f32.su ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_init_f16.cyclo ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_init_f16.d ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_init_f16.o ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_init_f16.su ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_init_f32.cyclo ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_init_f32.d ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_init_f32.o ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_init_f32.su ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_predict_f16.cyclo ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_predict_f16.d ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_predict_f16.o ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_predict_f16.su ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_predict_f32.cyclo ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_predict_f32.d ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_predict_f32.o ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_polynomial_predict_f32.su ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_init_f16.cyclo ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_init_f16.d ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_init_f16.o ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_init_f16.su ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_init_f32.cyclo ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_init_f32.d ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_init_f32.o ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_init_f32.su ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_predict_f16.cyclo ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_predict_f16.d ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_predict_f16.o ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_predict_f16.su ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_predict_f32.cyclo ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_predict_f32.d ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_predict_f32.o ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_rbf_predict_f32.su ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_init_f16.cyclo ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_init_f16.d ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_init_f16.o ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_init_f16.su ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_init_f32.cyclo ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_init_f32.d ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_init_f32.o ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_init_f32.su ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_predict_f16.cyclo ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_predict_f16.d ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_predict_f16.o ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_predict_f16.su ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_predict_f32.cyclo ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_predict_f32.d ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_predict_f32.o ./Drivers/CMSIS_DSP/Source/SVMFunctions/arm_svm_sigmoid_predict_f32.su

.PHONY: clean-Drivers-2f-CMSIS_DSP-2f-Source-2f-SVMFunctions

