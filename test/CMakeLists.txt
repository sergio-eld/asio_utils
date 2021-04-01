
link_libraries(eld::connection_tools GTest::gtest)

add_executable(test.make_connection_attempt test_make_connection_attempt.cpp)
add_test(NAME test.make_connection_attempt
        COMMAND test.make_connection_attempt)
