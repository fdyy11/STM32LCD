# Add sources to executable/library
target_sources(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/syscall.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/sysmem.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/main.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/startup_stm32f103xx.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/lcd_st7789.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/SYSTEM/usart/usart3.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/HARDWARE/OLED/chinese_font.c"
)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/stm32f103xc_flash.ld" "${CMAKE_CURRENT_BINARY_DIR}" COPYONLY)

set_target_properties(${PROJECT_NAME} PROPERTIES LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/stm32f103xc_flash.ld")
