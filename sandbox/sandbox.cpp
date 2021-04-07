
#include "asio_utils/connection_tools.hpp"

using namespace eld;

int main()
{

    using signature_t = void(asio::error_code, bool);

    // handler to be initiated and passed to a composed asynchronous call
    eld::chained_continuation_handler<signature_t> continuationHandler{
            eld::use_chained_completion};

    // result to be deduced and returned from a composed asynchronous call
    chained_continuation<signature_t> chainedContinuation{continuationHandler};

    auto continuation =
    asio::async_result < eld::chained_completion_t, signature_t > ::initiate(
            [](eld::chained_continuation_handler<signature_t> &&handler) {},
            eld::use_chained_completion);

    asio::io_context context;
    asio::ip::tcp::socket socket{context};
    std::vector<uint8_t> vec;

    // chainedWrite must be invocable with deduced signature and itself should
    // deduce next signature
    auto chainedWrite =
            asio::async_write(socket, asio::buffer(vec), eld::use_chained_completion);

    // next async_write must accept

    //    continuationImpl(asio::error::operation_aborted, false);
    //    continuationImpl.assign_continuation([](const asio::error_code&
    //    errorCode, bool b)
    //                                         {
    //        std::cout << errorCode.message() << std::endl <<
    //        std::boolalpha << b << std::endl;
    //                                         });

    // TODO: run server
    // TODO: refuse connections
    // TODO: accept connections
    // TODO: abort connection
    // TODO: connection time out

    return 0;
}