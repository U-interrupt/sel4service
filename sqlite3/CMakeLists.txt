cmake_minimum_required(VERSION 3.7.2)

project(sqlite3 C)

file(GLOB src src/*.c)
list(FILTER src EXCLUDE REGEX "shell.c")

add_executable(sqlite3 EXCLUDE_FROM_ALL ${src})
target_link_libraries(
    sqlite3
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
)
