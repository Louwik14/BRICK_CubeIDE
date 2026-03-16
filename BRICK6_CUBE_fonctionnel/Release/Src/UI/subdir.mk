################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Src/UI/font.c \
../Src/UI/font4x6.c \
../Src/UI/font5x7.c \
../Src/UI/font5x8_elektron.c \
../Src/UI/ui_core.c \
../Src/UI/ui_event.c \
../Src/UI/ui_navigation.c \
../Src/UI/ui_page_manager.c \
../Src/UI/ui_param.c \
../Src/UI/ui_renderer_oled.c \
../Src/UI/ui_tasklet.c 

C_DEPS += \
./Src/UI/font.d \
./Src/UI/font4x6.d \
./Src/UI/font5x7.d \
./Src/UI/font5x8_elektron.d \
./Src/UI/ui_core.d \
./Src/UI/ui_event.d \
./Src/UI/ui_navigation.d \
./Src/UI/ui_page_manager.d \
./Src/UI/ui_param.d \
./Src/UI/ui_renderer_oled.d \
./Src/UI/ui_tasklet.d 

OBJS += \
./Src/UI/font.o \
./Src/UI/font4x6.o \
./Src/UI/font5x7.o \
./Src/UI/font5x8_elektron.o \
./Src/UI/ui_core.o \
./Src/UI/ui_event.o \
./Src/UI/ui_navigation.o \
./Src/UI/ui_page_manager.o \
./Src/UI/ui_param.o \
./Src/UI/ui_renderer_oled.o \
./Src/UI/ui_tasklet.o 


# Each subdirectory must supply rules for building sources it contributes
Src/UI/%.o Src/UI/%.su Src/UI/%.cyclo: ../Src/UI/%.c Src/UI/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/csrc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Drv_app/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Core" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/MIDI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Param" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Storage" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/SD" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/UI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/Third_Party/FatFs/src" -O3 -ffunction-sections -fdata-sections -Wall -fno-math-errno -ffast-math -fsingle-precision-constant -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Src-2f-UI

clean-Src-2f-UI:
	-$(RM) ./Src/UI/font.cyclo ./Src/UI/font.d ./Src/UI/font.o ./Src/UI/font.su ./Src/UI/font4x6.cyclo ./Src/UI/font4x6.d ./Src/UI/font4x6.o ./Src/UI/font4x6.su ./Src/UI/font5x7.cyclo ./Src/UI/font5x7.d ./Src/UI/font5x7.o ./Src/UI/font5x7.su ./Src/UI/font5x8_elektron.cyclo ./Src/UI/font5x8_elektron.d ./Src/UI/font5x8_elektron.o ./Src/UI/font5x8_elektron.su ./Src/UI/ui_core.cyclo ./Src/UI/ui_core.d ./Src/UI/ui_core.o ./Src/UI/ui_core.su ./Src/UI/ui_event.cyclo ./Src/UI/ui_event.d ./Src/UI/ui_event.o ./Src/UI/ui_event.su ./Src/UI/ui_navigation.cyclo ./Src/UI/ui_navigation.d ./Src/UI/ui_navigation.o ./Src/UI/ui_navigation.su ./Src/UI/ui_page_manager.cyclo ./Src/UI/ui_page_manager.d ./Src/UI/ui_page_manager.o ./Src/UI/ui_page_manager.su ./Src/UI/ui_param.cyclo ./Src/UI/ui_param.d ./Src/UI/ui_param.o ./Src/UI/ui_param.su ./Src/UI/ui_renderer_oled.cyclo ./Src/UI/ui_renderer_oled.d ./Src/UI/ui_renderer_oled.o ./Src/UI/ui_renderer_oled.su ./Src/UI/ui_tasklet.cyclo ./Src/UI/ui_tasklet.d ./Src/UI/ui_tasklet.o ./Src/UI/ui_tasklet.su

.PHONY: clean-Src-2f-UI

