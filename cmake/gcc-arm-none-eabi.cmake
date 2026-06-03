set(CMAKE_SYSTEM_NAME               Generic)
set(CMAKE_SYSTEM_PROCESSOR          arm)

set(CMAKE_C_COMPILER_ID GNU)
set(CMAKE_CXX_COMPILER_ID GNU)

# Prefer a complete STM32/xPack toolchain when available. The Homebrew
# arm-none-eabi-gcc formula is built without target headers, which breaks
# normal STM32 builds that need newlib/sysroot files.
set(TOOLCHAIN_PREFIX                arm-none-eabi-)
set(_toolchain_bin_dir "")

if(DEFINED ENV{CUBE_BUNDLE_PATH})
    set(_cube_toolchain_bin "$ENV{CUBE_BUNDLE_PATH}/gnu-tools-for-stm32/13.3.1+st.9/bin")
    if(EXISTS "${_cube_toolchain_bin}/arm-none-eabi-gcc")
        set(_toolchain_bin_dir "${_cube_toolchain_bin}")
    endif()
endif()

if(NOT _toolchain_bin_dir AND DEFINED ENV{HOME})
    # Probe xPack install locations for macOS (~/Library/xPacks) and Linux (~/.local/xPacks, ~/xPacks)
    foreach(_xpack_base
            "$ENV{HOME}/Library/xPacks"
            "$ENV{HOME}/.local/xPacks"
            "$ENV{HOME}/xPacks")
        file(GLOB _xpack_toolchains "${_xpack_base}/@xpack-dev-tools/arm-none-eabi-gcc/*/.content/bin/arm-none-eabi-gcc")
        if(_xpack_toolchains)
            break()
        endif()
    endforeach()
    if(_xpack_toolchains)
        list(SORT _xpack_toolchains COMPARE NATURAL ORDER DESCENDING)
        list(GET _xpack_toolchains 0 _xpack_gcc)
        get_filename_component(_toolchain_bin_dir "${_xpack_gcc}" DIRECTORY)
    endif()
endif()

if(_toolchain_bin_dir)
    set(TOOLCHAIN_PREFIX "${_toolchain_bin_dir}/arm-none-eabi-")
endif()

set(CMAKE_C_COMPILER                ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_ASM_COMPILER              ${CMAKE_C_COMPILER})
set(CMAKE_CXX_COMPILER              ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_LINKER                    ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_OBJCOPY                   ${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_SIZE                      ${TOOLCHAIN_PREFIX}size)

set(CMAKE_EXECUTABLE_SUFFIX_ASM     ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C       ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX     ".elf")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# MCU specific flags
set(TARGET_FLAGS "-mcpu=cortex-m3 ")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${TARGET_FLAGS}")
set(CMAKE_ASM_FLAGS "${CMAKE_C_FLAGS} -x assembler-with-cpp -MMD -MP")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -fdata-sections -ffunction-sections -fstack-usage")

# The cyclomatic-complexity parameter must be defined for the Cyclomatic complexity feature in STM32CubeIDE to work.
# However, most GCC toolchains do not support this option, which causes a compilation error; for this reason, the feature is disabled by default.
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fcyclomatic-complexity")

set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_C_FLAGS_RELEASE "-Os -g0")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -g0")

set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fno-rtti -fno-exceptions -fno-threadsafe-statics")

set(CMAKE_EXE_LINKER_FLAGS "${TARGET_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -T \"${CMAKE_SOURCE_DIR}/STM32F103XX_FLASH.ld\"")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --specs=nano.specs")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-Map=${CMAKE_PROJECT_NAME}.map -Wl,--gc-sections")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--print-memory-usage")
set(TOOLCHAIN_LINK_LIBRARIES "m")
