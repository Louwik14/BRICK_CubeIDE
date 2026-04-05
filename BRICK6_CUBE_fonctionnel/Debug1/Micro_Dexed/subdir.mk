################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../Micro_Dexed/EngineMkI.cpp \
../Micro_Dexed/PluginFx.cpp \
../Micro_Dexed/dexed.cpp \
../Micro_Dexed/dx7note.cpp \
../Micro_Dexed/env.cpp \
../Micro_Dexed/exp2.cpp \
../Micro_Dexed/fm_core.cpp \
../Micro_Dexed/fm_op_kernel.cpp \
../Micro_Dexed/freqlut.cpp \
../Micro_Dexed/lfo.cpp \
../Micro_Dexed/microdexed_marki_minimal.cpp \
../Micro_Dexed/pitchenv.cpp \
../Micro_Dexed/porta.cpp \
../Micro_Dexed/sin.cpp 

OBJS += \
./Micro_Dexed/EngineMkI.o \
./Micro_Dexed/PluginFx.o \
./Micro_Dexed/dexed.o \
./Micro_Dexed/dx7note.o \
./Micro_Dexed/env.o \
./Micro_Dexed/exp2.o \
./Micro_Dexed/fm_core.o \
./Micro_Dexed/fm_op_kernel.o \
./Micro_Dexed/freqlut.o \
./Micro_Dexed/lfo.o \
./Micro_Dexed/microdexed_marki_minimal.o \
./Micro_Dexed/pitchenv.o \
./Micro_Dexed/porta.o \
./Micro_Dexed/sin.o 

CPP_DEPS += \
./Micro_Dexed/EngineMkI.d \
./Micro_Dexed/PluginFx.d \
./Micro_Dexed/dexed.d \
./Micro_Dexed/dx7note.d \
./Micro_Dexed/env.d \
./Micro_Dexed/exp2.d \
./Micro_Dexed/fm_core.d \
./Micro_Dexed/fm_op_kernel.d \
./Micro_Dexed/freqlut.d \
./Micro_Dexed/lfo.d \
./Micro_Dexed/microdexed_marki_minimal.d \
./Micro_Dexed/pitchenv.d \
./Micro_Dexed/porta.d \
./Micro_Dexed/sin.d 


# Each subdirectory must supply rules for building sources it contributes
Micro_Dexed/%.o Micro_Dexed/%.su Micro_Dexed/%.cyclo: ../Micro_Dexed/%.cpp Micro_Dexed/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m7 -std=gnu++14 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Micro_Dexed" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Juno" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Daisy_SP" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments/stmlib" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/freeverb-main/Components" -O0 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -Wall -ffast-math -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Micro_Dexed

clean-Micro_Dexed:
	-$(RM) ./Micro_Dexed/EngineMkI.cyclo ./Micro_Dexed/EngineMkI.d ./Micro_Dexed/EngineMkI.o ./Micro_Dexed/EngineMkI.su ./Micro_Dexed/PluginFx.cyclo ./Micro_Dexed/PluginFx.d ./Micro_Dexed/PluginFx.o ./Micro_Dexed/PluginFx.su ./Micro_Dexed/dexed.cyclo ./Micro_Dexed/dexed.d ./Micro_Dexed/dexed.o ./Micro_Dexed/dexed.su ./Micro_Dexed/dx7note.cyclo ./Micro_Dexed/dx7note.d ./Micro_Dexed/dx7note.o ./Micro_Dexed/dx7note.su ./Micro_Dexed/env.cyclo ./Micro_Dexed/env.d ./Micro_Dexed/env.o ./Micro_Dexed/env.su ./Micro_Dexed/exp2.cyclo ./Micro_Dexed/exp2.d ./Micro_Dexed/exp2.o ./Micro_Dexed/exp2.su ./Micro_Dexed/fm_core.cyclo ./Micro_Dexed/fm_core.d ./Micro_Dexed/fm_core.o ./Micro_Dexed/fm_core.su ./Micro_Dexed/fm_op_kernel.cyclo ./Micro_Dexed/fm_op_kernel.d ./Micro_Dexed/fm_op_kernel.o ./Micro_Dexed/fm_op_kernel.su ./Micro_Dexed/freqlut.cyclo ./Micro_Dexed/freqlut.d ./Micro_Dexed/freqlut.o ./Micro_Dexed/freqlut.su ./Micro_Dexed/lfo.cyclo ./Micro_Dexed/lfo.d ./Micro_Dexed/lfo.o ./Micro_Dexed/lfo.su ./Micro_Dexed/microdexed_marki_minimal.cyclo ./Micro_Dexed/microdexed_marki_minimal.d ./Micro_Dexed/microdexed_marki_minimal.o ./Micro_Dexed/microdexed_marki_minimal.su ./Micro_Dexed/pitchenv.cyclo ./Micro_Dexed/pitchenv.d ./Micro_Dexed/pitchenv.o ./Micro_Dexed/pitchenv.su ./Micro_Dexed/porta.cyclo ./Micro_Dexed/porta.d ./Micro_Dexed/porta.o ./Micro_Dexed/porta.su ./Micro_Dexed/sin.cyclo ./Micro_Dexed/sin.d ./Micro_Dexed/sin.o ./Micro_Dexed/sin.su

.PHONY: clean-Micro_Dexed

