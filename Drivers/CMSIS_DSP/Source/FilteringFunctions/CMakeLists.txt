cmake_minimum_required(VERSION 3.14)

project(CMSISDSPTransform)

include(configLib)
include(configDsp)

add_library(CMSISDSPTransform STATIC)
configLib(CMSISDSPTransform ${ROOT})
configDsp(CMSISDSPTransform ${ROOT})

include(fft)
fft(CMSISDSPTransform)

if (CONFIGTABLE AND ALLFFT)
    target_compile_definitions(CMSISDSPTransform PUBLIC ARM_ALL_FFT_TABLES)
endif()

# Minimal transform set kept for this project:
# - arm_rfft_fast_f32.c
# - arm_rfft_fast_init_f32.c
#
# These two are only needed if the ARM FFT path is enabled/used.
# They depend on the CFFT float32 implementation below.

target_sources(CMSISDSPTransform PRIVATE arm_rfft_fast_f32.c)
target_sources(CMSISDSPTransform PRIVATE arm_rfft_fast_init_f32.c)
target_sources(CMSISDSPTransform PRIVATE arm_cfft_f32.c)
target_sources(CMSISDSPTransform PRIVATE arm_cfft_init_f32.c)
target_sources(CMSISDSPTransform PRIVATE arm_cfft_radix8_f32.c)

### Includes
target_include_directories(CMSISDSPTransform PUBLIC "${DSP}/Include")