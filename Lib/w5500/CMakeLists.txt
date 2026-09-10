add_library(w5500 STATIC
    ioLibrary/Ethernet/wizchip_conf.c
    ioLibrary/Ethernet/socket.c
    ioLibrary/Ethernet/W5500/w5500.c
    w5500_stm32.c
)

target_include_directories(w5500 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ioLibrary/Ethernet
    ${CMAKE_CURRENT_SOURCE_DIR}/ioLibrary/Ethernet/W5500
)

# Chip and SPI framing selection. PUBLIC so a consumer including
# wizchip_conf.h sees the same chip as the library was built for -- a mismatch
# here silently changes register offsets. Both macros are #ifndef-guarded in
# the vendored wizchip_conf.h, so defining them here pre-empts the header's
# own default without needing to touch the vendored source.
target_compile_definitions(w5500 PUBLIC
    _WIZCHIP_=W5500
    _WIZCHIP_IO_MODE_=_WIZCHIP_IO_MODE_SPI_VDM_
)

# The STM32 port needs the App config header for its reset timings, and
# Core/Inc for the ETH_* pin macros.
target_include_directories(w5500 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../App/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc
)

# Interface target from cmake/stm32cubemx providing the CMSIS/HAL include
# paths and the STM32F103xB device define.
target_link_libraries(w5500 PRIVATE stm32cubemx)

# Vendored third-party code: do not let its warnings fail our build, but do
# not silence warnings in our own port either.
set_source_files_properties(
    ioLibrary/Ethernet/wizchip_conf.c
    ioLibrary/Ethernet/socket.c
    ioLibrary/Ethernet/W5500/w5500.c
    PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-sign-compare"
)
