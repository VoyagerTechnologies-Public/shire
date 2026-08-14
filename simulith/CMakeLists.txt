# Simulith CMake configuration
cmake_minimum_required(VERSION 3.25)
project(Simulith VERSION 0.0 LANGUAGES C)

option(BUILD_SIMULITH_TESTS "Build Simulith tests" OFF)

# Enable compile_commands.json for IDEs and static analysis
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find ZeroMQ via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZeroMQ REQUIRED libzmq)

# Include optional component flags if available
set(COMP_FLAGS_FILE "${CMAKE_CURRENT_LIST_DIR}/../comp/CompFlags.cmake")
if(EXISTS "${COMP_FLAGS_FILE}")
    message(STATUS "Including component flags from ${COMP_FLAGS_FILE}")
    include("${COMP_FLAGS_FILE}")
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # Basic fallback flags when component flags aren't available
    add_compile_options(-Wall -Werror)
    # If building tests, ensure we enable diagnostics and coverage flags
    if(BUILD_SIMULITH_TESTS)
        message(STATUS "CompFlags.cmake not found — adding test/coverage flags for GNU/Clang")
        add_compile_options(-fdiagnostics-show-option -fprofile-arcs -ftest-coverage)
        # Ensure linker emits coverage data too
        if(POLICY CMP0068)
            # add_link_options is available; attach --coverage for linking
            add_link_options(--coverage)
        else()
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} --coverage")
        endif()
    endif()
endif()

if(BUILD_SIMULITH_TESTS)
    # Create Unity library with specific compiler flags and always build with -fPIC
    add_library(unity STATIC ./test/unity/unity.c)
    set_target_properties(unity PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(unity PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/unity)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(unity PRIVATE -Wno-float-equal)
    endif()
endif()

# Simulith library sources
set(SIMULITH_SOURCES
    src/simulith_42_command_api.c
    src/simulith_42_socket_client.c
    src/simulith_common.c
    src/simulith_client.c
    src/simulith_server.c
    src/simulith_time.c
    src/simulith_transport.c
)

# Build Simulith as a shared library so director and dynamic components share symbols
add_library(simulith SHARED ${SIMULITH_SOURCES})
set_target_properties(simulith PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(simulith PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/unity
    ${CMAKE_CURRENT_SOURCE_DIR}/../42/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../42/Kit/Include
    ${ZeroMQ_INCLUDE_DIRS}
)
target_link_libraries(simulith PUBLIC ${ZeroMQ_LIBRARIES})

if(BUILD_SIMULITH_TESTS)
    # When building tests, expose internal symbols for test-only helpers
    target_compile_definitions(simulith PRIVATE SIMULITH_TESTING)
endif()

# NOTE: 42 is now built separately as a standalone service
# 42 directory is at ../42 (top-level) and will be containerized independently
# Director will communicate with 42 via socket IPC instead of direct linking

# Build Simulith director (no longer links to 42)
add_executable(simulith_director_standalone src/simulith_director.c)
target_link_libraries(simulith_director_standalone PRIVATE simulith ${ZeroMQ_LIBRARIES} dl m pthread)
set_target_properties(simulith_director_standalone PROPERTIES
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
)

# Build Simulith server standalone
add_executable(simulith_server_standalone 
    src/simulith_common.c
    src/simulith_server.c
    src/simulith_server_standalone.c)
target_link_libraries(simulith_server_standalone PRIVATE simulith ${ZeroMQ_LIBRARIES})
set_target_properties(simulith_server_standalone PROPERTIES
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
)

# Optionally add tests subdirectory
if(BUILD_SIMULITH_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
