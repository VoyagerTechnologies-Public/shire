cmake_minimum_required(VERSION 3.25)
project (demo_cli)


# Top-level FSW solution directory (selectable via cfg/shire-config.yaml).
# Falls back to "cfs" if not provided by the parent build invocation.
if(DEFINED ENV{FSW_DIR})
    set(FSW_DIR "$ENV{FSW_DIR}")
else()
    set(FSW_DIR "cfs")
endif()

if (NOT DEFINED TGTNAME)
  message(FATAL_ERROR "TGTNAME must be defined on the cmake command line (e.g. \"-DTGTNAME=cpu1\")")
endif()

include(../../CompFlags.cmake)

if(${TGTNAME} STREQUAL cpu1)
  # Find ZeroMQ via pkg-config
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(ZeroMQ REQUIRED libzmq)
  
  # Find Simulith headers
  set(SIMULITH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../simulith" CACHE PATH "Path to Simulith directory")
  if(NOT EXISTS "${SIMULITH_DIR}/include/simulith_transport.h")
      message(FATAL_ERROR "Could not find simulith_transport.h in ${SIMULITH_DIR}/include. Please set SIMULITH_DIR to the correct path.")
  endif()

  # Prefer a pre-built simulith static library if present in the simulith build dir.
  set(SIMULITH_BUILD_LIB "${SIMULITH_DIR}/build/libsimulith.a")
  if(EXISTS "${SIMULITH_BUILD_LIB}")
      message(STATUS "Using prebuilt simulith: ${SIMULITH_BUILD_LIB}")
      add_library(simulith STATIC IMPORTED)
      set_target_properties(simulith PROPERTIES IMPORTED_LOCATION "${SIMULITH_BUILD_LIB}")
  else()
      # Only add Simulith subdirectory if not already added
      if(NOT TARGET simulith)
          add_subdirectory(${SIMULITH_DIR} ${CMAKE_BINARY_DIR}/simulith)
      endif()
  endif()
endif()

# Use target_include_directories for demo_cli
set(demo_cli_includes
    "./"
    "../platform_inc"
    "../src"
    "../shared"
    "../../../${FSW_DIR}/psp/fsw/hwlib/inc"
)

set(demo_cli_src
  demo_cli.c 
  ../shared/demo_device.c
)

# Hardware library switch based on TGTNAME
if(${TGTNAME} STREQUAL cpu1)
  list(APPEND demo_cli_includes 
    "${SIMULITH_DIR}/include" 
  )
  set(demo_cli_src 
    ${demo_cli_src}
    ${SIMULITH_DIR}/src/simulith_common.c
    ${SIMULITH_DIR}/src/simulith_transport.c
    ../../../${FSW_DIR}/psp/fsw/hwlib/src/sim/libuart.c
  )
  set(demo_cli_libs
    ${ZeroMQ_LIBRARIES}
    simulith
  )
endif()
if(${TGTNAME} STREQUAL cpu2)
  set(demo_cli_src 
    ${demo_cli_src}
    ../../../${FSW_DIR}/psp/fsw/hwlib/src/linux/libuart.c
  )
endif()

add_executable(demo_cli ${demo_cli_src})
target_include_directories(demo_cli PRIVATE ${demo_cli_includes})
target_link_libraries(demo_cli ${demo_cli_libs})

if(${TGTNAME} STREQUAL cpu1)
  set_target_properties(demo_cli PROPERTIES COMPILE_FLAGS "-g" LINK_FLAGS "-g")
endif()
