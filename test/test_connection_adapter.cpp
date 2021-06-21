
#include "asio_utils/connection_adapter.hpp"
#include "asio_utils/testing/stub_connection.h"

#include <asio.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <numeric>
#include <vector>

namespace e_testing = eld::testing;

template<typename ConnectionT>
class connection_wrapper
{
public:
    using connection_type = ConnectionT;
    using config_type = typename eld::traits::connection<connection_type>::config_type;

    explicit connection_wrapper(connection_type &connection)   //
      : connection_(connection)
    {
    }

    template<typename ConstBuffer, typename CompletionT>
    auto async_send(ConstBuffer &&constBuffer, CompletionT &&a_completion)
    {
        return connection_.async_send(std::forward<ConstBuffer>(constBuffer),
                                      std::forward<CompletionT>(a_completion));
    }

    template<typename ConstBuffer, typename CompletionT>
    auto async_receive(ConstBuffer &&constBuffer, CompletionT &&a_completion)
    {
        return connection_.async_receive(std::forward<ConstBuffer>(constBuffer),
                                         std::forward<CompletionT>(a_completion));
    }

    void configure(const config_type &config) { connection_.configure(config); }

    config_type get_config() const { return connection_.get_config(); }

    void cancel() { connection_.cancel(); }

private:
    connection_type &connection_;
};

template<typename ConnectionT>
connection_wrapper<ConnectionT> wrap_connection(ConnectionT &connection)
{
    return connection_wrapper<ConnectionT>(connection);
}

/*
 * destination (to emulate receiving) -> source -> adapter -> destination -> source (to emulate
 * sending)
 * 1. make connections
 * 2. make wrappers to send and receive
 * 3. make adapter from wrappers
 * 4. send data with future
 * 5. wait for data with future
 * 6. compare data
 */

TEST(connection_adapter, stub_connection)
{
    using namespace e_testing;

    std::vector<uint32_t> send(size_t(512)),   //
        receive(size_t(512));

    std::iota(send.begin(), send.end(), 0);

    asio::thread_pool context{ 2 };

    stub_connection connectionSourceRemote{ context, stub_config{ 0, 1 } },
        connectionSource{ context, stub_config{ 1, 2 } },
        connectionDest{ context, stub_config{ 2, 3 } },
        connectionDestRemote{ context, stub_config{ 3, 4 } };

    connectionSourceRemote.set_remote_host(connectionSource);
    connectionDest.set_remote_host(connectionDestRemote);

    {
        auto adapter = eld::make_connection_adapter(wrap_connection(connectionSource),
                                                    wrap_connection(connectionDest));
        adapter.async_run(eld::direction::a_to_b,
                          [](const asio::error_code &errorCode) {   //
                              EXPECT_EQ(errorCode, asio::error_code());
                          });

        auto futureSent = connectionSourceRemote.async_send(asio::buffer(send), asio::use_future);
        auto futureReceive =
            connectionDestRemote.async_receive(asio::buffer(receive), asio::use_future);

        // why does future not block on destruction?
        futureSent.get();
        futureReceive.get();
    }
    std::cout << "end of receiving data" << std::endl;

    ASSERT_EQ(send, receive);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}