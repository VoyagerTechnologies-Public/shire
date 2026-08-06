##################################################################
#
# Coverage Unit Test build recipe
#
# This CMake file contains the recipe for building the eps unit tests.
# It is invoked from the parent directory when unit tests are enabled.
#
##################################################################

#
#
# NOTE on the subdirectory structures here:
#
# - "inc" provides local header files shared between the coveragetest,
#    wrappers, and overrides source code units
# - "coveragetest" contains source code for the actual unit test cases
#    The primary objective is to get line/path coverage on the FSW 
#    code units.
#


# Top-level FSW solution directory (selectable via cfg/shire-config.yaml).
# Falls back to "cfs" if not provided by the parent build invocation.
if(DEFINED ENV{FSW_DIR})
    set(FSW_DIR "$ENV{FSW_DIR}")
else()
    set(FSW_DIR "cfs")
endif()

add_cfe_coverage_stubs(eps-internal stubs/eps_device_stubs.c stubs/libi2c_stubs.c) 
target_link_libraries(coverage-eps-internal-stubs ut_core_api_stubs ut_assert)
target_include_directories(coverage-eps-internal-stubs PUBLIC $<TARGET_PROPERTY:eps,INCLUDE_DIRECTORIES>)

 
# Use the UT assert public API, and allow direct
# inclusion of source files that are normally private
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/inc)
include_directories(${PROJECT_SOURCE_DIR}/../../../${FSW_DIR}/psp/fsw/hwlib/inc)
include_directories(${PROJECT_SOURCE_DIR}/../../${FSW_DIR}/osal/ut_assert/inc)


# Add a coverage test executable called "eps-ALL" that 
# covers all of the functions in eps_app.  
#
# Also note in a more complex app/lib the coverage test can also
# be broken down into smaller units (in which case one should use
# a unique suffix other than "ALL" for each unit).  For example,
# OSAL implements a separate coverage test per source unit.
add_cfe_coverage_test(eps ALL 
    "coveragetest/coveragetest_eps_app.c"
    "../src/eps_app.c"
)

add_cfe_coverage_test(eps DEVICE 
    "coveragetest/coveragetest_eps_device.c"
    "../shared/eps_device.c"
    "stubs/libi2c_stubs.c"
)

add_cfe_coverage_dependency(eps ALL eps-internal)


# The eps uses library functions provided by eps_lib so must be linked
# with the eps_lib stub library (this is mainly just an example of how this 
# can be done).
#add_cfe_coverage_dependency(eps ALL eps_lib)

