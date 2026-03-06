################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include mutable_instruments/stmlib/utils/subdir.mk
-include mutable_instruments/stmlib/dsp/subdir.mk
-include mutable_instruments/clouds/dsp/pvoc/subdir.mk
-include mutable_instruments/clouds/dsp/subdir.mk
-include mutable_instruments/clouds/subdir.mk
-include Startup/subdir.mk
-include Src/UI/subdir.mk
-include Src/Streaming/subdir.mk
-include Src/Storage/subdir.mk
-include Src/Param/subdir.mk
-include Src/Sampler/subdir.mk
-include Src/MIDI/subdir.mk
-include Src/Core/subdir.mk
-include Src/Audio/subdir.mk
-include Src/App/subdir.mk
-include Src/subdir.mk
-include Middlewares/Third_Party/FatFs/src/option/subdir.mk
-include Middlewares/Third_Party/FatFs/src/subdir.mk
-include Drivers/STM32H7xx_HAL_Driver/Src/subdir.mk
-include Drivers/Drv_app/Src/subdir.mk
-include Drivers/CMSIS_DSP/Source/TransformFunctions/subdir.mk
-include Drivers/CMSIS_DSP/Source/QuaternionMathFunctions/subdir.mk
-include Drivers/CMSIS_DSP/Source/FilteringFunctions/subdir.mk
-include Drivers/CMSIS_DSP/Source/FastMathFunctions/subdir.mk
-include Drivers/CMSIS_DSP/Source/ControllerFunctions/subdir.mk
-include Drivers/CMSIS_DSP/Source/ComplexMathFunctions/subdir.mk
-include Drivers/CMSIS_DSP/Source/CommonTables/subdir.mk
-include Drivers/CMSIS_DSP/Source/BasicMathFunctions/subdir.mk
-include App/usb_stack/subdir.mk
-include App/Middlewares/ST/STM32_USB_Host_Library/Core/Src/subdir.mk
-include App/Middlewares/ST/STM32_USB_Device_Library/Core/Src/subdir.mk
-include App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Src/subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(CC_DEPS)),)
-include $(CC_DEPS)
endif
ifneq ($(strip $(C++_DEPS)),)
-include $(C++_DEPS)
endif
ifneq ($(strip $(CCM_DEPS)),)
-include $(CCM_DEPS)
endif
ifneq ($(strip $(C_UPPER_DEPS)),)
-include $(C_UPPER_DEPS)
endif
ifneq ($(strip $(CXX_DEPS)),)
-include $(CXX_DEPS)
endif
ifneq ($(strip $(S_DEPS)),)
-include $(S_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(CXXM_DEPS)),)
-include $(CXXM_DEPS)
endif
ifneq ($(strip $(C++M_DEPS)),)
-include $(C++M_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
ifneq ($(strip $(CPP_DEPS)),)
-include $(CPP_DEPS)
endif
endif

-include ../makefile.defs

OPTIONAL_TOOL_DEPS := \
$(wildcard ../makefile.defs) \
$(wildcard ../makefile.init) \
$(wildcard ../makefile.targets) \


BUILD_ARTIFACT_NAME := BRICK6_CUBE
BUILD_ARTIFACT_EXTENSION := elf
BUILD_ARTIFACT_PREFIX :=
BUILD_ARTIFACT := $(BUILD_ARTIFACT_PREFIX)$(BUILD_ARTIFACT_NAME)$(if $(BUILD_ARTIFACT_EXTENSION),.$(BUILD_ARTIFACT_EXTENSION),)

# Add inputs and outputs from these tool invocations to the build variables 
EXECUTABLES += \
BRICK6_CUBE.elf \

MAP_FILES += \
BRICK6_CUBE.map \

SIZE_OUTPUT += \
default.size.stdout \

OBJDUMP_LIST += \
BRICK6_CUBE.list \


# All Target
all: main-build

# Main-build Target
main-build: BRICK6_CUBE.elf secondary-outputs

# Tool invocations
BRICK6_CUBE.elf BRICK6_CUBE.map: $(OBJS) $(USER_OBJS) C:\Users\developpeur\Documents\BRICK5_H743_176\BRICK6_CUBE_fonctionnel\STM32H743IITX_FLASH.ld makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-g++ -o "BRICK6_CUBE.elf" @"objects.list" $(USER_OBJS) $(LIBS) -mcpu=cortex-m7 -T"C:\Users\developpeur\Documents\BRICK5_H743_176\BRICK6_CUBE_fonctionnel\STM32H743IITX_FLASH.ld" -Wl,-Map="BRICK6_CUBE.map" -Wl,--gc-sections -static -flto --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -Wl,--start-group -lc -lm -lstdc++ -lsupc++ -Wl,--end-group
	@echo 'Finished building target: $@'
	@echo ' '

default.size.stdout: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-size  $(EXECUTABLES)
	@echo 'Finished building: $@'
	@echo ' '

BRICK6_CUBE.list: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-objdump -h -S $(EXECUTABLES) > "BRICK6_CUBE.list"
	@echo 'Finished building: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) BRICK6_CUBE.elf BRICK6_CUBE.list BRICK6_CUBE.map default.size.stdout
	-@echo ' '

secondary-outputs: $(SIZE_OUTPUT) $(OBJDUMP_LIST)

fail-specified-linker-script-missing:
	@echo 'Error: Cannot find the specified linker script. Check the linker settings in the build configuration.'
	@exit 2

warn-no-linker-script-specified:
	@echo 'Warning: No linker script specified. Check the linker settings in the build configuration.'

.PHONY: all clean dependents main-build fail-specified-linker-script-missing warn-no-linker-script-specified

-include ../makefile.targets
