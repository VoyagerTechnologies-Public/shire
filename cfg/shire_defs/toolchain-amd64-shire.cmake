# This example toolchain file describes the cross compiler to use for
# the target architecture indicated in the configuration file.

# Basic cross system configuration
SET(CMAKE_SYSTEM_NAME           Linux)
SET(CMAKE_SYSTEM_VERSION        1)
SET(CMAKE_SYSTEM_PROCESSOR      amd64)

# Specify the cross compiler executables
# Typically these would be installed in a home directory or somewhere
# in /opt.  However in this example the system compiler is used.
SET(CMAKE_C_COMPILER            "/usr/bin/gcc")
SET(CMAKE_CXX_COMPILER          "/usr/bin/g++")

# Configure the find commands
SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM   NEVER)
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY   NEVER)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE   NEVER)

# These variable settings are specific to cFE/OSAL and determines which 
# abstraction layers are built when using this toolchain
SET(CFE_SYSTEM_PSPNAME      "shire")
SET(OSAL_SYSTEM_BSPTYPE     "shire-linux")
SET(OSAL_SYSTEM_OSTYPE      "shire")

# Enable deterministic Software Bus delivery accounting only for the SHIRE
# simulation target. Generic and physical-flight targets compile the observer
# call sites out entirely.
SET(CFE_SB_OBSERVER_SOURCE
    "${CMAKE_SOURCE_DIR}/../psp/fsw/shire/src/cfe_psp_sb_observer.c")
SET(CFE_SB_OBSERVER_INCLUDE_DIRS
    "${CMAKE_SOURCE_DIR}/../psp/fsw/shire/inc"
    "${CMAKE_SOURCE_DIR}/../../simulith/include")

# Replace SCH's normal OSAL-timer backend with the SHIRE synchronized clock
# and completion barrier. Physical targets do not set these variables and use
# the portable default implementation from the SCH application.
SET(SCH_CUSTOM_PLATFORM_SOURCE
    "${CMAKE_SOURCE_DIR}/../../cfg/shire_defs/sch/sch_custom_shire.c")
SET(SCH_CUSTOM_PLATFORM_INCLUDE_DIRS
    "${CMAKE_SOURCE_DIR}/../psp/fsw/shire/inc")
