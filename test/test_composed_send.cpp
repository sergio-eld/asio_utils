
#include <gtest/gtest.h>

#include <cstdint>
#include <vector>
#include <iostream>
#include <numeric>

#include "asio_utils/connection_tools.hpp"

constexpr const char localhost[] = "127.0.0.1";
constexpr uint16_t port = 12000;

class receiver
{
public:

    template<typename Executor>
    explicit receiver(Executor &executor)
            : acceptor_(executor, {asio::ip::make_address_v4(localhost), port}),
              peer_(executor)
    {
        using asio::ip::tcp;
        acceptor_.set_option(tcp::acceptor::reuse_address(true));
    }

    template<typename Executor>
    explicit receiver(Executor executor)
            : acceptor_(executor, {asio::ip::make_address_v4(localhost), port}),
              peer_(executor)
    {
        using asio::ip::tcp;
        acceptor_.set_option(tcp::acceptor::reuse_address(true));
    }

    void start()
    {
        acceptor_.listen();
        acceptor_.async_accept(peer_, [this](const asio::error_code &errorCode)
        {
            if (!errorCode)
                return startReadLoop();

            std::cerr << "Server error: " << errorCode.message() << std::endl;
        });
    }

    void stop()
    {
        peer_.close();
        acceptor_.close();
    }

    void getData(std::vector<uint8_t> &data)
    {
        data = std::move(dataReceived_);
    }

private:

    void startReadLoop()
    {
        peer_.async_receive(asio::buffer(readBuffer_),
                            [this](const asio::error_code &errorCode,
                                   size_t bytesReceived)
                            {
                                if (!errorCode)
                                {
                                    std::copy(readBuffer_.cbegin(),
                                              std::next(readBuffer_.cbegin(), bytesReceived),
                                              std::back_inserter(dataReceived_));
                                    return startReadLoop();
                                }

                                if (errorCode == asio::error::operation_aborted)
                                    return;

                                std::cerr << "Server error: " << errorCode.message() << std::endl;

                            });
    }

    asio::ip::tcp::acceptor acceptor_;
    asio::ip::tcp::socket peer_;

    std::vector<uint8_t> readBuffer_ = std::vector<uint8_t>(2048);
    std::vector<uint8_t> dataReceived_;
};

TEST(compose_send, success_sample)
{
    using asio::ip::tcp;

    asio::thread_pool context{2};

    receiver receiver{asio::make_strand(context)};

    receiver.start();

    constexpr size_t arraySize = 256;

    std::vector<uint8_t> dataSent(arraySize),
        dataReceived{};
    std::iota(dataSent.begin(), dataSent.end(), 0);

    tcp::socket client{context};

    client.async_connect({asio::ip::make_address_v4(localhost), port}, asio::use_future).wait();
    asio::async_write(client, asio::buffer(dataSent), asio::use_future).wait();

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    receiver.stop();

    context.wait();

    receiver.getData(dataReceived);

    ASSERT_EQ(dataSent, dataReceived);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
