# Set the minimum version of CMake that can be used
cmake_minimum_required(VERSION 3.20)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

# Set target definition variables required by the toolchain file
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/target.cmake)

# Set the toolchain file for cross-compilation
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/gcc-arm-none-eabi.cmake)

# Project definition
project(stm32_1)

# Enable CMake support for ASM and C/CPP languages
enable_language(C ASM)

set(PROJECT_NAME stm32_1)

# Create an executable object type
add_executable(${PROJECT_NAME})

# Include additional CMake files for flags, components, and files
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/flags.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/components.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/files.cmake)

# Add sources to executable/library
target_sources(${PROJECT_NAME} PRIVATE
  # User defined sources
)

# Add include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/SYSTEM/usart"
    "${CMAKE_CURRENT_SOURCE_DIR}/SYSTEM/sys"
    "${CMAKE_CURRENT_SOURCE_DIR}/SYSTEM/delay"
    "${CMAKE_CURRENT_SOURCE_DIR}/HARDWARE/OLED"
)

# Compiler options
target_compile_options(${PROJECT_NAME} PRIVATE
  # User defined compiler options
)

# Add additional defined symbols
target_compile_definitions(${PROJECT_NAME} PRIVATE
  # STM32F103RCT6 is a High-Density device (256KB Flash)
  STM32F10X_HD
  # USE_STDPERIPH_DRIVER removed - using direct register access only
)

# Linker options
target_link_options(${PROJECT_NAME} PRIVATE 
  # User defined linker options
)

# Add additional linker search directories
target_link_directories(${PROJECT_NAME} PRIVATE
  # Add additional linker search directories here
)

# Add additional linker libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
  # Add additional linker libraries here
)

# ============================================================================
# Post-build: Generate HEX and BIN files from ELF
# ============================================================================
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${PROJECT_NAME}> ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}> ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}>
    COMMENT "Generating HEX and BIN files..."
    VERBATIM
)

# ============================================================================
# Custom target: Flash firmware via serial (USB-TTL)
# ============================================================================
# Note: Modify COM port and baud rate according to your setup
set(STM32_SERIAL_PORT "COM4" CACHE STRING "Serial port for flashing")
set(STM32_BAUD_RATE "115200" CACHE STRING "Baud rate for flashing")

find_program(STM32FLASH_EXECUTABLE stm32flash)

if(STM32FLASH_EXECUTABLE)
    add_custom_target(flash
        COMMAND ${STM32FLASH_EXECUTABLE} -w ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin -v -g 0x0 -b ${STM32_BAUD_RATE} ${STM32_SERIAL_PORT}
        DEPENDS ${PROJECT_NAME}
        COMMENT "Flashing firmware to STM32 via ${STM32_SERIAL_PORT} at ${STM32_BAUD_RATE} baud..."
        VERBATIM
    )
    
    add_custom_target(flash_hex
        COMMAND ${STM32FLASH_EXECUTABLE} -w ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.hex -v -g 0x0 -b ${STM32_BAUD_RATE} ${STM32_SERIAL_PORT}
        DEPENDS ${PROJECT_NAME}
        COMMENT "Flashing HEX firmware to STM32 via ${STM32_SERIAL_PORT} at ${STM32_BAUD_RATE} baud..."
        VERBATIM
    )
else()
    message(WARNING "stm32flash not found. Flash targets will not be available.")
    message(WARNING "Please install stm32flash or use the PowerShell script: flash_stm32.ps1")
endif()

# ============================================================================
# Custom target: Flash firmware via ST-Link (OpenOCD)
# ============================================================================
find_program(OPENOCD_EXECUTABLE openocd)

if(OPENOCD_EXECUTABLE)
    add_custom_target(flash_stlink
        COMMAND ${OPENOCD_EXECUTABLE} -f interface/stlink.cfg 
                -f target/stm32f1x.cfg 
                -c "program ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.elf verify reset exit"
        DEPENDS ${PROJECT_NAME}
        COMMENT "Flashing via ST-Link using OpenOCD..."
        VERBATIM
    )
    
    add_custom_target(flash_stlink_bin
        COMMAND ${OPENOCD_EXECUTABLE} -f interface/stlink.cfg 
                -f target/stm32f1x.cfg 
                -c "program ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin 0x08000000 verify reset exit"
        DEPENDS ${PROJECT_NAME}
        COMMENT "Flashing BIN via ST-Link using OpenOCD..."
        VERBATIM
    )
else()
    message(WARNING "OpenOCD not found. ST-Link flash targets will not be available.")
    message(WARNING "Please install OpenOCD or use the PowerShell script: flash_stlink.ps1")
endif()

# ============================================================================
# Custom target: Flash firmware via ST-Link (st-flash - faster)
# ============================================================================
find_program(STFLASH_EXECUTABLE st-flash)

if(STFLASH_EXECUTABLE)
    add_custom_target(flash_stlink_fast
        COMMAND ${STFLASH_EXECUTABLE} write ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin 0x08000000
        DEPENDS ${PROJECT_NAME}
        COMMENT "Flashing via ST-Link using st-flash (fast mode)..."
        VERBATIM
    )
else()
    message(WARNING "st-flash not found. Fast ST-Link flash target will not be available.")
endif()

# ============================================================================
# Clean additional generated files
# ============================================================================
set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES 
    "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.hex"
    "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin"
)
