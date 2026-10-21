# The SDK is built alongside the QEMU guest. Never borrow host headers/libraries.
if(NOT DEFINED ENV{SHIRE_ARM_SDK})
    message(FATAL_ERROR "SHIRE_ARM_SDK must name the Buildroot output/host directory")
endif()
set(CMAKE_SYSTEM_NAME Linux)
include("$ENV{SHIRE_ARM_SDK}/share/buildroot/toolchainfile.cmake")
# Component flags provide the project's fortify level; avoid a conflicting SDK
# default while retaining the SDK's ABI, large-file, and sysroot settings.
string(REPLACE "-D_FORTIFY_SOURCE=1" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
include("${CMAKE_CURRENT_LIST_DIR}/shire_simulation.cmake")
