#pragma once

#include <asio.hpp>
#include <vector>
#include <cstdint>
#include <numeric>
#include <chrono>
#include <cmath>

namespace eld
{
    namespace testing
    {
        constexpr const char localhost[] = "127.0.0.1";
        constexpr uint16_t port = 12000;

        namespace detail
        {
            template<typename Signature, typename Callable>
            using require_signature_t = typename std::enable_if<
                    std::is_constructible<std::function<Signature>, Callable>::value
            >::type;

            // moved out of the class to ease debugging
            template<typename T, T>
            struct status_tag
            {
            };

            // moved out of the class to ease debugging
            template<typename T, T>
            struct timeout_tag
            {
            };

            template<typename CompletionHandler>
            struct composed_receive_data_tcp :
                    std::enable_shared_from_this<composed_receive_data_tcp<CompletionHandler>>
            {
            public:
                using completion_signature_t = void(asio::error_code, std::vector<uint8_t>);
                using completion_t = CompletionHandler;

                enum class status
                {
                    accepting_peer,
                    receiving_data
                };

                using accepting_peer_t = status_tag<status, status::accepting_peer>;
                using receiving_data_t = status_tag<status, status::receiving_data>;
                using timeout_accepting_t = timeout_tag<status, status::accepting_peer>;
                using timeout_receiving_t = timeout_tag<status, status::receiving_data>;

                constexpr static accepting_peer_t accepting_peer{};
                constexpr static receiving_data_t receiving_data{};
                constexpr static timeout_accepting_t timeout_accepting{};
                constexpr static timeout_receiving_t timeout_receiving{};

                // TODO: make private/protected to prevent stack-allocation
                template<typename Executor,
                        typename CompletionHandlerT,
                        typename = require_signature_t<completion_signature_t, CompletionHandlerT>>
                composed_receive_data_tcp(Executor &&executor,
                                          CompletionHandlerT &&completion)
                        : acceptor_(std::forward<Executor>(executor),
                                    {asio::ip::make_address_v4(localhost), port}),
                          completion_(std::forward<CompletionHandlerT>(completion))
                {}

                // TODO: make private/protected to prevent stack-allocation
                template<typename Executor,
                        typename CompletionHandlerT,
                        typename = require_signature_t<completion_signature_t, CompletionHandlerT>>
                composed_receive_data_tcp(Executor &&executor,
                                          asio::ip::tcp::endpoint endpoint,
                                          CompletionHandlerT &&completion)
                        : acceptor_(std::forward<Executor>(executor),
                                    std::move(endpoint)),
                          completion_(std::forward<CompletionHandlerT>(completion))
                {}


                void operator()(size_t expectedBytes, std::chrono::milliseconds timeout)
                {
                    expectedBytes_ = expectedBytes;
                    timeout_ = timeout;

                    if (!expectedBytes)
                    {
                        completion_(asio::error_code(),
                                    std::vector<uint8_t>());
                        return;
                    }

                    // can't be called form constructor because of shared_from_this()
                    startAccepting();
                }

                // operator for steady_timer::async_wait handling
                template<status S>
                void operator()(timeout_tag<status, S> timeoutTag, const asio::error_code &errorCode)
                {
                    // timer was cancelled
                    if (errorCode == asio::error::operation_aborted)
                        return;

                    // timeout case
                    assert(!errorCode && "Unexpected timer error");
                    cancel(timeoutTag);
                }

                // operator for acceptor::async_accept result handling
                void operator()(accepting_peer_t, const asio::error_code &errorCode)
                {
                    // timeout or other error case
                    if (errorCode)
                    {
                        completion_(errorCode == asio::error::operation_aborted ?
                                    asio::error::timed_out : errorCode,
                                    std::vector<uint8_t>()); // will not compile with {}
                        return;
                    }

                    // success
                    stopTimer();
                    startReceiving();
                }

                // operator for socket::async_read_some handling
                void operator()(receiving_data_t,
                                const asio::error_code &errorCode,
                                size_t bytesRead)
                {
                    // timeout or error
                    if (errorCode)
                    {
                        completion_(errorCode == asio::error::operation_aborted ?
                                    asio::error::timed_out : errorCode,
                                    std::move(dataReceived_));
                        return;
                    }

                    // successful read
                    stopTimer();
                    const auto iterInputBegin = inputBuffer_.cbegin(),
                            iterInputEnd = std::next(iterInputBegin, bytesRead);

                    std::copy(iterInputBegin, iterInputEnd, std::back_inserter(dataReceived_));
                    if (dataReceived_.size() != expectedBytes_)
                    {
                        startReceiving();
                        return;
                    }

                    // finish
                    completion_({}, std::move(dataReceived_));
                }

                ~composed_receive_data_tcp()
                {
                    try
                    {
                        acceptor_.close();
                        peer_.close();
                    }
                    catch (const asio::error_code &errorCode)
                    {
                        std::cerr << "Destructor exception: " <<
                                  errorCode.message() << std::endl;
                    }
                }

            private:
                asio::ip::tcp::acceptor acceptor_;
                completion_t completion_;
                size_t expectedBytes_;
                std::chrono::milliseconds timeout_;

                asio::ip::tcp::socket peer_{acceptor_.get_executor()};

                std::vector<uint8_t> inputBuffer_ = std::vector<uint8_t>(2048);
                std::vector<uint8_t> dataReceived_;
                asio::steady_timer timeoutTimer_{acceptor_.get_executor()};

                void cancel(timeout_accepting_t)
                {
                    acceptor_.cancel();
                }

                void cancel(timeout_receiving_t)
                {
                    peer_.cancel();
                }

                template<status S>
                void startTimer(timeout_tag<status, S>)
                {
                    // automatically cancels the timer's asynchronous wait
                    timeoutTimer_.expires_after(timeout_);

                    timeoutTimer_.async_wait([this, keepAlive =
                    this->shared_from_this()](const asio::error_code &errorCode)
                                             {
                                                 (*this)(timeout_tag<status, S>(), errorCode);
                                             });
                }

                void stopTimer()
                {
                    timeoutTimer_.cancel();
                }

                // use bind instead?
                void startAccepting()
                {
                    using asio::ip::tcp;

                    acceptor_.set_option(tcp::acceptor::reuse_address(true));
                    startTimer(timeout_accepting);

                    acceptor_.async_accept(peer_, [this, keepAlive =
                    this->shared_from_this()](const asio::error_code &errorCode)
                    {
                        (*this)(accepting_peer, errorCode);
                    });
                }

                // use bind instead?
                void startReceiving()
                {
                    startTimer(timeout_receiving);
                    peer_.async_read_some(asio::buffer(inputBuffer_),
                                          [this, sharedPtr =
                                          this->shared_from_this()](const asio::error_code &errorCode,
                                                                    size_t bytesReceived)
                                          {
                                              (*this)(receiving_data, errorCode, bytesReceived);
                                          });
                }
            };
        }

