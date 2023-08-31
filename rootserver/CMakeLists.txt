cmake_minimum_required(VERSION 3.7.2)

project(sel4service-rootserver C)

find_package(musllibc REQUIRED)
find_package(util_libs REQUIRED)
find_package(seL4_libs REQUIRED)
find_package(sel4_projects_libs REQUIRED)

# Need to disable GC sections as it causes our tests to be stripped sometimes
# This option is tested in the following musllibc_setup_build_environment_with_sel4runtime call.
set(UserLinkerGCSections OFF CACHE BOOL "" FORCE)
# This sets up environment build flags and imports musllibc and runtime libraries.
musllibc_setup_build_environment_with_sel4runtime()
sel4_import_libsel4()
util_libs_import_libraries()
sel4_libs_import_libraries()

# Setting LibNanopb On causes its libraries to be created when sel4_projects_libs_import_libraries
# is called.
set(LibNanopb ON CACHE BOOL "" FORCE)
sel4_projects_libs_import_libraries()

# Build sqlite3 image
add_subdirectory(../sqlite3 sqlite3)

# Build xv6fs server image
add_subdirectory(../xv6fs xv6fs)

# Build ramdisk driver image
add_subdirectory(../ramdisk ramdisk)

# Build CPIO archive
include(cpio)
set(cpio_files "")
list(APPEND cpio_files "$<TARGET_FILE:sqlite3>" "$<TARGET_FILE:xv6fs>" "$<TARGET_FILE:ramdisk>")
MakeCPIO(archive.o "${cpio_files}")

file(
    GLOB
        static
        src/*.c
)

add_executable(sel4service-rootserver EXCLUDE_FROM_ALL ${static} archive.o)
target_link_libraries(
    sel4service-rootserver
    PUBLIC
        sel4_autoconf
        muslc
        sel4
        sel4runtime
        sel4allocman
        sel4vka
        sel4utils
        sel4rpc
        sel4platsupport
        sel4muslcsys
        sel4service
)
target_compile_options(sel4service-rootserver PRIVATE -Werror -g)

# Set this image as the rootserver
include(rootserver)
DeclareRootserver(sel4service-rootserver)