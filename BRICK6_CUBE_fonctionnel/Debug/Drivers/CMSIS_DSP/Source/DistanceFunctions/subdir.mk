################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/DistanceFunctions.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/DistanceFunctionsF16.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_boolean_distance.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_braycurtis_distance_f16.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_braycurtis_distance_f32.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_canberra_distance_f16.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_canberra_distance_f32.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_chebyshev_distance_f16.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_chebyshev_distance_f32.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_chebyshev_distance_f64.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cityblock_distance_f16.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cityblock_distance_f32.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cityblock_distance_f64.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_correlation_distance_f16.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_correlation_distance_f32.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cosine_distance_f16.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cosine_distance_f32.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cosine_distance_f64.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dice_distance.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dtw_distance_f32.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dtw_init_window_q7.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dtw_path_f32.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_euclidean_distance_f16.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_euclidean_distance_f32.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_euclidean_distance_f64.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_hamming_distance.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_jaccard_distance.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_jensenshannon_distance_f16.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_jensenshannon_distance_f32.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_kulsinski_distance.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_minkowski_distance_f16.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_minkowski_distance_f32.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_rogerstanimoto_distance.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_russellrao_distance.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_sokalmichener_distance.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_sokalsneath_distance.c \
../Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_yule_distance.c 

OBJS += \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/DistanceFunctions.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/DistanceFunctionsF16.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_boolean_distance.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_braycurtis_distance_f16.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_braycurtis_distance_f32.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_canberra_distance_f16.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_canberra_distance_f32.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_chebyshev_distance_f16.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_chebyshev_distance_f32.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_chebyshev_distance_f64.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cityblock_distance_f16.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cityblock_distance_f32.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cityblock_distance_f64.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_correlation_distance_f16.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_correlation_distance_f32.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cosine_distance_f16.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cosine_distance_f32.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cosine_distance_f64.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dice_distance.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dtw_distance_f32.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dtw_init_window_q7.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dtw_path_f32.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_euclidean_distance_f16.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_euclidean_distance_f32.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_euclidean_distance_f64.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_hamming_distance.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_jaccard_distance.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_jensenshannon_distance_f16.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_jensenshannon_distance_f32.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_kulsinski_distance.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_minkowski_distance_f16.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_minkowski_distance_f32.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_rogerstanimoto_distance.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_russellrao_distance.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_sokalmichener_distance.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_sokalsneath_distance.o \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_yule_distance.o 

C_DEPS += \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/DistanceFunctions.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/DistanceFunctionsF16.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_boolean_distance.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_braycurtis_distance_f16.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_braycurtis_distance_f32.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_canberra_distance_f16.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_canberra_distance_f32.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_chebyshev_distance_f16.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_chebyshev_distance_f32.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_chebyshev_distance_f64.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cityblock_distance_f16.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cityblock_distance_f32.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cityblock_distance_f64.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_correlation_distance_f16.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_correlation_distance_f32.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cosine_distance_f16.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cosine_distance_f32.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cosine_distance_f64.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dice_distance.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dtw_distance_f32.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dtw_init_window_q7.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dtw_path_f32.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_euclidean_distance_f16.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_euclidean_distance_f32.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_euclidean_distance_f64.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_hamming_distance.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_jaccard_distance.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_jensenshannon_distance_f16.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_jensenshannon_distance_f32.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_kulsinski_distance.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_minkowski_distance_f16.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_minkowski_distance_f32.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_rogerstanimoto_distance.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_russellrao_distance.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_sokalmichener_distance.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_sokalsneath_distance.d \
./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_yule_distance.d 


# Each subdirectory must supply rules for building sources it contributes
Drivers/CMSIS_DSP/Source/DistanceFunctions/%.o Drivers/CMSIS_DSP/Source/DistanceFunctions/%.su Drivers/CMSIS_DSP/Source/DistanceFunctions/%.cyclo: ../Drivers/CMSIS_DSP/Source/DistanceFunctions/%.c Drivers/CMSIS_DSP/Source/DistanceFunctions/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -Og -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Drivers-2f-CMSIS_DSP-2f-Source-2f-DistanceFunctions

