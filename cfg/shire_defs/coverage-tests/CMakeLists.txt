##################################################################
# SHIRE-owned coverage tests for deployed upstream flight software.
##################################################################

add_cfe_coverage_test(sch shire
    "${CMAKE_CURRENT_SOURCE_DIR}/sch_coverage_tests.c"
    "${sch_MISSION_DIR}/fsw/src/sch_api.c"
    "${sch_MISSION_DIR}/fsw/src/sch_app.c"
    "${sch_MISSION_DIR}/fsw/src/sch_cmds.c"
    "${sch_MISSION_DIR}/fsw/src/sch_custom.c"
)

target_link_libraries(coverage-sch-shire-testrunner pthread)

add_cfe_coverage_test(io_lib shire
    "${CMAKE_CURRENT_SOURCE_DIR}/io_lib_coverage_tests.c"
    "${io_lib_MISSION_DIR}/fsw/src/io_lib_utils.c"
    "${io_lib_MISSION_DIR}/fsw/src/formats/tctf.c"
    "${io_lib_MISSION_DIR}/fsw/src/formats/tmtf.c"
    "${io_lib_MISSION_DIR}/fsw/src/services/tc_sync.c"
    "${io_lib_MISSION_DIR}/fsw/src/services/tm_sync.c"
    "${io_lib_MISSION_DIR}/fsw/src/services/trans_select.c"
)

add_cfe_coverage_test(to_lab shire
    "${CMAKE_CURRENT_SOURCE_DIR}/to_lab_coverage_tests.c"
    "${to_lab_MISSION_DIR}/fsw/src/to_lab_app.c"
    "${to_lab_MISSION_DIR}/fsw/src/to_lab_cmds.c"
    "${to_lab_MISSION_DIR}/fsw/src/to_lab_dispatch.c"
    "${to_lab_MISSION_DIR}/fsw/src/to_lab_passthru_encode.c"
)
target_include_directories(coverage-to_lab-shire-testrunner PRIVATE
    "${to_lab_MISSION_DIR}/fsw/inc"
    "${to_lab_MISSION_DIR}/fsw/src"
)

add_cfe_coverage_test(ci_lab shire
    "${CMAKE_CURRENT_SOURCE_DIR}/ci_lab_coverage_tests.c"
    "${ci_lab_MISSION_DIR}/fsw/src/ci_lab_app.c"
    "${ci_lab_MISSION_DIR}/fsw/src/ci_lab_cmds.c"
    "${ci_lab_MISSION_DIR}/fsw/src/ci_lab_dispatch.c"
    "${ci_lab_MISSION_DIR}/fsw/src/ci_lab_passthru_decode.c"
)
target_include_directories(coverage-ci_lab-shire-testrunner PRIVATE
    "${ci_lab_MISSION_DIR}/fsw/inc"
    "${ci_lab_MISSION_DIR}/fsw/src"
)
