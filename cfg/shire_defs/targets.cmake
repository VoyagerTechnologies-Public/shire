######################################################################
#
# Master config file for cFS target boards
#
######################################################################

# The MISSION_NAME will be compiled into the target build data structure
# as well as being passed to "git describe" to filter the tags when building
# the version string.
SET(MISSION_NAME "SHIRE")

# MDG dedication. RIP - September 16, 2023.
SET(SPACECRAFT_ID 0x17)

# The "MISSION_GLOBAL_APPLIST" is a set of apps/libs that will be built
# for every defined target.  These are built as dynamic modules
# and must be loaded explicitly via startup script or command.
# This list is effectively appended to every TGTx_APPLIST in targets.cmake.
# Example:
list(APPEND MISSION_GLOBAL_APPLIST
    #
    # Libraries
    #
        #cryptolib
        io_lib

    #
    # cFS Apps
    #
        cf
        ci_lab
        ds
        fm
        lc
        sc
        sch
        to_lab

    #
    # Components
    #
        adcs
        demo
        eps
        radio
)

# Create Application Platform Include List
FOREACH(X ${MISSION_GLOBAL_APPLIST})
    LIST(APPEND APPLICATION_PLATFORM_INC_LIST ${${X}_MISSION_DIR}/config)    
    LIST(APPEND APPLICATION_PLATFORM_INC_LIST ${${X}_MISSION_DIR}/fsw/inc)
    LIST(APPEND APPLICATION_PLATFORM_INC_LIST ${${X}_MISSION_DIR}/fsw/platform_inc)
    LIST(APPEND APPLICATION_PLATFORM_INC_LIST ${${X}_MISSION_DIR}/fsw/public_inc)
    LIST(APPEND APPLICATION_PLATFORM_INC_LIST ${${X}_MISSION_DIR}/inc)
    LIST(APPEND APPLICATION_PLATFORM_INC_LIST ${${X}_MISSION_DIR}/include)
    LIST(APPEND APPLICATION_PLATFORM_INC_LIST ${${X}_MISSION_DIR}/mission_inc)
    LIST(APPEND APPLICATION_PLATFORM_INC_LIST ${${X}_MISSION_DIR}/platform_inc)
    LIST(APPEND APPLICATION_PLATFORM_INC_LIST ${${X}_MISSION_DIR}/public_inc)
    LIST(APPEND APPLICATION_PLATFORM_INC_LIST ${${X}_MISSION_DIR}/shared)
    LIST(APPEND APPLICATION_PLATFORM_INC_LIST ${${X}_MISSION_DIR}/src)
ENDFOREACH(X)

# FT_INSTALL_SUBDIR indicates where the black box test data files (lua scripts) should
# be copied during the install process.
SET(FT_INSTALL_SUBDIR "host/functional-test")

# Each target board can have its own HW arch selection and set of included apps
SET(MISSION_CPUNAMES cpu1) # cpu2

# SHIRE Host Processor (amd64)
SET(cpu1_PROCESSORID 1)
SET(cpu1_APPLIST) # Note: Using all ${MISSION_GLOBAL_APPLIST} automatically
SET(cpu1_FILELIST cfe_es_startup.scr)
if (ENABLE_UNIT_TESTS)
    SET(cpu1_SYSTEM amd64-linux)
else() 
    SET(cpu1_SYSTEM amd64-shire)
endif()

# Flight Processor (Zybo7020 - armv7l)
#SET(cpu2_PROCESSORID 2)
#SET(cpu2_APPLIST) # Note: Using all ${MISSION_GLOBAL_APPLIST} automatically
#SET(cpu2_FILELIST cfe_es_startup.scr)
#SET(cpu2_SYSTEM armv7l-linux)
