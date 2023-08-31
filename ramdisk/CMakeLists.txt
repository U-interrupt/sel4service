cmake_minimum_required(VERSION 3.7.2)

project(ramdisk C)

file(GLOB src src/*.c)

add_executable(ramdisk EXCLUDE_FROM_ALL ${src})
target_link_libraries(
    ramdisk
    PUBLIC
        sel4_autoconf
        muslc
        sel4
        sel4runtime
        sel4allocman
        sel4vka
        sel4utils
        sel4rpc
        sel4sync
        sel4muslcsys
        sel4service
)
