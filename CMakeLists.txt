cmake_minimum_required(VERSION 3.7.2)

project(sel4service C CXX ASM)

set(project_dir "${CMAKE_CURRENT_LIST_DIR}/../../")
file(GLOB project_modules ${project_dir}/projects/*)
list(
    APPEND
        CMAKE_MODULE_PATH
        ${project_dir}/kernel
        ${project_dir}/tools/seL4/cmake-tool/helpers/
        ${project_dir}/tools/seL4/elfloader-tool/
        ${project_modules}
)

set(NANOPB_SRC_ROOT_FOLDER "${project_dir}/tools/nanopb" CACHE INTERNAL "")
set(OPENSBI_PATH "${project_dir}/tools/opensbi" CACHE STRING "OpenSBI Folder location")

include(application_settings)
include(${CMAKE_CURRENT_LIST_DIR}/easy-settings.cmake)

# Import seL4 kernel
find_package(seL4 REQUIRED)
sel4_import_kernel()

# Platform check an setup
correct_platform_strings()
sel4_configure_platform_settings()
set(valid_platforms ${KernelPlatform_all_strings} ${correct_platform_strings_platform_aliases})
set_property(CACHE PLATFORM PROPERTY STRINGS ${valid_platforms})
if(NOT "${PLATFORM}" IN_LIST valid_platforms)
    message(FATAL_ERROR "Invalid PLATFORM selected: \"${PLATFORM}\"
        Valid platforms are: \"${valid_platforms}\"")
endif()

# Elfloader settings that correspond to how Data61 sets its boards up.
find_package(elfloader-tool REQUIRED)
ApplyData61ElfLoaderSettings(${KernelPlatform} ${KernelSel4Arch})
elfloader_import_project()

# Set the roottask CNode size to be large enough for DTB, timer caps, etc
# Note that this works for the platforms that we have at the moment, and may
# need to be increased in the future
set(KernelRootCNodeSizeBits 13 CACHE INTERNAL "")

ApplyCommonReleaseVerificationSettings(${RELEASE} ${VERIFICATION})

if(SMP)
    if(NUM_NODES MATCHES "^[0-9]+$")
        set(KernelMaxNumNodes ${NUM_NODES} CACHE STRING "" FORCE)
    else()
        set(KernelMaxNumNodes 4 CACHE STRING "" FORCE)
    endif()
else()
    set(KernelMaxNumNodes 1 CACHE STRING "" FORCE)
endif()

# add_subdirectory(apps/sel4test-driver)

if(SIMULATION)
    include(simulation)
    # Set QEMU memory size
    SetSimulationScriptProperty(MEM_SIZE "2G")
    GenerateSimulateScript()
endif()
