
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
using elements_per_thread_t = size_t;
using subranges_per_thread_t = size_t;

namespace e_testing = eld::testing;

template <typename Interval>
void send_data_success(elements_num_t elements,
                       threads_num_t threads,
                       elements_per_thread_t elemsPerThread,
                       subranges_per_thread_t subRangesNum,
                       Interval intervalBetweenSends)
{
    // sending and receiving in different threads
    asio::thread_pool threadPool{2};

    e_testing::receiver receiver{asio::make_strand(threadPool)};


}

TEST(compose_send_tcp, success_sample)
{
    using asio::ip::tcp;

    asio::thread_pool context{2};

    eld::testing::receiver receiver{asio::make_strand(context)};

    receiver.start();

    constexpr size_t arraySize = 256;

    std::vector<uint8_t> dataSent(arraySize),
            dataReceived{};
    std::iota(dataSent.begin(), dataSent.end(), 0);

    tcp::socket client{context};

    client.async_connect({asio::ip::make_address_v4(eld::testing::localhost), eld::testing::port}, asio::use_future).wait();
    asio::async_write(client, asio::buffer(dataSent), asio::use_future).wait();

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    receiver.stop();

    context.wait();

    receiver.getData(dataReceived);

    ASSERT_EQ(dataSent, dataReceived);
}

TEST(compose_send_tcp, single_send_success)
{
    using asio::ip::tcp;

    asio::thread_pool context{2};

    eld::testing::receiver receiver{asio::make_strand(context)};

    receiver.start();

    constexpr size_t arraySize = 256;

    std::vector<uint8_t> dataSent(arraySize),
            dataReceived{};
    std::iota(dataSent.begin(), dataSent.end(), 0);

    tcp::socket client{context};
    client.async_connect({asio::ip::make_address_v4(eld::testing::localhost), eld::testing::port}, asio::use_future).wait();

    auto asyncSendQueueTuple =
            eld::make_async_send_queue(client, asio::use_future);
    size_t bytesSent = 0;
    asio::error_code sentError{};

    // send data
    eld::unwrap(asyncSendQueueTuple).
            asyncSend(asio::buffer(dataSent), [&](const asio::error_code errorCode, size_t sent)
    {
        bytesSent = sent;
        sentError = errorCode;
        return false;
    });

    // wait data to be sent
    std::get<1>(asyncSendQueueTuple).get();

    // wait data to be received
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    receiver.stop();

    context.wait();

    receiver.getData(dataReceived);

    ASSERT_EQ(dataSent, dataReceived);
    ASSERT_EQ(bytesSent, arraySize);
    ASSERT_FALSE(sentError);
}

TEST(compose_send_tcp, big_array_send_one_go)
{
    using asio::ip::tcp;

    asio::thread_pool context{2};

    eld::testing::receiver receiver{asio::make_strand(context)};

    receiver.start();

    constexpr size_t arraySize = 1000000;

    std::vector<uint8_t> dataSent(arraySize),
            dataReceived{};
    std::iota(dataSent.begin(), dataSent.end(), 0);

    tcp::socket client{context};
    client.async_connect({asio::ip::make_address_v4(eld::testing::localhost), eld::testing::port}, asio::use_future).wait();

    auto asyncSendQueueTuple =
            eld::make_async_send_queue(client, asio::use_future);
    size_t bytesSent = 0;
    asio::error_code sentError{};

    // send data
    eld::unwrap(asyncSendQueueTuple).
            asyncSend(asio::buffer(dataSent), [&](const asio::error_code errorCode, size_t sent)
    {
        bytesSent = sent;
        sentError = errorCode;
        return false;
    });

    // wait data to be sent
    std::get<1>(asyncSendQueueTuple).get();

    // wait data to be received
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    receiver.stop();

    context.wait();

    receiver.getData(dataReceived);

    std::cout << "Sent: " << dataSent.size() <<
              " received: " << dataReceived.size() << std::endl;
    ASSERT_EQ(dataSent, dataReceived);
    ASSERT_EQ(bytesSent, arraySize);
    ASSERT_FALSE(sentError);
}

