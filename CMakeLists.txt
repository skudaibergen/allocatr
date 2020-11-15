cmake_minimum_required(VERSION 3.15)
project(allocatr C)

set(CMAKE_C_STANDARD 99)

if ((UNIX AND ${CMAKE_SYSTEM_NAME} STREQUAL "Linux") OR
(APPLE AND ${CMAKE_SYSTEM_NAME} MATCHES "Darwin"))
    set(MEM_CORE src/linux_core.c)
elseif(WIN32)
    set(MEM_CORE src/win32_core.c)
endif()

add_library(allocatr STATIC
        src/allocatr.h
        src/allocatr.c
        src/oscore.h
        ${MEM_CORE})

# test runner
add_executable(allocatr_test
        test/allocatr_test.c)

target_link_libraries(allocatr_test allocatr)