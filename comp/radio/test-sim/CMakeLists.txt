# Tests for the radio component simulator.
#
# Loads the just-built radio_sim.so via dlopen and drives the
# component_interface_t (init/tick/cleanup) entirely in-process.
#
# Included from comp/radio/sim/CMakeLists.txt when BUILD_SIM_TESTS=ON.
# Inherits SIMULITH_DIR, ZeroMQ_*, the radio_sim_component target, and the
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

add_executable(test_radio_sim
    test_radio_sim.c
    ${UNITY_SRC}
)

target_include_directories(test_radio_sim PRIVATE
    ${UNITY_INC}
    ${SIMULITH_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../sim
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../${FSW_DIR}/psp/fsw/hwlib/inc
    ${ZeroMQ_INCLUDE_DIRS}
)

target_link_libraries(test_radio_sim PRIVATE simulith dl pthread m)

target_compile_definitions(test_radio_sim PRIVATE
    "RADIO_SIM_SO_PATH=\"$<TARGET_FILE:radio_sim_component>\""
)

add_dependencies(test_radio_sim radio_sim_component)

enable_testing()
add_test(NAME RadioSimTests COMMAND test_radio_sim)
