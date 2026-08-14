# Tests for the demo component simulator.
#
# Loads the just-built demo_sim.so via dlopen and drives the
# component_interface_t (init/tick/cleanup/backdoor) entirely in-process.
#
# Included from comp/demo/sim/CMakeLists.txt when BUILD_SIM_TESTS=ON.
# Inherits SIMULITH_DIR, ZeroMQ_*, the demo_sim_component target, and the
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

add_executable(test_demo_sim
    test_demo_sim.c
    ${UNITY_SRC}
)

target_include_directories(test_demo_sim PRIVATE
    ${UNITY_INC}
    ${SIMULITH_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../sim
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../${FSW_DIR}/psp/fsw/hwlib/inc
    ${ZeroMQ_INCLUDE_DIRS}
)

# dl: dlopen/dlsym/dlclose. simulith: transport API used directly by the
# wire-protocol tests (must resolve to the same instance the dlopen'd .so
# uses, which it does because both link the shared libsimulith.so).
# pthread/m: pulled in transitively by libsimulith.
target_link_libraries(test_demo_sim PRIVATE simulith dl pthread m)

# Point the harness at the .so produced by the demo_sim_component target so
# the test runs against the in-tree build with no install/copy step.
target_compile_definitions(test_demo_sim PRIVATE
    "DEMO_SIM_SO_PATH=\"$<TARGET_FILE:demo_sim_component>\""
)

add_dependencies(test_demo_sim demo_sim_component)

enable_testing()
add_test(NAME DemoSimTests COMMAND test_demo_sim)
