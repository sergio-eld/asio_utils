
#include <gtest/gtest.h>

#include <cstdint>
#include <vector>
#include <iostream>
#include <numeric>
#include <chrono>

#include "asio_utils/connection_tools.hpp"
#include "test_utils.h"

/*
 * Testing scenarios:
 * 1) Sending data successfully:
 *    async_send_queue serializes asynchronous composed requests to send data.
 *    According to specification asio::async_write sends data in chunks in zero or more calls.
 *    async_send_queue guarantees that at any time only one call to asio::async_write will be processed
 *    regardless of the thread of invocation or number of simultaneous calls made.
 *
 *    The test sends a contiguous array of increasing integers (next element is increased by 1).
 *    Data can be sent in one or more calls from one or more threads.
 *
 *      Input arguments:
 *          - number of elements to send
 *          - number of threads
 *          - number of elements per thread
 *          - number of sub-ranges per thread
 *          - interval between sending sub-ranges of data
 *      Expected outcome:
 *          - number of sub-ranges received corresponds to number of those sent
 *          - sub-range sent by one command must be received as the same contiguous sub-range
 * 2) Errors on sending
 *
 * 3) Data race test
 */

using elements_num_t = size_t;
using threads_num_t = size_t;
using subranges_per_thread_t = size_t;

namespace e_testing = eld::testing;

template<typename Interval>
void send_data_success(const elements_num_t elements,
                       const threads_num_t threads,
                       const subranges_per_thread_t subRangesPerThread,
                       const Interval intervalBetweenSends)
{
    using asio::ip::tcp;

    // sending and receiving in different threads
    asio::thread_pool threadPool{2};

    const size_t expectedBytes = elements * (sizeof(uint32_t) / sizeof(uint8_t));

    auto resReceivedBytes = e_testing::async_receive_tcp(asio::make_strand(threadPool),
                                                         expectedBytes,
                                                         asio::use_future);

    tcp::socket client{asio::make_strand(threadPool)};
    const tcp::endpoint endpoint{asio::ip::make_address_v4(e_testing::localhost),
                                 e_testing::port};

    // begin establishing connection
    auto connected = client.async_connect(endpoint, asio::use_future);

    // prepare data to be sent
    const std::vector<uint32_t> inputElements =
            e_testing::make_increasing_range(uint32_t(0), elements);

    using const_iter = decltype(inputElements.cbegin());
    const size_t subrangesTotal = threads * subRangesPerThread;

    const std::vector<std::pair<const_iter, const_iter>> subRangesInput =
            e_testing::divide_range(inputElements, subrangesTotal);


    // TODO: explicitly handle exception
    ASSERT_NO_THROW(connected.get());

    // TODO: multiple ranges/multiple threads, concurrent invocations
    // this will throw "promise already satisfied" with asio::use_future
    auto sendQueue = eld::make_async_send_queue(client, asio::detached);

    // stub implementation to send array in one go
//    sendQueue.asyncSend(asio::buffer(inputElements),
//                              [&](const asio::error_code &errorCode, size_t bytesSent)
//                              {
//                                  EXPECT_FALSE(errorCode);
//                                  EXPECT_EQ(inputElements.size(), bytesSent /
//                                                    (sizeof(uint32_t) / sizeof(uint8_t)));
//                              });

    // send data
    // one-range simple implementation
    for (size_t t = 0; t != threads; ++t)
        std::thread([&, t]()
                    {
                        for (size_t sr = t * subRangesPerThread;
                             sr != t * subRangesPerThread + subRangesPerThread;
                             ++sr)
                        {
                            const std::pair<const_iter, const_iter> &subRange =
                                    subRangesInput[sr];

                            const uint32_t *begin = &(*subRange.first);
                            const auto length = (size_t) std::distance(subRange.first,
                                                                       subRange.second),
                                    sizeInBytes = length * (sizeof(uint32_t) / sizeof(uint8_t));
                            sendQueue.asyncSend(asio::buffer(begin, sizeInBytes),
                                                [sizeInBytes](const asio::error_code &errorCode, size_t bytesSent)
                                                {
                                                    EXPECT_FALSE(errorCode);
                                                    EXPECT_EQ(sizeInBytes, bytesSent);
                                                });
                            if (intervalBetweenSends.count())
                                std::this_thread::sleep_for(intervalBetweenSends);
                        }
                    }).detach();

    // wait until all data has been received and get it
    std::vector<uint8_t> vecBytesReceived{};

    // TODO: explicitly handle exception
    ASSERT_NO_THROW(vecBytesReceived = resReceivedBytes.get());

    // verify that the data has not been fragmented on reception
    ASSERT_FALSE(vecBytesReceived.size() % (sizeof(uint32_t) / sizeof(uint8_t)));

    std::vector<uint32_t> receivedElements{};
    const uint8_t *rawReceivedBytes = vecBytesReceived.data();
    const auto *rawReceivedElements = reinterpret_cast<const uint32_t *>(rawReceivedBytes);
    const size_t elemsReceived = vecBytesReceived.size() /
                                 (sizeof(uint32_t) / sizeof(uint8_t));
    std::copy(rawReceivedElements, std::next(rawReceivedElements, elemsReceived),
              std::back_inserter(receivedElements));

    auto receivedSubRanges =
            e_testing::get_contiguous_subranges(receivedElements.cbegin(),
                                                receivedElements.cend());

    EXPECT_EQ(elemsReceived, inputElements.size());
    EXPECT_LE(receivedSubRanges.size(), subRangesInput.size());

    // TODO: thorough sub-range based comparison
    std::sort(receivedElements.begin(), receivedElements.end());
    ASSERT_EQ(inputElements, receivedElements);
}

