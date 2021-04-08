#pragma once

#include <asio.hpp>
#include <vector>
#include <cstdint>
#include <numeric>

namespace eld
{
    namespace testing
    {
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

//    template <typename Executor>
//    explicit receiver(Executor &executor)

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

            size_t bytesReceived() const
            {
                return dataReceived_.size();
            }

            template <typename T>
            void getData(std::vector<T> &data)
            {
                const auto *received = reinterpret_cast<const T*>(dataReceived_.data());
                std::copy(received, std::next(received, dataReceived_.size() / sizeof(T)),
                          std::back_inserter(data));
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

        template<typename RandomIter>
        std::vector<size_t> get_chunk_lengths(RandomIter begin, RandomIter end)
        {
            const auto distance = (size_t) std::distance(begin, end);
            if (distance < 2)
                return {distance};

            std::vector<size_t> lengths{};

            // sequence iterator that stores the beginning of the subsequence
            auto subSeqBegin = begin,
                    prev = begin,
                    iter = begin;

            while (++iter != end)
            {
                // if next is not greater than prev by one, start new chunk
                const int prevVal = int(*prev),
                        curVal = int(*iter),
                        diff = curVal - prevVal;
                if (diff != 1)
                {
                    lengths.emplace_back(std::distance(subSeqBegin, iter));
                    subSeqBegin = iter;
                }

                prev = iter;
            }
            lengths.emplace_back(std::distance(subSeqBegin, iter));

            return lengths;
        }

        template <typename Integral>
        std::vector<Integral> make_increasing_range(Integral start, size_t length)
        {
            auto vec = std::vector<Integral>(length);
            std::iota(vec.begin(), vec.end(), start);
            return vec;
        }
    }
}


