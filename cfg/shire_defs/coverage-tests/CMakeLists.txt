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
    "${io_lib_MISSION_DIR}/fsw/src/services/cop1.c"
    "${io_lib_MISSION_DIR}/fsw/src/services/tm_sdlp.c"
    "${io_lib_MISSION_DIR}/fsw/src/services/trans_rs422.c"
    "${io_lib_MISSION_DIR}/fsw/src/services/trans_select.c"
    "${io_lib_MISSION_DIR}/fsw/src/services/trans_udp.c"
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

add_cfe_coverage_test(io_lib linux_sysmon
    "${CMAKE_CURRENT_SOURCE_DIR}/linux_sysmon_coverage_tests.c"
    "${CFEPSP_SOURCE_DIR}/fsw/modules/linux_sysmon/linux_sysmon.c"
)
target_compile_definitions(coverage-io_lib-linux_sysmon-object PRIVATE _CFE_PSP_MODULE_)
target_compile_definitions(coverage-io_lib-linux_sysmon-testrunner PRIVATE _CFE_PSP_MODULE_)
target_include_directories(coverage-io_lib-linux_sysmon-object PRIVATE
    "${CFE_SOURCE_DIR}/cmake/target/inc"
    "${CFEPSP_SOURCE_DIR}/fsw/inc"
    "${CFEPSP_SOURCE_DIR}/fsw/shared/inc"
    "${CFEPSP_SOURCE_DIR}/fsw/modules/iodriver/inc"
    "${CFEPSP_SOURCE_DIR}/fsw/modules/linux_sysmon"
)
target_include_directories(coverage-io_lib-linux_sysmon-testrunner PRIVATE
    "${CFE_SOURCE_DIR}/cmake/target/inc"
    "${CFEPSP_SOURCE_DIR}/fsw/inc"
    "${CFEPSP_SOURCE_DIR}/fsw/shared/inc"
    "${CFEPSP_SOURCE_DIR}/fsw/modules/iodriver/inc"
    "${CFEPSP_SOURCE_DIR}/fsw/modules/linux_sysmon"
)
target_link_libraries(coverage-io_lib-linux_sysmon-testrunner pthread)

# Exercise the SHIRE OSAL adapter against the real host pthread and signal
# primitives.  The test supplies only the neighboring OSAL/PSP seams; the
# binary semaphore and common adapter sources themselves are production code.
add_cfe_coverage_test(io_lib shire_osal_runtime
    "${CMAKE_CURRENT_SOURCE_DIR}/shire_osal_runtime_tests.c"
    "${OSAL_SOURCE_DIR}/src/os/shire/src/os-impl-binsem.c"
    "${OSAL_SOURCE_DIR}/src/os/shire/src/os-impl-common.c"
)
foreach(target coverage-io_lib-shire_osal_runtime-object coverage-io_lib-shire_osal_runtime-testrunner)
    target_include_directories(${target} PRIVATE
        "${OSAL_SOURCE_DIR}/src/os/shire/inc"
        "${OSAL_SOURCE_DIR}/src/os/posix/inc"
        "${OSAL_SOURCE_DIR}/src/os/shared/inc"
        "${OSAL_SOURCE_DIR}/src/bsp/shared/inc"
        "${CFEPSP_SOURCE_DIR}/fsw/shire/inc"
        "${CMAKE_SOURCE_DIR}/../../simulith/include"
    )
endforeach()
target_link_libraries(coverage-io_lib-shire_osal_runtime-testrunner pthread)

# Run SHIRE's PSP clock distribution and exception adapters with real pthread
# synchronization and signals.  The Simulith client boundary is replaced by a
# deterministic in-process tick queue, avoiding any external network service.
add_cfe_coverage_test(io_lib shire_psp_runtime
    "${CMAKE_CURRENT_SOURCE_DIR}/shire_psp_runtime_tests.c"
    "${CFEPSP_SOURCE_DIR}/fsw/shire/src/cfe_psp_timebase.c"
    "${CFEPSP_SOURCE_DIR}/fsw/shire/src/cfe_psp_exception.c"
)
foreach(target coverage-io_lib-shire_psp_runtime-object coverage-io_lib-shire_psp_runtime-testrunner)
    target_compile_definitions(${target} PRIVATE _CFE_PSP_MODULE_)
    target_include_directories(${target} PRIVATE
        "${CFE_SOURCE_DIR}/cmake/target/inc"
        "${CFEPSP_SOURCE_DIR}/fsw/inc"
        "${CFEPSP_SOURCE_DIR}/fsw/shared/inc"
        "${CFEPSP_SOURCE_DIR}/fsw/shire/inc"
        "${OSAL_SOURCE_DIR}/src/os/inc"
        "${CMAKE_SOURCE_DIR}/../../simulith/include"
    )
endforeach()
target_link_libraries(coverage-io_lib-shire_psp_runtime-testrunner pthread)

add_cfe_coverage_test(io_lib shire_osal_tasks_runtime
    "${CMAKE_CURRENT_SOURCE_DIR}/shire_osal_tasks_runtime_tests.c"
    "${OSAL_SOURCE_DIR}/src/os/shire/src/os-impl-tasks.c"
)
foreach(target coverage-io_lib-shire_osal_tasks_runtime-object coverage-io_lib-shire_osal_tasks_runtime-testrunner)
    target_include_directories(${target} PRIVATE
        "${OSAL_SOURCE_DIR}/src/os/shire/inc"
        "${OSAL_SOURCE_DIR}/src/os/posix/inc"
        "${OSAL_SOURCE_DIR}/src/os/shared/inc"
        "${OSAL_SOURCE_DIR}/src/bsp/shared/inc"
        "${CFEPSP_SOURCE_DIR}/fsw/shire/inc"
        "${CMAKE_SOURCE_DIR}/../../simulith/include"
    )
endforeach()
target_link_libraries(coverage-io_lib-shire_osal_tasks_runtime-testrunner pthread)

add_cfe_coverage_test(io_lib shire_osal_timebase_runtime
    "${CMAKE_CURRENT_SOURCE_DIR}/shire_osal_timebase_runtime_tests.c"
    "${OSAL_SOURCE_DIR}/src/os/shire/src/os-impl-timebase.c"
)
foreach(target coverage-io_lib-shire_osal_timebase_runtime-object coverage-io_lib-shire_osal_timebase_runtime-testrunner)
    target_include_directories(${target} PRIVATE
        "${OSAL_SOURCE_DIR}/src/os/shire/inc"
        "${OSAL_SOURCE_DIR}/src/os/posix/inc"
        "${OSAL_SOURCE_DIR}/src/os/shared/inc"
        "${OSAL_SOURCE_DIR}/src/bsp/shared/inc"
        "${CFEPSP_SOURCE_DIR}/fsw/shire/inc"
        "${CMAKE_SOURCE_DIR}/../../simulith/include"
    )
endforeach()
target_link_libraries(coverage-io_lib-shire_osal_timebase_runtime-testrunner pthread)
