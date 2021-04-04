
#include "asio_ct/connection_tools.hpp"

using namespace eld;

int main()
{
    asio::io_context io_context{};

    auto runContext = [&io_context]()
    {
        std::string threadId{};
        std::stringstream ss;
        ss << std::this_thread::get_id();
        ss >> threadId;
        std::cout << std::string("New thread for asio context: ")
                     + threadId + "\n";
        std::cout.flush();

        io_context.run();

        std::cout << std::string("Stopping thread: ")
                     + threadId + "\n";
        std::cout.flush();

    };

    asio::ip::tcp::socket client{io_context};

    auto composedAttempt = make_composed_connection_attempt(client,
                                                            [](const asio::error_code &) {},
                                                            [](const asio::error_code &) -> bool { return false; });

    std::future<void> future = async_connection_attempt(client,
                             asio::ip::tcp::endpoint(asio::ip::make_address_v4("127.0.0.1"),
                                                     12000),
                             1,
                             std::chrono::milliseconds(20),
                             asio::use_future,
                             [](const asio::error_code &) -> bool
                             {
                                 return false;
                             });

    auto connectionAttempt = make_connection_attempt(client,
                                                     [](const asio::error_code &errorCode)
                                                     {
                                                         std::cout << errorCode.message() << std::endl;
                                                         return false;
                                                     });


    auto res = connectionAttempt(asio::ip::tcp::endpoint(asio::ip::make_address_v4("127.0.0.1"),
                                                         12000),
                                 std::chrono::milliseconds(20));

    auto threads = {
            std::async(std::launch::async, runContext),
            std::async(std::launch::async, runContext)
    };

    // TODO: run server
    // TODO: refuse connections
    // TODO: accept connections
    // TODO: abort connection
    // TODO: connection time out

    return 0;
}