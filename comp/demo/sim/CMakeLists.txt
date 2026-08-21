cmake_minimum_required(VERSION 3.25)
project(demo_sim C)


# Top-level FSW solution directory (selectable via cfg/shire-config.yaml).
# Falls back to "cfs" if not provided by the parent build invocation.
if(DEFINED ENV{FSW_DIR})
    set(FSW_DIR "$ENV{FSW_DIR}")
else()
    set(FSW_DIR "cfs")
endif()

include(../../CompFlags.cmake)

# Find ZeroMQ dependency
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZeroMQ REQUIRED libzmq)

# Find Simulith headers
set(SIMULITH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../simulith" CACHE PATH "Path to Simulith directory")
if(NOT EXISTS "${SIMULITH_DIR}/include/simulith_transport.h")
    message(FATAL_ERROR "Could not find simulith_transport.h in ${SIMULITH_DIR}/include. Please set SIMULITH_DIR to the correct path.")
endif()

# Prefer a pre-built simulith shared library if present in the simulith build dir.
set(SIMULITH_BUILD_LIB "${SIMULITH_DIR}/build/libsimulith.so")
if(EXISTS "${SIMULITH_BUILD_LIB}")
    message(STATUS "Using prebuilt simulith: ${SIMULITH_BUILD_LIB}")
    add_library(simulith SHARED IMPORTED)
    set_target_properties(simulith PROPERTIES IMPORTED_LOCATION "${SIMULITH_BUILD_LIB}")
else()
    # Only add Simulith subdirectory if not already added
    if(NOT TARGET simulith)
        add_subdirectory(${SIMULITH_DIR} ${CMAKE_BINARY_DIR}/simulith)
    endif()
endif()

# Determine whether to include simulith source files in component targets.
if(EXISTS "${SIMULITH_BUILD_LIB}")
    set(SIMULITH_SRC_FILES "")
else()
    set(SIMULITH_SRC_FILES
    ${SIMULITH_DIR}/src/simulith_common.c
    ${SIMULITH_DIR}/src/simulith_transport.c
    )
endif()

# Create the shared library for dynamic loading by director
add_library(demo_sim_component SHARED
    demo_sim.c
    ../shared/demo_device.c
    ../../../${FSW_DIR}/psp/fsw/hwlib/src/sim/libuart.c
    ${SIMULITH_SRC_FILES}
)

# Include directories
target_include_directories(demo_sim_component
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SIMULITH_DIR}/include
    ../shared
    ../../../${FSW_DIR}/psp/fsw/hwlib/inc
    ${ZeroMQ_INCLUDE_DIRS}
)

# Link against Simulith and ZeroMQ
target_link_libraries(demo_sim_component
    PRIVATE
    simulith
    ${ZeroMQ_LIBRARIES}
)

# Set C standard for the component
set_target_properties(demo_sim_component PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    OUTPUT_NAME "demo_sim"
    PREFIX ""
)

# Add dependencies on simulith for both targets
add_dependencies(demo_sim_component simulith)

# Optionally include the simulator-side test harness. Built only when both
# BUILD_SIM_TESTS and ENABLE_UNIT_TESTS are on so the lenient test compile
# flags from ../../CompFlags.cmake apply.
option(BUILD_SIM_TESTS "Build component simulator tests" OFF)
if(BUILD_SIM_TESTS AND ENABLE_UNIT_TESTS)
    enable_testing()
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../test-sim ${CMAKE_BINARY_DIR}/test-sim)
endif()
