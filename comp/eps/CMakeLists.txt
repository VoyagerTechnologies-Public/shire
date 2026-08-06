cmake_minimum_required(VERSION 3.25)
project(CFS_EPS_APP C)

include(../CompFlags.cmake)

include_directories(public_inc)
include_directories(mission_inc)
include_directories(platform_inc)
include_directories(src)

include_directories(shared)
include_directories(${CMAKE_SOURCE_DIR}/../psp/fsw/hwlib/inc)
include_directories(${APPLICATION_PLATFORM_INC_LIST})

aux_source_directory(src APP_SRC_FILES)

# Create the app module
add_cfe_app(eps ${APP_SRC_FILES}
            shared/eps_device.c)

# If UT is enabled, then add the tests from the subdirectory
# Note that this is an app, and therefore does not provide
# stub functions, as other entities would not typically make
# direct function calls into this application.
if (ENABLE_UNIT_TESTS)
  add_subdirectory(test-fsw)
endif (ENABLE_UNIT_TESTS)
