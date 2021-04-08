
#include "asio_utils/connection_tools.hpp"
#include "../test/test_utils.h"
#include <numeric>

using namespace eld;

int main()
{
    std::vector<uint32_t> input{2, 3,
                                0, 1,
                                0, 1,
                                3,
                                50, 51, 52, 53,
                                4};

    auto lengths = testing::get_chunk_lengths(input.cbegin(), input.cend());

    // TODO: run server
    // TODO: refuse connections
    // TODO: accept connections
    // TODO: abort connection
    // TODO: connection time out

    return 0;
}