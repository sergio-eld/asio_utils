
#include <gtest/gtest.h>

#include "asio_ct/connection_tools.hpp"

// TODO: 15 timeouts
TEST(connection_attempt, 15_timeouts)
{
    asio::io_context context;
    asio::ip::tcp::socket socket{context};
    size_t attempts = 2;
    auto attempt = eld::make_connection_attempt(socket,
                                                asio::steady_timer(context),
                                                [&attempts](const asio::error_code &errorCode) -> bool
                                                {
                                                    EXPECT_EQ(errorCode, asio::error::timed_out);
                                                    --attempts;
                                                    return false;
                                                });

    auto res = attempt(asio::ip::tcp::endpoint(
            asio::ip::make_address_v4("127.0.0.1"), 12000),
                       attempts,
                       std::chrono::milliseconds(20));

    auto runContext = std::async(std::launch::async,
                                 [&]()
                                 {
                                     context.run();
                                 });

    ASSERT_EQ(attempts, 0);
}

// TODO: 2 timeouts with infinite attempts
// TODO: cancel with onError and infinite attempts
// TODO: call connect twice while pending
// TODO: call connect when already connected

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}