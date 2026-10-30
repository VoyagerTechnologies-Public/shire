# Shared synchronized runtime contract for host and remote Linux processors.
set(CFE_SYSTEM_PSPNAME "shire")
set(OSAL_SYSTEM_BSPTYPE "shire-linux")
set(OSAL_SYSTEM_OSTYPE "shire")
set(CFE_SB_OBSERVER_SOURCE
    "${CMAKE_SOURCE_DIR}/../psp/fsw/shire/src/cfe_psp_sb_observer.c")
set(CFE_SB_OBSERVER_INCLUDE_DIRS
    "${CMAKE_SOURCE_DIR}/../psp/fsw/shire/inc"
    "${CMAKE_SOURCE_DIR}/../../simulith/include")
set(SCH_CUSTOM_PLATFORM_SOURCE
    "${CMAKE_SOURCE_DIR}/../../cfg/shire_defs/sch/sch_custom_shire.c")
set(SCH_CUSTOM_PLATFORM_INCLUDE_DIRS
    "${CMAKE_SOURCE_DIR}/../psp/fsw/shire/inc")