        template<typename CompletionToken,
                typename Executor>
        auto async_receive_tcp(Executor &&executor,
                               asio::ip::tcp::endpoint endpoint,
                               size_t expectedBytes,
                               CompletionToken &&token)
        {
            using asio::ip::tcp;
            using namespace detail;
            using namespace std::chrono_literals;

            using result_t = asio::async_result<std::decay_t<CompletionToken>,
                    void(asio::error_code, std::vector<uint8_t>)>;
            using completion_t = typename result_t::completion_handler_type;
            using composed_procedure_t = composed_receive_data_tcp<completion_t>;

            completion_t completion{std::forward<CompletionToken>(token)};
            result_t result{completion};

            constexpr std::chrono::milliseconds timeout = 200ms;

            auto procedure = std::make_shared<composed_procedure_t>(std::forward<Executor>(executor),
                                                                    std::move(endpoint),
                                                                    std::move(completion));
            (*procedure)(expectedBytes, timeout);

            return result.get();
        }

        template<typename CompletionToken,
                typename Executor>
        auto async_receive_tcp(Executor &&executor,
                               size_t expectedBytes,
                               CompletionToken &&token)
        {
            return async_receive_tcp(std::forward<Executor>(executor),
                    asio::ip::tcp::endpoint(asio::ip::make_address_v4(localhost), port),
                    expectedBytes,
                    std::forward<CompletionToken>(token));
        }


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

