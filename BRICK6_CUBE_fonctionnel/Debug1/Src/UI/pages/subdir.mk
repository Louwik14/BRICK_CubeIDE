################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Src/UI/pages/ui_page_calibration.c \
../Src/UI/pages/ui_page_debug_hall.c \
../Src/UI/pages/ui_page_param_test.c \
../Src/UI/pages/ui_page_template_arp.c \
../Src/UI/pages/ui_page_template_cfg.c \
../Src/UI/pages/ui_page_template_dx7.c \
../Src/UI/pages/ui_page_template_filter.c \
../Src/UI/pages/ui_page_template_keyboard.c \
../Src/UI/pages/ui_page_template_mix.c \
../Src/UI/pages/ui_page_template_play.c \
../Src/UI/pages/ui_page_template_seq.c 

C_DEPS += \
./Src/UI/pages/ui_page_calibration.d \
./Src/UI/pages/ui_page_debug_hall.d \
./Src/UI/pages/ui_page_param_test.d \
./Src/UI/pages/ui_page_template_arp.d \
./Src/UI/pages/ui_page_template_cfg.d \
./Src/UI/pages/ui_page_template_dx7.d \
./Src/UI/pages/ui_page_template_filter.d \
./Src/UI/pages/ui_page_template_keyboard.d \
./Src/UI/pages/ui_page_template_mix.d \
./Src/UI/pages/ui_page_template_play.d \
./Src/UI/pages/ui_page_template_seq.d 

OBJS += \
./Src/UI/pages/ui_page_calibration.o \
./Src/UI/pages/ui_page_debug_hall.o \
./Src/UI/pages/ui_page_param_test.o \
./Src/UI/pages/ui_page_template_arp.o \
./Src/UI/pages/ui_page_template_cfg.o \
./Src/UI/pages/ui_page_template_dx7.o \
./Src/UI/pages/ui_page_template_filter.o \
./Src/UI/pages/ui_page_template_keyboard.o \
./Src/UI/pages/ui_page_template_mix.o \
./Src/UI/pages/ui_page_template_play.o \
./Src/UI/pages/ui_page_template_seq.o 


# Each subdirectory must supply rules for building sources it contributes
Src/UI/pages/%.o Src/UI/pages/%.su Src/UI/pages/%.cyclo: ../Src/UI/pages/%.c Src/UI/pages/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Drv_app/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Core" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/MIDI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Param" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Storage" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/UI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/SD" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/U8g2" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/Third_Party/FatFs/src" -Og -ffunction-sections -fdata-sections -Wall -fno-math-errno -fsingle-precision-constant -ffast-math -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Src-2f-UI-2f-pages

clean-Src-2f-UI-2f-pages:
	-$(RM) ./Src/UI/pages/ui_page_calibration.cyclo ./Src/UI/pages/ui_page_calibration.d ./Src/UI/pages/ui_page_calibration.o ./Src/UI/pages/ui_page_calibration.su ./Src/UI/pages/ui_page_debug_hall.cyclo ./Src/UI/pages/ui_page_debug_hall.d ./Src/UI/pages/ui_page_debug_hall.o ./Src/UI/pages/ui_page_debug_hall.su ./Src/UI/pages/ui_page_param_test.cyclo ./Src/UI/pages/ui_page_param_test.d ./Src/UI/pages/ui_page_param_test.o ./Src/UI/pages/ui_page_param_test.su ./Src/UI/pages/ui_page_template_arp.cyclo ./Src/UI/pages/ui_page_template_arp.d ./Src/UI/pages/ui_page_template_arp.o ./Src/UI/pages/ui_page_template_arp.su ./Src/UI/pages/ui_page_template_cfg.cyclo ./Src/UI/pages/ui_page_template_cfg.d ./Src/UI/pages/ui_page_template_cfg.o ./Src/UI/pages/ui_page_template_cfg.su ./Src/UI/pages/ui_page_template_dx7.cyclo ./Src/UI/pages/ui_page_template_dx7.d ./Src/UI/pages/ui_page_template_dx7.o ./Src/UI/pages/ui_page_template_dx7.su ./Src/UI/pages/ui_page_template_filter.cyclo ./Src/UI/pages/ui_page_template_filter.d ./Src/UI/pages/ui_page_template_filter.o ./Src/UI/pages/ui_page_template_filter.su ./Src/UI/pages/ui_page_template_keyboard.cyclo ./Src/UI/pages/ui_page_template_keyboard.d ./Src/UI/pages/ui_page_template_keyboard.o ./Src/UI/pages/ui_page_template_keyboard.su ./Src/UI/pages/ui_page_template_mix.cyclo ./Src/UI/pages/ui_page_template_mix.d ./Src/UI/pages/ui_page_template_mix.o ./Src/UI/pages/ui_page_template_mix.su ./Src/UI/pages/ui_page_template_play.cyclo ./Src/UI/pages/ui_page_template_play.d ./Src/UI/pages/ui_page_template_play.o ./Src/UI/pages/ui_page_template_play.su ./Src/UI/pages/ui_page_template_seq.cyclo ./Src/UI/pages/ui_page_template_seq.d ./Src/UI/pages/ui_page_template_seq.o ./Src/UI/pages/ui_page_template_seq.su

.PHONY: clean-Src-2f-UI-2f-pages

