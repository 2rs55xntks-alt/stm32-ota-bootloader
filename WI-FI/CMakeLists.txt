cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME WI-FI)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
# 注意：APP 下的源文件全部由下面的 GLOB_RECURSE 自动收集（含 app/svc/drv/bsp 子目录），
#       这里不要再写死 APP/*.c 的路径，否则文件一挪位置构建就失败
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# User application directory (APP/)
set(APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/APP)

# Collect all user sources (supports sub-folders inside APP/)
file(GLOB_RECURSE APP_Sources CONFIGURE_DEPENDS
    ${APP_DIR}/*.c
    ${APP_DIR}/*.s
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    ${APP_Sources}
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    ${APP_DIR}
    ${APP_DIR}/app      # 业务层：task.c / app_config.h
    ${APP_DIR}/svc      # 服务层：OTA.c / fw_meta.c
    ${APP_DIR}/drv      # 驱动层：esp32.c（AT）/ wifi.c（链路）
    ${APP_DIR}/bsp      # 板级：boot_jump.c / printf_uart.c
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# 关键：禁止 GCC 把 printf("纯字符串\r\n") 自动改写成 puts()！
# 否则这部分 printf 会绕开 APP/printf_uart.c，改走
# newlib: puts -> _write -> __io_putchar，打到另一个串口上去（两条输出通道）
# -Os：App 区只有 20KB，-O0 已经装不下（溢出 492 字节），改成 -Os 压体积
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -fno-builtin-printf -Os)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# 生成可直接烧录/交给 bootloader 的 bin 镜像（链接地址 0x08005000）
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    COMMENT "Generating ${CMAKE_PROJECT_NAME}.bin (app @ 0x08005000)"
)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
