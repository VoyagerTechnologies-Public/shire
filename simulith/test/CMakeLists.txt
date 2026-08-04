# Unity source and include path
set(UNITY_SRC ./unity/unity.c)
set(UNITY_INC ./unity)

# Include paths for tests
include_directories(
    ${UNITY_INC}
    ../include
    ${ZeroMQ_INCLUDE_DIRS}
)

add_executable(test_42_command_api test_42.c ${UNITY_SRC})
target_link_libraries(test_42_command_api simulith ${ZeroMQ_LIBRARIES} pthread)
target_compile_definitions(test_42_command_api PRIVATE SIMULITH_TESTING)
add_test(NAME 42Tests COMMAND test_42_command_api)

add_executable(test_common test_common.c ${UNITY_SRC})
target_link_libraries(test_common simulith ${ZeroMQ_LIBRARIES})
target_compile_definitions(test_common PRIVATE SIMULITH_TESTING)
add_test(NAME CommonTests COMMAND test_common)

add_executable(test_simulith test_simulith.c ${UNITY_SRC})
target_link_libraries(test_simulith simulith ${ZeroMQ_LIBRARIES} pthread)
target_compile_definitions(test_simulith PRIVATE SIMULITH_TESTING)
add_test(NAME SimulithTests COMMAND test_simulith)

add_executable(test_time test_time.c ${UNITY_SRC})
target_link_libraries(test_time simulith ${ZeroMQ_LIBRARIES} pthread)
target_compile_definitions(test_time PRIVATE SIMULITH_TESTING)
add_test(NAME TimeTests COMMAND test_time)

add_executable(test_transport test_transport.c ${UNITY_SRC})
target_link_libraries(test_transport simulith ${ZeroMQ_LIBRARIES})
target_compile_definitions(test_transport PRIVATE SIMULITH_TESTING)
add_test(NAME TransportTests COMMAND test_transport)
