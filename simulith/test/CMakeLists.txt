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
target_compile_definitions(test_simulith PRIVATE
    SIMULITH_TESTING
    SIMULITH_SERVER_PATH="$<TARGET_FILE:simulith_server_standalone>")
add_dependencies(test_simulith simulith_server_standalone)
add_test(NAME SimulithTests COMMAND test_simulith)

add_executable(test_server_failures test_server_failures.c ../src/simulith_server.c ${UNITY_SRC})
target_link_libraries(test_server_failures ${ZeroMQ_LIBRARIES})
target_compile_definitions(test_server_failures PRIVATE SIMULITH_TESTING)
target_link_options(test_server_failures PRIVATE
    -Wl,--wrap=zmq_ctx_new
    -Wl,--wrap=zmq_ctx_term
    -Wl,--wrap=zmq_socket
    -Wl,--wrap=zmq_bind
    -Wl,--wrap=zmq_setsockopt
    -Wl,--wrap=zmq_close
    -Wl,--wrap=zmq_send
    -Wl,--wrap=zmq_recv)
add_test(NAME ServerFailureTests COMMAND test_server_failures)

add_executable(test_client_failures test_client_failures.c ../src/simulith_client.c ${UNITY_SRC})
target_link_libraries(test_client_failures ${ZeroMQ_LIBRARIES})
target_compile_definitions(test_client_failures PRIVATE SIMULITH_TESTING)
target_link_options(test_client_failures PRIVATE
    -Wl,--wrap=zmq_ctx_new
    -Wl,--wrap=zmq_ctx_term
    -Wl,--wrap=zmq_socket
    -Wl,--wrap=zmq_connect
    -Wl,--wrap=zmq_setsockopt
    -Wl,--wrap=zmq_close
    -Wl,--wrap=zmq_send
    -Wl,--wrap=zmq_recv)
add_test(NAME ClientFailureTests COMMAND test_client_failures)

add_executable(test_time test_time.c ${UNITY_SRC})
target_link_libraries(test_time simulith ${ZeroMQ_LIBRARIES} pthread)
target_compile_definitions(test_time PRIVATE SIMULITH_TESTING)
add_test(NAME TimeTests COMMAND test_time)

add_executable(test_time_failures test_time_failures.c ../src/simulith_time.c ${UNITY_SRC})
target_link_libraries(test_time_failures ${ZeroMQ_LIBRARIES})
target_compile_definitions(test_time_failures PRIVATE SIMULITH_TESTING)
target_link_options(test_time_failures PRIVATE
    -Wl,--wrap=malloc
    -Wl,--wrap=zmq_ctx_new
    -Wl,--wrap=zmq_ctx_destroy
    -Wl,--wrap=zmq_socket
    -Wl,--wrap=zmq_connect
    -Wl,--wrap=zmq_setsockopt
    -Wl,--wrap=zmq_close
    -Wl,--wrap=zmq_recv)
add_test(NAME TimeFailureTests COMMAND test_time_failures)

add_executable(test_transport test_transport.c ${UNITY_SRC})
target_link_libraries(test_transport simulith ${ZeroMQ_LIBRARIES})
target_compile_definitions(test_transport PRIVATE SIMULITH_TESTING)
add_test(NAME TransportTests COMMAND test_transport)

add_executable(test_transport_failures test_transport_failures.c ../src/simulith_transport.c ${UNITY_SRC})
target_link_libraries(test_transport_failures ${ZeroMQ_LIBRARIES})
target_compile_definitions(test_transport_failures PRIVATE SIMULITH_TESTING)
target_link_options(test_transport_failures PRIVATE
    -Wl,--wrap=zmq_ctx_new
    -Wl,--wrap=zmq_ctx_term
    -Wl,--wrap=zmq_socket
    -Wl,--wrap=zmq_setsockopt
    -Wl,--wrap=zmq_close
    -Wl,--wrap=zmq_send
    -Wl,--wrap=zmq_poll
    -Wl,--wrap=zmq_msg_recv)
add_test(NAME TransportFailureTests COMMAND test_transport_failures)

add_executable(test_42_socket_client test_42_socket_client.c ${UNITY_SRC})
target_link_libraries(test_42_socket_client simulith pthread)
target_compile_definitions(test_42_socket_client PRIVATE SIMULITH_TESTING)
add_test(NAME 42SocketClientTests COMMAND test_42_socket_client)

add_executable(test_director test_director.c ${UNITY_SRC})
target_link_libraries(test_director simulith_director_core)
set(DIRECTOR_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/director-fixtures")

add_library(director_fixture_valid MODULE director_component_fixture.c)
add_library(director_fixture_null MODULE director_component_fixture.c)
add_library(director_fixture_no_interface MODULE director_component_fixture.c)
target_compile_definitions(director_fixture_null PRIVATE DIRECTOR_FIXTURE_NULL_INTERFACE)
target_compile_definitions(director_fixture_no_interface PRIVATE DIRECTOR_FIXTURE_NO_INTERFACE)
set_target_properties(
    director_fixture_valid director_fixture_null director_fixture_no_interface
    PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${DIRECTOR_FIXTURE_DIR}")

add_dependencies(test_director
    director_fixture_valid director_fixture_null director_fixture_no_interface)
target_compile_definitions(test_director PRIVATE
    SIMULITH_TESTING
    DIRECTOR_FIXTURE_DIR="${DIRECTOR_FIXTURE_DIR}")
add_test(NAME DirectorTests COMMAND test_director)

# Exercise every entry-point outcome with the real main source and deterministic
# service stubs. Renaming main is confined to this object target.
add_library(director_main_test_object OBJECT ../src/simulith_director_main.c)
target_compile_definitions(director_main_test_object PRIVATE main=simulith_director_main)
add_executable(test_director_main test_director_main.c
    $<TARGET_OBJECTS:director_main_test_object> ${UNITY_SRC})
target_link_libraries(test_director_main pthread)
target_link_options(test_director_main PRIVATE -Wl,--wrap=sleep)
target_compile_definitions(test_director_main PRIVATE SIMULITH_TESTING)
add_test(NAME DirectorMainTests COMMAND test_director_main)

# Exercise the thin standalone entry points without starting external services.
add_test(NAME DirectorHelpTest COMMAND $<TARGET_FILE:simulith_director_standalone> --help)
add_test(NAME DirectorWithoutServerTest COMMAND $<TARGET_FILE:simulith_director_standalone>)
set_tests_properties(DirectorWithoutServerTest PROPERTIES
    WILL_FAIL TRUE
    ENVIRONMENT "SIMULITH_42_RECONNECT_ATTEMPTS=1;SIMULITH_42_RECONNECT_DELAY_MS=0;FORTYTWO_HOST=127.0.0.1;FORTYTWO_PORT=1;SIMULITH_GSW_HOST=127.0.0.1")
add_test(NAME ServerInvalidClientCountTest COMMAND $<TARGET_FILE:simulith_server_standalone> 0)
set_tests_properties(ServerInvalidClientCountTest PROPERTIES WILL_FAIL TRUE)

# Prevent a failed socket/thread fixture from stalling CI indefinitely.
set_tests_properties(
    SimulithTests DirectorTests DirectorMainTests DirectorWithoutServerTest
    PROPERTIES TIMEOUT 20)
