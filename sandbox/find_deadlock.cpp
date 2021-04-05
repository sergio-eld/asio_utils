
#include <asio.hpp>
#include <iostream>
#include <sstream>
#include <functional>

constexpr const char localhost[] = "127.0.0.1";
constexpr unsigned short port = 12000;

void runContext(asio::io_context &io_context)
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

class server
{
public:

    template<typename Executor>
    explicit server(Executor &executor)
            : acceptor_(executor)
    {
        using asio::ip::tcp;
        auto endpoint = tcp::endpoint(asio::ip::make_address_v4(localhost),
                                      port);
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(tcp::acceptor::reuse_address(true));
        acceptor_.bind(endpoint);
        acceptor_.listen();
    }

    void startAccepting()
    {
        acceptor_.async_accept(
                [this](const asio::error_code &errorCode,
                       asio::ip::tcp::socket peer)
                {
                    if (!errorCode)
                    {
                        startAccepting();
                        std::cout << "Connection accepted:" << peer.remote_endpoint() << std::endl;
                    }
                    if (errorCode == asio::error::operation_aborted)
                    {
                        std::cout << "Stopped accepting connections\n";
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
        // asio::post(acceptor_.get_executor(), [this](){acceptor_.cancel();}); // this line fixes deadlock
        acceptor_.cancel();
        // acceptor_.close(); // this line also fixes deadlock
    }

private:
    asio::ip::tcp::acceptor acceptor_;
};

int main()
{
    setvbuf(stdout, NULL, _IONBF, 0);
    asio::io_context context;

    // run server
    auto serverStrand = asio::make_strand(context);
    server server{serverStrand};
    server.startAccepting();

    // run client
    auto clientStrand = asio::make_strand(context);
    asio::ip::tcp::socket socket{clientStrand};

    size_t attempts = 1;
    auto endpoint = asio::ip::tcp::endpoint(
            asio::ip::make_address_v4(localhost), port);

    std::future<void> res = socket.async_connect(endpoint, asio::use_future);

    std::future<void> runningContexts[] = {
            std::async(std::launch::async, runContext, std::ref(context)),
            std::async(std::launch::async, runContext, std::ref(context))
    };

    res.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    server.stop();
    std::cout << "Server has been requested to stop" << std::endl;

    return 0;
}
