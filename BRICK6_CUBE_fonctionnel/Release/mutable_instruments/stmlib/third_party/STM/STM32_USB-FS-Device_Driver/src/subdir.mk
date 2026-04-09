################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_cal.c \
../mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_dev.c \
../mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_int.c \
../mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_pcd.c \
../mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_core.c \
../mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_init.c \
../mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_int.c \
../mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_mem.c \
../mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_regs.c \
../mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_sil.c 

C_DEPS += \
./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_cal.d \
./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_dev.d \
./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_int.d \
./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_pcd.d \
./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_core.d \
./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_init.d \
./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_int.d \
./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_mem.d \
./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_regs.d \
./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_sil.d 

OBJS += \
./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_cal.o \
./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_dev.o \
./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_int.o \
./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_pcd.o \
./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_core.o \
./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_init.o \
./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_int.o \
./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_mem.o \
./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_regs.o \
./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_sil.o 


# Each subdirectory must supply rules for building sources it contributes
mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/%.o mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/%.su mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/%.cyclo: ../mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/%.c mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/Drv_app/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Audio" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Core" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/MIDI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Param" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/Storage" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/UI" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Inc/SD" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/U8g2" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/Include" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/Drivers/CMSIS_DSP/PrivateInclude" -I../Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/tinyusb" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/usb_stack" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Host_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Core/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc" -I"C:/Users/developpeur/Documents/BRICK5_H743_176/BRICK6_CUBE_fonctionnel/App/Middlewares/Third_Party/FatFs/src" -O3 -ffunction-sections -fdata-sections -Wall -fno-math-errno -ffast-math -fsingle-precision-constant -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-mutable_instruments-2f-stmlib-2f-third_party-2f-STM-2f-STM32_USB-2d-FS-2d-Device_Driver-2f-src

clean-mutable_instruments-2f-stmlib-2f-third_party-2f-STM-2f-STM32_USB-2d-FS-2d-Device_Driver-2f-src:
	-$(RM) ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_cal.cyclo ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_cal.d ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_cal.o ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_cal.su ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_dev.cyclo ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_dev.d ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_dev.o ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_dev.su ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_int.cyclo ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_int.d ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_int.o ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_int.su ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_pcd.cyclo ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_pcd.d ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_pcd.o ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/otgd_fs_pcd.su ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_core.cyclo ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_core.d ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_core.o ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_core.su ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_init.cyclo ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_init.d ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_init.o ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_init.su ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_int.cyclo ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_int.d ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_int.o ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_int.su ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_mem.cyclo ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_mem.d ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_mem.o ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_mem.su ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_regs.cyclo ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_regs.d ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_regs.o ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_regs.su ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_sil.cyclo ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_sil.d ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_sil.o ./mutable_instruments/stmlib/third_party/STM/STM32_USB-FS-Device_Driver/src/usb_sil.su

.PHONY: clean-mutable_instruments-2f-stmlib-2f-third_party-2f-STM-2f-STM32_USB-2d-FS-2d-Device_Driver-2f-src

