
link_libraries(eld::asio_utils GTest::gtest)

add_executable(test.make_connection_attempt test_make_connection_attempt.cpp)
add_test(NAME test.make_connection_attempt
        COMMAND test.make_connection_attempt)

add_executable(test.composed_send test_composed_send.cpp)
add_test(NAME test.composed_send
        COMMAND test.composed_send)