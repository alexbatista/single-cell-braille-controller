add_library(tmc2209 STATIC
    tmc2209.c
    tmc2209_stm32.c
)

target_include_directories(tmc2209 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Select the CMSIS device header used by the STM32 port. PUBLIC so that
# consumers including tmc2209_stm32.h get the same device header.
target_compile_definitions(tmc2209 PUBLIC
    TMC2209_STM32_DEVICE_HEADER="stm32f1xx.h"
)

# Interface target from cmake/stm32cubemx providing the CMSIS include paths
# and the STM32F103xB device define.
target_link_libraries(tmc2209 PRIVATE stm32cubemx)
