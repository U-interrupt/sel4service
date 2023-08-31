cmake_minimum_required(VERSION 3.7.2)

include(settings.cmake)

project(sel4service C CXX ASM)

find_package(seL4 REQUIRED)
sel4_import_kernel()

# Elfloader settings that correspond to how Data61 sets its boards up.
find_package(elfloader-tool REQUIRED)
ApplyData61ElfLoaderSettings(${KernelPlatform} ${KernelSel4Arch})
elfloader_import_project()

# Set the roottask CNode size to be large enough for DTB, timer caps, etc
# Note that this works for the platforms that we have at the moment, and may
# need to be increased in the future
set(KernelRootCNodeSizeBits 13 CACHE INTERNAL "")

add_subdirectory(rootserver)

if(SIMULATION)
    include(simulation)
    # Set QEMU memory size
    SetSimulationScriptProperty(MEM_SIZE "2G")
    GenerateSimulateScript()
endif()
