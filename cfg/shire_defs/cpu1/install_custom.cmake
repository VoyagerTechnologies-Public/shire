# SHIRE mission install_custom.cmake for default_cpu1
#
# Registers SC RTS table files 005-015 with the SC app.  These are not part of
# the upstream SC CMakeLists.txt (which only defines 001-004) but are needed
# because shire sets SC_NUMBER_OF_RTS=15.
#
# The add_cfe_tables_impl tool calls cfe_locate_implementation_file for each
# entry, which checks MISSION_DEFS/tables/ first; shire-provided tables
# (005, 006, 011) are found there, stubs fill the remaining slots.
#
# This file is included AFTER all apps are processed, so the sc target exists.

add_cfe_tables(sc
    sc_rts005.c
    sc_rts006.c
    sc_rts007.c
    sc_rts008.c
    sc_rts009.c
    sc_rts010.c
    sc_rts011.c
    sc_rts012.c
    sc_rts013.c
    sc_rts014.c
    sc_rts015.c
)

# Repository-owned coverage tests for upstream applications live in the
# mission layer so upstream submodules stay pristine.  This hook runs after
# all application targets have been created, which is when coverage tests can
# inherit the production target's include paths and compile definitions.
if(ENABLE_UNIT_TESTS AND NOT TARGET coverage-sch-shire-testrunner)
    add_subdirectory(
        "${MISSION_DEFS}/coverage-tests"
        "${CMAKE_BINARY_DIR}/shire-coverage-tests")
endif()