            template<typename T>
            void getData(std::vector<T> &data)
            {
                const auto *received = reinterpret_cast<const T *>(dataReceived_.data());
                std::copy(received, std::next(received, dataReceived_.size() / sizeof(T)),
                          std::back_inserter(data));
            }

        private:
            asio::ip::tcp::acceptor acceptor_;
            asio::ip::tcp::socket peer_;
            std::atomic_size_t expectedInput_;

            asio::steady_timer timeoutTimer_;

            std::vector<uint8_t> readBuffer_ = std::vector<uint8_t>(2048);
            std::vector<uint8_t> dataReceived_;

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
        };

        template<typename RandomIter>
        std::vector<std::pair<RandomIter, RandomIter>>
        get_contiguous_subranges(RandomIter begin, RandomIter end)
        {
            using ranges_t = std::vector<std::pair<RandomIter, RandomIter>>;
            const auto distance = (size_t) std::distance(begin, end);
            if (distance < 2)
                return ranges_t();

            ranges_t ranges{};

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
                    ranges.emplace_back(subSeqBegin, iter);
                    subSeqBegin = iter;
                }
                prev = iter;
            }
            ranges.emplace_back(subSeqBegin, iter);
            return ranges;
        }

// TODO: return ranges (begin, end)
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

        template<typename Integral>
        std::vector<Integral> make_increasing_range(Integral start, size_t length)
        {
            auto vec = std::vector<Integral>(length);
            std::iota(vec.begin(), vec.end(), start);
            return vec;
        }

// returns a vector of lengths of consequent sub-ranges
        template<typename Integral>
        std::vector<size_t> make_subranges(const std::vector<Integral> &input, size_t num)
        {
            // input too small to be divided in chunks with more than 1 element
            if (input.size() <= num)
            {
                std::vector<size_t> out(input.size());
                std::fill(out.begin(), out.end(), 1);
                return out;
            }

            auto out = std::vector<size_t>(num);
            const auto elemsPerRange = (size_t) std::ceil(float(input.size()) / float(num));
            std::fill(out.begin(), out.end(), input.size() / elemsPerRange);
            if (input.size() % num)
                out.back() = input.size() % num;

            return out;
        }

        // TODO: handle cases when input.size() < numSubRanges
        template<typename Integral>
        auto divide_range(const std::vector<Integral> &input, size_t numSubRanges)
        {
            assert(numSubRanges && "numSubRanges must be greater than zero!");

            using const_iter = decltype(input.cbegin());
            using sub_range_t = std::pair<const_iter, const_iter>;

            std::vector<sub_range_t> resSubRanges{};

            const float elemsPerRangeF = float(input.size()) / float(numSubRanges);
            const size_t elemsPerRange = std::max(size_t(std::ceil(elemsPerRangeF)),
                                                  size_t(1));

            const_iter begin = input.cbegin();

            while (begin != input.cend())
            {
                const size_t elemsInSubRange = std::min((size_t) std::distance(begin, input.cend()),
                                                        elemsPerRange);

                auto subRangeEnd = std::next(begin, elemsInSubRange);
                resSubRanges.emplace_back(begin, subRangeEnd);
                begin = subRangeEnd;
            }

            assert(numSubRanges >= resSubRanges.size() &&
                   "Number of result subranges must be less then or equal to requested number");
            return resSubRanges;
        }

    }
}


