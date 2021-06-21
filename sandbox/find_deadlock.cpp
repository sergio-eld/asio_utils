
#include <asio.hpp>
#include <functional>
#include <iostream>
#include <sstream>

constexpr const char localhost[] = "127.0.0.1";
constexpr unsigned short port = 12000;

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

class server
{
public:
    template<typename Executor>
    explicit server(Executor executor) : acceptor_(executor)
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
        std::cout << "Start accepting\n";
        accepting_ = true;
        acceptor_.async_accept(
            [this](const asio::error_code &errorCode, asio::ip::tcp::socket /*peer*/)
            {
                std::cout << "accepted: " << errorCode.message() << " "
                          << std::this_thread::get_id() << std::endl;
                if (!errorCode)
                {
                    //                        if (!accepting_)
                    //                            return;
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
        using asio::ip::tcp;
        //        acceptor_.cancel();
        //        accepting_ = false;
        //  accepting_ = false;
        //         acceptor_.cancel();
        // std::thread{[this](){acceptor_.cancel();}}.detach();
        // accepting_ = false;
        asio::post(acceptor_.get_executor(),
                   [this]() { acceptor_.cancel(); });   // acceptor_.cancel();
        // acceptor_.close(); // this line also fixes deadlock
    }

private:
    asio::ip::tcp::acceptor acceptor_;
    std::atomic_bool accepting_{ false };
};

int main()
{
    setvbuf(stdout, NULL, _IONBF, 0);
    asio::io_context context;

    // run server
    auto serverStrand = asio::make_strand(context);
    server server{ serverStrand };
    //    server server{asio::make_strand(context)};
    server.startAccepting();

    // run client
    //    auto clientStrand = asio::make_strand(context);
    //    asio::ip::tcp::socket socket{clientStrand};
    asio::ip::tcp::socket socket{ asio::make_strand(context) };

    auto endpoint = asio::ip::tcp::endpoint(asio::ip::make_address_v4(localhost), port);

    std::future<void> res = socket.async_connect(endpoint, asio::use_future);

    std::future<void> runningContexts[] = {
        std::async(std::launch::async, runContext, std::ref(context)),
        std::async(std::launch::async, runContext, std::ref(context))
    };

    res.get();
    server.stop();
    std::cout << "Server has been requested to stop" << std::endl;

    return 0;
}