clean-Drivers-2f-CMSIS_DSP-2f-Source-2f-DistanceFunctions:
	-$(RM) ./Drivers/CMSIS_DSP/Source/DistanceFunctions/DistanceFunctions.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/DistanceFunctions.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/DistanceFunctions.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/DistanceFunctions.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/DistanceFunctionsF16.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/DistanceFunctionsF16.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/DistanceFunctionsF16.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/DistanceFunctionsF16.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_boolean_distance.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_boolean_distance.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_boolean_distance.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_boolean_distance.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_braycurtis_distance_f16.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_braycurtis_distance_f16.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_braycurtis_distance_f16.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_braycurtis_distance_f16.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_braycurtis_distance_f32.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_braycurtis_distance_f32.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_braycurtis_distance_f32.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_braycurtis_distance_f32.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_canberra_distance_f16.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_canberra_distance_f16.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_canberra_distance_f16.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_canberra_distance_f16.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_canberra_distance_f32.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_canberra_distance_f32.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_canberra_distance_f32.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_canberra_distance_f32.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_chebyshev_distance_f16.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_chebyshev_distance_f16.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_chebyshev_distance_f16.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_chebyshev_distance_f16.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_chebyshev_distance_f32.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_chebyshev_distance_f32.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_chebyshev_distance_f32.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_chebyshev_distance_f32.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_chebyshev_distance_f64.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_chebyshev_distance_f64.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_chebyshev_distance_f64.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_chebyshev_distance_f64.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cityblock_distance_f16.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cityblock_distance_f16.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cityblock_distance_f16.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cityblock_distance_f16.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cityblock_distance_f32.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cityblock_distance_f32.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cityblock_distance_f32.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cityblock_distance_f32.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cityblock_distance_f64.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cityblock_distance_f64.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cityblock_distance_f64.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cityblock_distance_f64.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_correlation_distance_f16.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_correlation_distance_f16.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_correlation_distance_f16.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_correlation_distance_f16.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_correlation_distance_f32.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_correlation_distance_f32.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_correlation_distance_f32.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_correlation_distance_f32.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cosine_distance_f16.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cosine_distance_f16.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cosine_distance_f16.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cosine_distance_f16.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cosine_distance_f32.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cosine_distance_f32.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cosine_distance_f32.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cosine_distance_f32.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cosine_distance_f64.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cosine_distance_f64.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cosine_distance_f64.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_cosine_distance_f64.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dice_distance.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dice_distance.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dice_distance.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dice_distance.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dtw_distance_f32.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dtw_distance_f32.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dtw_distance_f32.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dtw_distance_f32.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dtw_init_window_q7.cyclo
	-$(RM) ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dtw_init_window_q7.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dtw_init_window_q7.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dtw_init_window_q7.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dtw_path_f32.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dtw_path_f32.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dtw_path_f32.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_dtw_path_f32.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_euclidean_distance_f16.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_euclidean_distance_f16.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_euclidean_distance_f16.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_euclidean_distance_f16.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_euclidean_distance_f32.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_euclidean_distance_f32.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_euclidean_distance_f32.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_euclidean_distance_f32.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_euclidean_distance_f64.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_euclidean_distance_f64.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_euclidean_distance_f64.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_euclidean_distance_f64.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_hamming_distance.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_hamming_distance.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_hamming_distance.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_hamming_distance.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_jaccard_distance.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_jaccard_distance.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_jaccard_distance.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_jaccard_distance.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_jensenshannon_distance_f16.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_jensenshannon_distance_f16.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_jensenshannon_distance_f16.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_jensenshannon_distance_f16.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_jensenshannon_distance_f32.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_jensenshannon_distance_f32.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_jensenshannon_distance_f32.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_jensenshannon_distance_f32.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_kulsinski_distance.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_kulsinski_distance.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_kulsinski_distance.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_kulsinski_distance.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_minkowski_distance_f16.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_minkowski_distance_f16.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_minkowski_distance_f16.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_minkowski_distance_f16.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_minkowski_distance_f32.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_minkowski_distance_f32.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_minkowski_distance_f32.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_minkowski_distance_f32.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_rogerstanimoto_distance.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_rogerstanimoto_distance.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_rogerstanimoto_distance.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_rogerstanimoto_distance.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_russellrao_distance.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_russellrao_distance.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_russellrao_distance.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_russellrao_distance.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_sokalmichener_distance.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_sokalmichener_distance.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_sokalmichener_distance.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_sokalmichener_distance.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_sokalsneath_distance.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_sokalsneath_distance.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_sokalsneath_distance.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_sokalsneath_distance.su ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_yule_distance.cyclo ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_yule_distance.d ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_yule_distance.o ./Drivers/CMSIS_DSP/Source/DistanceFunctions/arm_yule_distance.su

.PHONY: clean-Drivers-2f-CMSIS_DSP-2f-Source-2f-DistanceFunctions