TEST(compose_send_tcp, big_array_send_multiple_attempts_threadsafe)
{
    using asio::ip::tcp;

    asio::thread_pool context{2};

    eld::testing::receiver receiver{asio::make_strand(context)};

    receiver.start();

    constexpr size_t arraySize = 1000000;

    std::vector<uint8_t> dataSent(arraySize),
            dataReceived{};
    std::iota(dataSent.begin(), dataSent.end(), 0);

    tcp::socket client{context};
    client.async_connect({asio::ip::make_address_v4(eld::testing::localhost), eld::testing::port}, asio::use_future).wait();

    auto asyncSendQueueTuple =
            eld::make_async_send_queue(client, asio::use_future);
    size_t bytesSent = 0;
    asio::error_code sentError{};

    constexpr size_t chunks = 1000,
            chunkSize = arraySize / chunks;
    for (size_t i = 0; i != arraySize;)
    {
        const size_t toSend = i + chunkSize <= arraySize ?
                              chunkSize :
                              arraySize - i;

        eld::unwrap(asyncSendQueueTuple).
                asyncSend(asio::buffer(std::next(dataSent.data(), i), toSend),
                          [&](const asio::error_code errorCode, size_t sent)
                          {
                              bytesSent += sent;
                              EXPECT_FALSE(errorCode);
                              EXPECT_EQ(toSend, sent);
                              return false;
                          });
        i += toSend;
    }

    // wait data to be sent
    std::get<1>(asyncSendQueueTuple).get();

    // wait data to be received
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    receiver.stop();

    context.wait();

    receiver.getData(dataReceived);

    std::cout << "Sent: " << dataSent.size() <<
              " received: " << dataReceived.size() << std::endl;
    ASSERT_EQ(dataSent, dataReceived);
    ASSERT_EQ(bytesSent, arraySize);
}


TEST(compose_send_tcp, big_array_send_one_chunk_per_thread)
{
    using asio::ip::tcp;

    asio::thread_pool context{2};

    eld::testing::receiver receiver{asio::make_strand(context)};

    receiver.start();

    constexpr size_t arraySize = 100;
    constexpr size_t elementToByteRatio = sizeof(uint32_t) / sizeof (uint8_t);

    std::vector<uint32_t> dataSent(arraySize),
            dataReceived{};
    std::iota(dataSent.begin(), dataSent.end(), 0);

    tcp::socket client{context};
    client.async_connect({asio::ip::make_address_v4(eld::testing::localhost), eld::testing::port}, asio::use_future).wait();

    std::atomic<size_t> queueEmptied_{0};
    auto asyncSendQueueTuple =
            eld::make_async_send_queue(client, [&queueEmptied_](asio::error_code)
            {
//                std::cout << "Thread id: " << std::this_thread::get_id() << std::endl;
//                std::cout << "Send queue has been emptied: " << ++queueEmptied_ << std::endl;
//                std::cout << "Thread id: " << std::this_thread::get_id() << std::endl;

            });
    std::atomic<size_t> bytesSent{0};


    const size_t chunks = std::thread::hardware_concurrency(),
            chunkSize = arraySize / chunks;

    std::vector<size_t> chunksSent{};
    for (size_t i = 0; i != arraySize;)
    {
        const size_t elementsToSend = i + chunkSize <= arraySize ?
                                      chunkSize :
                              arraySize - i;

        chunksSent.emplace_back(elementsToSend);
        std::thread([&]()
                    {
                        eld::unwrap(asyncSendQueueTuple).
                                asyncSend(asio::buffer(std::next(dataSent.data(), i), elementsToSend),
                                          [&](const asio::error_code errorCode, size_t sent)
                                          {
                                              bytesSent += sent;
                                              EXPECT_FALSE(errorCode);
                                              EXPECT_EQ(elementsToSend * elementToByteRatio, sent);
                                              return false;
                                          });
                    }).detach();
        i += elementsToSend;
    }

    // TODO: block and wait for data to be sent
    // TODO: block and wait for data to be received

    // wait data to be received
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    receiver.stop();

    context.wait();
    EXPECT_EQ(receiver.bytesReceived() % sizeof(uint32_t), 0);

    receiver.getData(dataReceived);

    // analyze chunks
    auto receivedLengths = eld::testing::get_chunk_lengths(dataReceived.cbegin(),
                                                           dataReceived.cend());
    using const_iter = decltype(dataReceived.cbegin());
    std::vector<std::pair<const_iter, const_iter>> rangesSent,
        rangesReceived;


    std::cout << "Sent: " << dataSent.size() <<
              " received: " << dataReceived.size() << std::endl;
    ASSERT_EQ(dataSent, dataReceived);
    ASSERT_EQ(bytesSent, arraySize * elementToByteRatio);
}

// TODO: check for race conditions in sender

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
