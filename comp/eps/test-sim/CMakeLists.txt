# Tests for the EPS component simulator.
#
# Loads the just-built eps_sim.so via dlopen and drives the
# component_interface_t (init/tick/cleanup) entirely in-process.
#
# Included from comp/eps/sim/CMakeLists.txt when BUILD_SIM_TESTS=ON.
# Inherits SIMULITH_DIR, ZeroMQ_*, the eps_sim_component target, and the
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

add_executable(test_eps_sim
    test_eps_sim.c
    ${UNITY_SRC}
)

target_include_directories(test_eps_sim PRIVATE
    ${UNITY_INC}
    ${SIMULITH_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../sim
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../${FSW_DIR}/psp/fsw/hwlib/inc
    ${ZeroMQ_INCLUDE_DIRS}
)

target_link_libraries(test_eps_sim PRIVATE simulith dl pthread m)

target_compile_definitions(test_eps_sim PRIVATE
    "EPS_SIM_SO_PATH=\"$<TARGET_FILE:eps_sim_component>\""
    UNITY_INCLUDE_DOUBLE
)

add_dependencies(test_eps_sim eps_sim_component)

enable_testing()
add_test(NAME EpsSimTests COMMAND test_eps_sim)
