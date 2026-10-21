# A matching AArch64 Buildroot SDK/userspace must be supplied by the board profile.
# This selects the synchronized Linux runtime, not a board bootloader or BSP.
if(NOT DEFINED ENV{SHIRE_ARM_SDK})
    message(FATAL_ERROR "SHIRE_ARM_SDK must name the matching AArch64 Buildroot SDK directory")
endif()
set(CMAKE_SYSTEM_NAME Linux)
include("$ENV{SHIRE_ARM_SDK}/share/buildroot/toolchainfile.cmake")
string(REPLACE "-D_FORTIFY_SOURCE=1" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
include("${CMAKE_CURRENT_LIST_DIR}/shire_simulation.cmake")
