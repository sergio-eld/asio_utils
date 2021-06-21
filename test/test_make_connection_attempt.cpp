
#include <gtest/gtest.h>

#include "asio_utils/connection_tools.hpp"

constexpr const char localhost[] = "127.0.0.1";
constexpr unsigned short port = 12000;

class server
{
public:
    template<typename Executor>
    explicit server(Executor &executor) : acceptor_(executor)
    {
        using asio::ip::tcp;
        auto endpoint = tcp::endpoint(asio::ip::make_address_v4(localhost), port);
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(tcp::acceptor::reuse_address(true));
        acceptor_.bind(endpoint);
        acceptor_.listen();
    }

    void startAccepting()
    {
        accepting_ = true;
        acceptor_.async_accept(
            [this](const asio::error_code &errorCode, asio::ip::tcp::socket /*peer*/)
            {
                if (!errorCode)
                {
                    if (!accepting_)
                        return;
                    startAccepting();
                }
                if (errorCode == asio::error::operation_aborted)
                {
                    return;
                }
            });
    }

    void startRejecting()
    {
        // TODO: how to reject?
    }

    void stop()
    {
        accepting_ = false;
        std::thread([this]() { acceptor_.cancel(); }).detach();
        // acceptor_.cancel();
    }

private:
    asio::ip::tcp::acceptor acceptor_;
    std::atomic_bool accepting_{ false };

    asio::ip::tcp::socket peer_{ acceptor_.get_executor() };
};

void runContext(asio::io_context &io_context)
{
    std::string threadId{};
    std::stringstream ss;
    ss << std::this_thread::get_id();
    ss >> threadId;
    std::cout << std::string("New thread for asio context: ") + threadId + "\n";
    std::cout.flush();

    io_context.run();

    std::cout << std::string("Stopping thread: ") + threadId + "\n";
    std::cout.flush();
}

// cancel on 15 timeouts
TEST(connection_attempt, 15_timeouts)
{
    asio::io_context context;
    asio::ip::tcp::socket socket{ context };
    size_t timeouts = 15;

    std::future<void> result = eld::async_connection_attempt(
        socket,
        asio::ip::tcp::endpoint(asio::ip::make_address_v4(localhost), port),
        15,
        std::chrono::milliseconds(20),
        asio::use_future,
        [&timeouts](const asio::error_code &errorCode) -> bool
        {
            EXPECT_EQ(errorCode, asio::error::timed_out);
            --timeouts;
            return false;
        });

    auto runContext = std::async(std::launch::async, [&]() { context.run(); });
    result.wait();

    ASSERT_EQ(timeouts, 0);
}

// cancel with onError and infinite attempts
TEST(connection_attempt, interrupted_infinite_attempts)
{
    asio::io_context context;
    asio::ip::tcp::socket socket{ context };
    size_t timeouts = 15;
    auto endpoint = asio::ip::tcp::endpoint(asio::ip::make_address_v4(localhost), port);
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

    auto runContext = std::async(std::launch::async, [&]() { context.run(); });
    res.wait();

    ASSERT_EQ(timeouts, 0);
}

TEST(connection_attempt, connection_succeeded)
{
    asio::io_context context;

    // run server
    auto serverStrand = asio::make_strand(context);
    server server{ serverStrand };
    server.startAccepting();

    // run client
    auto clientStrand = asio::make_strand(context);
    asio::ip::tcp::socket socket{ clientStrand };

    size_t attempts = 1;
    auto endpoint = asio::ip::tcp::endpoint(asio::ip::make_address_v4(localhost), port);

    std::future<void> res =
        eld::async_connection_attempt(socket,
                                      std::move(endpoint),
                                      std::chrono::seconds(1),
                                      asio::use_future,
                                      [&attempts](const asio::error_code &errorCode) -> bool
                                      {
                                          EXPECT_EQ(errorCode, asio::error_code());
                                          return !--attempts;
                                      });

    std::future<void> runningContexts[] = {
        std::async(std::launch::async, runContext, std::ref(context)),
        std::async(std::launch::async, runContext, std::ref(context))
    };

    ASSERT_NO_THROW(res.get());

    server.stop();
    ASSERT_EQ(attempts, 1);
}
// TODO: rejected connection

// TODO: call connect twice while pending
// TODO: call connect when already connected

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}