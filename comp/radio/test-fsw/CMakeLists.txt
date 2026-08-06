##################################################################
#
# Coverage Unit Test build recipe
#
# This CMake file contains the recipe for building the radio unit tests.
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

add_cfe_coverage_stubs(radio-internal 
    stubs/libspi_stubs.c 
    stubs/libgpio_stubs.c 
    stubs/cryptolib_stubs.c 
    stubs/io_lib_stubs.c
    stubs/radio_device_stubs.c
) 
target_link_libraries(coverage-radio-internal-stubs ut_core_api_stubs ut_assert)
target_include_directories(coverage-radio-internal-stubs PUBLIC $<TARGET_PROPERTY:radio,INCLUDE_DIRECTORIES>)

 
# Use the UT assert public API, and allow direct
# inclusion of source files that are normally private
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/inc)
include_directories(${hwlib_MISSION_DIR}/public_inc)
include_directories(${PROJECT_SOURCE_DIR}/../../${FSW_DIR}/osal/ut_assert/inc)
include_directories(${PROJECT_SOURCE_DIR}/../cryptolib/include)
include_directories(${PROJECT_SOURCE_DIR}/../../${FSW_DIR}/apps/io_lib/fsw/public_inc)


# Add a coverage test executable called "radio-ALL" that 
# covers all of the functions in radio_app.  
#
# Also note in a more complex app/lib the coverage test can also
# be broken down into smaller units (in which case one should use
# a unique suffix other than "ALL" for each unit).  For example,
# OSAL implements a separate coverage test per source unit.
add_cfe_coverage_test(radio ALL 
    "coveragetest/coveragetest_radio_app.c"
    "../src/radio_app.c"
    "../shared/radio_device.c"
)

add_cfe_coverage_test(radio DEVICE 
    "coveragetest/coveragetest_radio_device.c"
    "../shared/radio_device.c"
    "stubs/libspi_stubs.c"
    "stubs/libgpio_stubs.c"
)

add_cfe_coverage_dependency(radio ALL radio-internal)


# The radio uses library functions provided by radio_lib so must be linked
# with the radio_lib stub library (this is mainly just an example of how this 
# can be done).
#add_cfe_coverage_dependency(radio ALL radio_lib)

