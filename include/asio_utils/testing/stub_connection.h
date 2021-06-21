#pragma once

#include <asio.hpp>

namespace eld
{
    namespace testing
    {
        struct stub_config
        {
            int local_endpoint,   //
                remote_endpoint;
        };

        class stub_connection
        {
        public:
            using config_type = stub_config;

            template<typename ExecutorT>
            stub_connection(ExecutorT &executor, const config_type &config)
              : config_(config),
                timerSend_(executor)
            {
            }

            stub_connection(const stub_connection &) = default;
            stub_connection(stub_connection &&) = default;

            stub_connection &operator=(const stub_connection &) = default;
            stub_connection &operator=(stub_connection &&) = default;

            const config_type &get_config() const { return config_; }

            void configure(const config_type &endpoint) { config_ = endpoint; }

            template<typename MutableBuffer, typename CompletionT>
            auto async_receive(MutableBuffer &&mutableBuffer, CompletionT &&a_completion)
            {
                using result_t = asio::async_result<std::decay_t<CompletionT>,
                                                    void(const asio::error_code &, size_t)>;
                using completion_t = typename result_t::completion_handler_type;

                completion_t completion{ std::forward<CompletionT>(a_completion) };
                result_t result{ completion };

                auto buffer = std::forward<MutableBuffer>(mutableBuffer);

                receiveHandler_ = std::move(completion);
                pReadBuffer_ = static_cast<uint8_t *>(buffer.data());
                readBufferSize = buffer.size();

                return result.get();
            }

            // const buffer must be asio const buffer
            template<typename ConstBuffer, typename CompletionT>
            auto async_send(ConstBuffer &&constBuffer, CompletionT &&a_completion)
            {
                using result_t = asio::async_result<std::decay_t<CompletionT>,
                                                    void(const asio::error_code &, size_t)>;
                using completion_t = typename result_t::completion_handler_type;

                completion_t completion{ std::forward<CompletionT>(a_completion) };
                result_t result{ completion };

                // emulate sending delay
                timerSend_.expires_after(std::chrono::milliseconds(20));
                timerSend_.async_wait(
                    [completion = std::move(completion),
                     constBuffer = std::forward<ConstBuffer>(constBuffer),
                     this](const asio::error_code &errorCode) mutable
                    {
                        const auto *pData = static_cast<const uint8_t*>(constBuffer.data());
                        const size_t size = constBuffer.size();

                        assert(remoteHost_ && "Remote host is not set!");
                        remoteHost_(pData, std::next(pData, size));

                        completion(asio::error_code(), size);
                    });

                return result.get();
            }

            void cancel()
            {
                // TODO: implement
            }

            // reading operator for async_receive
            template<typename RandomIter>
            void operator()(RandomIter begin, RandomIter end)
            {
                assert(pReadBuffer_ && readBufferSize && "Invalid read buffer!");

                // TODO: check size of input against read buffer
                size_t received = std::distance(begin, end);

                std::copy(begin, end, pReadBuffer_);
                pReadBuffer_ = nullptr;
                readBufferSize = 0;

                receiveHandler_(asio::error_code(), received);
                receiveHandler_ = {};
            }

            template <typename RemoteHostT>
            void set_remote_host(RemoteHostT &remoteHost)
            {
                remoteHost_ = [&remoteHost](const uint8_t* pBegin, const uint8_t *pEnd)
                {
                    remoteHost(pBegin, pEnd);
                };
            }

            template <typename RemoteHostT>
            void set_remote_host(RemoteHostT &&remoteHost)
            {
                remoteHost_ = std::forward<RemoteHostT>(remoteHost);
            }

        private:
            config_type config_;
            std::function<void(const uint8_t*, const uint8_t*)> remoteHost_;
            asio::steady_timer timerSend_;

            uint8_t *pReadBuffer_ = nullptr;
            size_t readBufferSize = 0;
            std::function<void(const asio::error_code&, size_t)> receiveHandler_;
        };
    }
}