
link_libraries(eld::connection_tools GTest::gtest)

add_executable(test.make_connection_attempt test_make_connection_attempt.cpp)
target_link_libraries(test.make_connection_attempt WS2_32)
add_test(NAME test.make_connection_attempt
        COMMAND test.make_connection_attempt)
