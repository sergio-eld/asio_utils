
#include <gtest/gtest.h>

#include "asio_ct/connection_tools.hpp"

// cancel on 15 timeouts
TEST(connection_attempt, 15_timeouts)
{
    asio::io_context context;
    asio::ip::tcp::socket socket{context};
    size_t timeouts = 15;

    std::future<void> result =
            eld::async_connection_attempt(socket,
                                          asio::ip::tcp::endpoint(
                                                  asio::ip::make_address_v4("127.0.0.1"), 12000),
                                          15,
                                          std::chrono::milliseconds(20),
                                          asio::use_future,
                                          [&timeouts](const asio::error_code &errorCode) -> bool
                                          {
                                              EXPECT_EQ(errorCode, asio::error::timed_out);
                                              --timeouts;
                                              return false;
                                          });

    auto runContext = std::async(std::launch::async,
                                 [&]()
                                 {
                                     context.run();
                                 });
    result.wait();

    ASSERT_EQ(timeouts, 0);
}

// cancel with onError and infinite attempts
TEST(connection_attempt, interrupted_infinite_attempts)
{
    asio::io_context context;
    asio::ip::tcp::socket socket{context};
    size_t timeouts = 15;
    auto endpoint = asio::ip::tcp::endpoint(
            asio::ip::make_address_v4("127.0.0.1"), 12000);
    std::future<void> res =
            eld::async_connection_attempt(socket,
                                          std::move(endpoint),
                                          std::chrono::milliseconds(20),
                                          asio::use_future,
                                          [&timeouts](const asio::error_code &errorCode) -> bool
                                          {
                                              EXPECT_EQ(errorCode, asio::error::timed_out);
                                              return !--timeouts;
                                          });


    auto runContext = std::async(std::launch::async,
                                 [&]()
                                 {
                                     context.run();
                                 });
    res.wait();

    ASSERT_EQ(timeouts, 0);
}

// TODO: successful connection
// TODO: rejected connection

// TODO: call connect twice while pending
// TODO: call connect when already connected

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}