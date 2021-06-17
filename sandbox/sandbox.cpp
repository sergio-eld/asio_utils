
#include "../test/test_utils.h"
#include "asio_utils/connection_adapter.hpp"
#include "asio_utils/connection_tools.hpp"

#include <numeric>

using namespace eld;

int main()
{
    std::vector<uint32_t> sequence{ 1, 2, 3, 4, 5, 6, 7, 8 };
    auto subRanges = testing::divide_range(sequence, 3);

    for (const auto &sr : subRanges)
    {
        auto iter = sr.first;
        while (iter != sr.second)
            std::cout << *iter++ << " ";
        std::cout << std::endl;
    }

    std::vector<uint32_t> input{ 2, 3, 0, 1, 0, 1, 3, 50, 51, 52, 53, 4 };

    auto lengths = testing::get_chunk_lengths(input.cbegin(), input.cend());

    asio::io_context context;
    auto res = testing::async_receive_tcp(asio::make_strand(context), 100, asio::use_future);

    context.run();

    try
    {
        auto emptyVec = res.get();
    }
    catch (const std::system_error &errorCode)
    {
        std::cerr << errorCode.what() << std::endl;
    }
    // TODO: run server
    // TODO: refuse connections
    // TODO: accept connections
    // TODO: abort connection
    // TODO: connection time out

    struct StubConnectionA
    {
        using config_type = double;
        void cancel(){}

    };

    struct StubConnectionB
    {
        using config_type = double;
        void cancel(){}

    };

    auto adapter = eld::make_connection_adapter(StubConnectionA(), StubConnectionB());

    return 0;
}