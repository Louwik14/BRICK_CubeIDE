################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../md-drum-synth-main/FmClapModel.cpp \
../md-drum-synth-main/FmCowbellModel.cpp \
../md-drum-synth-main/FmCymbalModel.cpp \
../md-drum-synth-main/FmKickModel.cpp \
../md-drum-synth-main/FmRimshotModel.cpp \
../md-drum-synth-main/FmSnareModel.cpp \
../md-drum-synth-main/FmTomModel.cpp \
../md-drum-synth-main/TRXBassDrum.cpp \
../md-drum-synth-main/TRXClaves.cpp \
../md-drum-synth-main/TRXHiHat.cpp \
../md-drum-synth-main/TRXSnareDrum.cpp 

OBJS += \
./md-drum-synth-main/FmClapModel.o \
./md-drum-synth-main/FmCowbellModel.o \
./md-drum-synth-main/FmCymbalModel.o \
./md-drum-synth-main/FmKickModel.o \
./md-drum-synth-main/FmRimshotModel.o \
./md-drum-synth-main/FmSnareModel.o \
./md-drum-synth-main/FmTomModel.o \
./md-drum-synth-main/TRXBassDrum.o \
./md-drum-synth-main/TRXClaves.o \
./md-drum-synth-main/TRXHiHat.o \
./md-drum-synth-main/TRXSnareDrum.o 

CPP_DEPS += \
./md-drum-synth-main/FmClapModel.d \
./md-drum-synth-main/FmCowbellModel.d \
./md-drum-synth-main/FmCymbalModel.d \
./md-drum-synth-main/FmKickModel.d \
./md-drum-synth-main/FmRimshotModel.d \
./md-drum-synth-main/FmSnareModel.d \
./md-drum-synth-main/FmTomModel.d \
./md-drum-synth-main/TRXBassDrum.d \
./md-drum-synth-main/TRXClaves.d \
./md-drum-synth-main/TRXHiHat.d \
./md-drum-synth-main/TRXSnareDrum.d 


# Each subdirectory must supply rules for building sources it contributes
md-drum-synth-main/%.o md-drum-synth-main/%.su md-drum-synth-main/%.cyclo: ../md-drum-synth-main/%.cpp md-drum-synth-main/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m7 -std=gnu++14 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Micro_Dexed" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Juno" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Daisy_SP" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/mutable_instruments/stmlib" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/freeverb-main/Components" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/md-drum-synth-main" -O3 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -Wall -ffast-math -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-md-2d-drum-2d-synth-2d-main

clean-md-2d-drum-2d-synth-2d-main:
	-$(RM) ./md-drum-synth-main/FmClapModel.cyclo ./md-drum-synth-main/FmClapModel.d ./md-drum-synth-main/FmClapModel.o ./md-drum-synth-main/FmClapModel.su ./md-drum-synth-main/FmCowbellModel.cyclo ./md-drum-synth-main/FmCowbellModel.d ./md-drum-synth-main/FmCowbellModel.o ./md-drum-synth-main/FmCowbellModel.su ./md-drum-synth-main/FmCymbalModel.cyclo ./md-drum-synth-main/FmCymbalModel.d ./md-drum-synth-main/FmCymbalModel.o ./md-drum-synth-main/FmCymbalModel.su ./md-drum-synth-main/FmKickModel.cyclo ./md-drum-synth-main/FmKickModel.d ./md-drum-synth-main/FmKickModel.o ./md-drum-synth-main/FmKickModel.su ./md-drum-synth-main/FmRimshotModel.cyclo ./md-drum-synth-main/FmRimshotModel.d ./md-drum-synth-main/FmRimshotModel.o ./md-drum-synth-main/FmRimshotModel.su ./md-drum-synth-main/FmSnareModel.cyclo ./md-drum-synth-main/FmSnareModel.d ./md-drum-synth-main/FmSnareModel.o ./md-drum-synth-main/FmSnareModel.su ./md-drum-synth-main/FmTomModel.cyclo ./md-drum-synth-main/FmTomModel.d ./md-drum-synth-main/FmTomModel.o ./md-drum-synth-main/FmTomModel.su ./md-drum-synth-main/TRXBassDrum.cyclo ./md-drum-synth-main/TRXBassDrum.d ./md-drum-synth-main/TRXBassDrum.o ./md-drum-synth-main/TRXBassDrum.su ./md-drum-synth-main/TRXClaves.cyclo ./md-drum-synth-main/TRXClaves.d ./md-drum-synth-main/TRXClaves.o ./md-drum-synth-main/TRXClaves.su ./md-drum-synth-main/TRXHiHat.cyclo ./md-drum-synth-main/TRXHiHat.d ./md-drum-synth-main/TRXHiHat.o ./md-drum-synth-main/TRXHiHat.su ./md-drum-synth-main/TRXSnareDrum.cyclo ./md-drum-synth-main/TRXSnareDrum.d ./md-drum-synth-main/TRXSnareDrum.o ./md-drum-synth-main/TRXSnareDrum.su

.PHONY: clean-md-2d-drum-2d-synth-2d-main

