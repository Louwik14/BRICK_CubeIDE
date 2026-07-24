set(CMAKE_SYSTEM_NAME               Generic)
set(CMAKE_SYSTEM_PROCESSOR          arm)

set(CMAKE_C_COMPILER_ID GNU)
set(CMAKE_CXX_COMPILER_ID GNU)

# Force CubeIDE GCC 13.3.rel1
set(TOOLCHAIN_BIN "C:/ST/STM32CubeIDE_2.0.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.100.202509120712/tools/bin")
set(TOOLCHAIN_PREFIX                "${TOOLCHAIN_BIN}/arm-none-eabi-")

set(CMAKE_C_COMPILER                "${TOOLCHAIN_PREFIX}gcc.exe")
set(CMAKE_ASM_COMPILER              "${TOOLCHAIN_PREFIX}gcc.exe")
set(CMAKE_CXX_COMPILER              "${TOOLCHAIN_PREFIX}g++.exe")
set(CMAKE_LINKER                    "${TOOLCHAIN_PREFIX}g++.exe")
set(CMAKE_AR                        "${TOOLCHAIN_PREFIX}ar.exe")
set(CMAKE_OBJCOPY                   "${TOOLCHAIN_PREFIX}objcopy.exe")
set(CMAKE_OBJDUMP                   "${TOOLCHAIN_PREFIX}objdump.exe")
set(CMAKE_SIZE                      "${TOOLCHAIN_PREFIX}size.exe")

set(CMAKE_EXECUTABLE_SUFFIX_ASM     ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C       ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX     ".elf")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# MCU specific flags
set(TARGET_FLAGS "-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard")

set(CMAKE_C_FLAGS                   "${CMAKE_C_FLAGS} ${TARGET_FLAGS}")
set(CMAKE_ASM_FLAGS                 "${CMAKE_C_FLAGS} -x assembler-with-cpp -MMD -MP")
set(CMAKE_C_FLAGS                   "${CMAKE_C_FLAGS} -Wall -fdata-sections -ffunction-sections")

set(CMAKE_C_FLAGS_DEBUG             "-Og -g3")
set(CMAKE_C_FLAGS_RELEASE           "-Os -g0")
set(CMAKE_CXX_FLAGS_DEBUG           "-Og -g3")
set(CMAKE_CXX_FLAGS_RELEASE         "-Os -g0")

set(CMAKE_CXX_FLAGS                 "${CMAKE_C_FLAGS} -fno-rtti -fno-exceptions -fno-threadsafe-statics")

set(CMAKE_EXE_LINKER_FLAGS          "${TARGET_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS          "${CMAKE_EXE_LINKER_FLAGS} -Wl,--print-memory-usage")

set(TOOLCHAIN_LINK_LIBRARIES        "m")
