################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../mutable_instruments/stmlib/third_party/STM/CMSIS/DSP_Lib/CommonTables/arm_common_tables.c \
../mutable_instruments/stmlib/third_party/STM/CMSIS/DSP_Lib/CommonTables/arm_const_structs.c 

C_DEPS += \
./mutable_instruments/stmlib/third_party/STM/CMSIS/DSP_Lib/CommonTables/arm_common_tables.d \
./mutable_instruments/stmlib/third_party/STM/CMSIS/DSP_Lib/CommonTables/arm_const_structs.d 

OBJS += \
./mutable_instruments/stmlib/third_party/STM/CMSIS/DSP_Lib/CommonTables/arm_common_tables.o \
./mutable_instruments/stmlib/third_party/STM/CMSIS/DSP_Lib/CommonTables/arm_const_structs.o 


# Each subdirectory must supply rules for building sources it contributes
mutable_instruments/stmlib/third_party/STM/CMSIS/DSP_Lib/CommonTables/%.o mutable_instruments/stmlib/third_party/STM/CMSIS/DSP_Lib/CommonTables/%.su mutable_instruments/stmlib/third_party/STM/CMSIS/DSP_Lib/CommonTables/%.cyclo: ../mutable_instruments/stmlib/third_party/STM/CMSIS/DSP_Lib/CommonTables/%.c mutable_instruments/stmlib/third_party/STM/CMSIS/DSP_Lib/CommonTables/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Drv_app/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Core" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/MIDI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Param" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Storage" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/UI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/SD" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/U8g2" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/Third_Party/FatFs/src" -O3 -ffunction-sections -fdata-sections -Wall -fno-math-errno -ffast-math -fsingle-precision-constant -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-mutable_instruments-2f-stmlib-2f-third_party-2f-STM-2f-CMSIS-2f-DSP_Lib-2f-CommonTables

clean-mutable_instruments-2f-stmlib-2f-third_party-2f-STM-2f-CMSIS-2f-DSP_Lib-2f-CommonTables:
	-$(RM) ./mutable_instruments/stmlib/third_party/STM/CMSIS/DSP_Lib/CommonTables/arm_common_tables.cyclo ./mutable_instruments/stmlib/third_party/STM/CMSIS/DSP_Lib/CommonTables/arm_common_tables.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/DSP_Lib/CommonTables/arm_common_tables.o ./mutable_instruments/stmlib/third_party/STM/CMSIS/DSP_Lib/CommonTables/arm_common_tables.su ./mutable_instruments/stmlib/third_party/STM/CMSIS/DSP_Lib/CommonTables/arm_const_structs.cyclo ./mutable_instruments/stmlib/third_party/STM/CMSIS/DSP_Lib/CommonTables/arm_const_structs.d ./mutable_instruments/stmlib/third_party/STM/CMSIS/DSP_Lib/CommonTables/arm_const_structs.o ./mutable_instruments/stmlib/third_party/STM/CMSIS/DSP_Lib/CommonTables/arm_const_structs.su

.PHONY: clean-mutable_instruments-2f-stmlib-2f-third_party-2f-STM-2f-CMSIS-2f-DSP_Lib-2f-CommonTables

