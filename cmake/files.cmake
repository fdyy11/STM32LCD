# Add sources to executable/library
target_sources(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/System/syscall.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/System/sysmem.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Modules/main.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Modules/battery_rs485.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Modules/battery_pages.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Modules/wifi_uart4.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/System/startup_stm32f103xx.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/System/system_stm32f10x.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/System/stm32f10x_it.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Modules/lcd_st7789.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Modules/chinese_font.c"
)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/System/stm32f103xc_flash.ld" "${CMAKE_CURRENT_BINARY_DIR}" COPYONLY)

set_target_properties(${PROJECT_NAME} PROPERTIES LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/stm32f103xc_flash.ld")
