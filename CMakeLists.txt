cmake_minimum_required(VERSION 3.15)
project(allocatr C)

set(CMAKE_C_STANDARD 99)

add_executable(allocatr src/allocatr.h src/allocatr.c src/memcore.h src/linux_memcore.c src/win32_memcore.c)