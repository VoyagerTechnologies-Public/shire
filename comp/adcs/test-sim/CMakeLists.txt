# Tests for the ADCS component simulator.
#
# Loads the just-built adcs_sim.so via dlopen and drives the
# component_interface_t (init/tick/cleanup) entirely in-process.
#
# Included from comp/adcs/sim/CMakeLists.txt when BUILD_SIM_TESTS=ON.
# Inherits SIMULITH_DIR, ZeroMQ_*, the adcs_sim_component target, and the
# component flags applied by ../../CompFlags.cmake from the parent scope.

# Vendored Unity from simulith.

# Top-level FSW solution directory (selectable via cfg/shire-config.yaml).
# Falls back to "cfs" if not provided by the parent build invocation.
if(DEFINED ENV{FSW_DIR})
    set(FSW_DIR "$ENV{FSW_DIR}")
else()
    set(FSW_DIR "cfs")
endif()

set(UNITY_SRC ${SIMULITH_DIR}/test/unity/unity.c)
set(UNITY_INC ${SIMULITH_DIR}/test/unity)

if(NOT EXISTS "${UNITY_SRC}")
    message(FATAL_ERROR "Unity source not found at ${UNITY_SRC}")
endif()

add_executable(test_adcs_sim
    test_adcs_sim.c
    ${UNITY_SRC}
)

target_include_directories(test_adcs_sim PRIVATE
    ${UNITY_INC}
    ${SIMULITH_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../sim
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../${FSW_DIR}/psp/fsw/hwlib/inc
    ${ZeroMQ_INCLUDE_DIRS}
)

target_link_libraries(test_adcs_sim PRIVATE simulith dl pthread m)

target_compile_definitions(test_adcs_sim PRIVATE
    "ADCS_SIM_SO_PATH=\"$<TARGET_FILE:adcs_sim_component>\""
)

add_dependencies(test_adcs_sim adcs_sim_component)

enable_testing()
add_test(NAME AdcsSimTests COMMAND test_adcs_sim)

# Compile the production simulator directly into a fault-injection binary so
# transport and allocation failures can be tested deterministically.
add_executable(test_adcs_sim_failures
    test_adcs_sim_failures.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../sim/adcs_sim.c
    ${UNITY_SRC}
)

target_include_directories(test_adcs_sim_failures PRIVATE
    ${UNITY_INC}
    ${SIMULITH_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../sim
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../${FSW_DIR}/psp/fsw/hwlib/inc
    ${ZeroMQ_INCLUDE_DIRS}
)

target_link_libraries(test_adcs_sim_failures PRIVATE pthread m)
target_link_options(test_adcs_sim_failures PRIVATE
    -Wl,--wrap=malloc
)
add_test(NAME AdcsSimFailureTests COMMAND test_adcs_sim_failures)

# Contract test: confirms ADCS HK telemetry and the 42-truth wire packet
# (simulith_serialize_42_telemetry, simulith/src/simulith_director.c) stay
# aligned for the fields they share (issue #8 item 1). Compiles
# simulith_director.c directly rather than depending on whichever
# simulith_director_core/prebuilt-libsimulith.so branch the parent build
# took, so this target links the same regardless of that choice.
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZeroMQ REQUIRED libzmq)

add_executable(test_adcs_truth_alignment
    test_adcs_truth_alignment.c
    ${SIMULITH_DIR}/src/simulith_director.c
    ${UNITY_SRC}
)

target_include_directories(test_adcs_truth_alignment PRIVATE
    ${UNITY_INC}
    ${SIMULITH_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../sim
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../${FSW_DIR}/psp/fsw/hwlib/inc
    ${ZeroMQ_INCLUDE_DIRS}
)

target_link_libraries(test_adcs_truth_alignment PRIVATE simulith dl pthread m ${ZeroMQ_LIBRARIES})

target_compile_definitions(test_adcs_truth_alignment PRIVATE
    "ADCS_SIM_SO_PATH=\"$<TARGET_FILE:adcs_sim_component>\""
)

add_dependencies(test_adcs_truth_alignment adcs_sim_component)
add_test(NAME AdcsTruthAlignmentTests COMMAND test_adcs_truth_alignment)