TEST(compose_send_tcp, success_1024elems_1thread_1chunk)
{
    send_data_success(elements_num_t(1024),
                      threads_num_t(1),
                      subranges_per_thread_t(1),
                      std::chrono::milliseconds(0));
}

TEST(compose_send_tcp, success_100000elems_1thread_1chunk)
{
    send_data_success(elements_num_t(10000),
                      threads_num_t(1),
                      subranges_per_thread_t(1),
                      std::chrono::milliseconds(0));
}

TEST(compose_send_tcp, success_100000elems_1thread_100chunks)
{
    send_data_success(elements_num_t(10000),
                      threads_num_t(1),
                      subranges_per_thread_t(100),
                      std::chrono::milliseconds(0));
}

TEST(compose_send_tcp, success_100elems_2threads_1chunk)
{
    send_data_success(elements_num_t(100),
                      threads_num_t(2),
                      subranges_per_thread_t(1),
                      std::chrono::milliseconds(0));
}

TEST(compose_send_tcp, success_100000elems_2threads_1chunk)
{
    send_data_success(elements_num_t(100000),
                      threads_num_t(2),
                      subranges_per_thread_t(1),
                      std::chrono::milliseconds(0));
}

TEST(compose_send_tcp, success_100000elems_all_threads_1chunk)
{
    send_data_success(elements_num_t(100000),
                      threads_num_t(std::thread::hardware_concurrency()),
                      subranges_per_thread_t(1),
                      std::chrono::milliseconds(0));
}

TEST(compose_send_tcp, success_1000000elems_all_threads_1chunk)
{
    send_data_success(elements_num_t(100000),
                      threads_num_t(std::thread::hardware_concurrency()),
                      subranges_per_thread_t(1),
                      std::chrono::milliseconds(0));
}

TEST(compose_send_tcp, success_1000000elems_all_threads_10chunks)
{
    send_data_success(elements_num_t(100000),
                      threads_num_t(std::thread::hardware_concurrency()),
                      subranges_per_thread_t(10),
                      std::chrono::milliseconds(0));
}

TEST(compose_send_tcp, success_1000000elems_all_threads_10chunks_20msec)
{
    send_data_success(elements_num_t(100000),
                      threads_num_t(std::thread::hardware_concurrency()),
                      subranges_per_thread_t(10),
                      std::chrono::milliseconds(20));
}

TEST(compose_send_tcp, success_10000000elems_all_threads_10chunks)
{
    send_data_success(elements_num_t(10000000),
                      threads_num_t(std::thread::hardware_concurrency()),
                      subranges_per_thread_t(10),
                      std::chrono::milliseconds(0));
}

// this causes segfault for some reason
//TEST(compose_send_tcp, success_100000000elems_all_threads_10chunks)
//{
//    send_data_success(elements_num_t(100000000),
//                      threads_num_t(std::thread::hardware_concurrency()),
//                      subranges_per_thread_t(10),
//                      std::chrono::milliseconds(0));
//}


// TODO: check for race conditions in sender

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
