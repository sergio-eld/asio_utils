#pragma once

#include <type_traits>
#include <iterator>
#include <queue>
#include <atomic>
#include <mutex>

#include <chrono>
#include <future>
#include <functional>
#include <memory>

// TODO: remove
#include <iostream>

#include <asio.hpp>

namespace eld
{
    namespace traits
    {
        template<typename Connection>
        using endpoint_type = typename Connection::endpoint_type;
    }

    namespace detail
    {
        template<typename SteadyTimer>
        using require_steady_timer = typename
        std::enable_if<std::is_same<SteadyTimer, asio::steady_timer>::value>::type;

        template<typename POD>
        using require_pod = typename std::enable_if<std::is_pod<POD>::value>::type;

        template<typename Iter>
        constexpr bool check_random_iter()
        {
            return std::is_same<typename std::iterator_traits<Iter>::value_type, uint8_t>() &&
                   std::is_same<typename std::iterator_traits<Iter>::iterator_category,
                           std::random_access_iterator_tag>();
        }

        template<typename Iter>
        using require_iter = typename std::enable_if<check_random_iter<Iter>()>::type;


        template<typename Endpoint, typename Connection>
        using require_endpoint = typename std::enable_if<std::is_same<Endpoint,
                traits::endpoint_type<Connection>>::value>::type;
    }


    template<typename Connection, typename CompletionHandler>
    class composed_connection_attempt
    {
    public:
        using connection_type = Connection;
        using endpoint_type = typename Connection::endpoint_type;

        // TODO: clarify the type!
        using completion_handler_t = CompletionHandler;

        constexpr static auto default_timeout()
        {
            return std::chrono::milliseconds(3000);
        }

        constexpr static size_t infinite_attempts()
        {
            return size_t() - 1;
        }

        using executor_type = asio::associated_executor_t<
                typename std::decay<CompletionHandler>::type,
                typename connection_type::executor_type>;

        executor_type get_executor() const noexcept
        {
            // TODO: get completion handler executor
            return pImpl_->get_executor();
        }

        // TODO: allocator type
        using allocator_type = typename asio::associated_allocator_t<CompletionHandler,
                std::allocator<void>>;

        allocator_type get_allocator() const noexcept
        {
            // TODO: get completion handler allocator
            return pImpl_->get_allocator();
        }

        // TODO: constructor to initialize state, pass timeout value?
        template<typename CompletionHandlerT>
        explicit composed_connection_attempt(connection_type &connection,
                                             CompletionHandlerT &&completionHandler)
                : pImpl_(std::make_shared<impl>(connection,
                                                std::forward<CompletionHandlerT>(completionHandler)))
        {}


        template<typename CompletionHandlerT,
                typename Callable>
        explicit composed_connection_attempt(connection_type &connection,
                                             CompletionHandlerT &&completionHandler,
                                             Callable &&stopOnError)
                : pImpl_(std::make_shared<impl>(connection,
                                                std::forward<CompletionHandlerT>(completionHandler),
                                                std::forward<Callable>(stopOnError)))
        {}

        /**
         * Initiation operator. Initiates composed connection procedure.
         * @tparam Endpoint type of endpoint
         * @tparam Duration type of timeout
         * @param endpoint endpoint to be used for connection
         * @param attempts number of attempts
         * @param timeout value to be used as a timeout between attempts
         */
        // TODO: require endpoint type
        template<typename Endpoint, typename Duration>
        void operator()(Endpoint &&endpoint,
                        size_t attempts,
                        Duration &&timeout = default_timeout())
        {
            pImpl_->endpoint_ = std::forward<Endpoint>(endpoint);
            pImpl_->attempts_ = attempts;
            pImpl_->timeout_ = std::forward<Duration>(timeout);

            asyncConnect();
        }

        /**
         * Initiation operator. Initiates composed connection procedure. Connection attempts default to infinite.
         * @tparam Endpoint type of endpoint
         * @tparam Duration type of timeout
         * @param endpoint endpoint to be used for connection
         * @param timeout value to be used as a timeout between attempts
         */
        // TODO: require endpoint type
        template<typename Endpoint, typename Duration>
        void operator()(Endpoint &&endpoint,
                        Duration &&timeout = default_timeout())
        {
            pImpl_->endpoint_ = std::forward<Endpoint>(endpoint);
            pImpl_->timeout_ = std::forward<Duration>(timeout);

            asyncConnect();
        }

        /**
         * Intermediate completion handler. Will be trying to connect until:<br>
         * - has connected<br>
         * - has run out of attempts<br>
         * - user-provided callback #impl::stopOnError_ interrupts execution when a specific connection error has occurred<br>
         * <br>Will be invoked only on connection events:<br>
         * - success<br>
         * - connection timeout or operation_cancelled in case if timer has expired<br>
         * - connection errors<br>
         * @param errorCode error code resulted from async_connect
         */
        void operator()(const asio::error_code &errorCode)
        {
            if (!errorCode)
            {
                stopTimer();
                pImpl_->completionHandler_(errorCode);
                return;
            }

            const auto attemptsLeft = pImpl_->attempts_ == infinite_attempts() ?
                                      infinite_attempts() :
                                      pImpl_->attempts_ - 1;

            if ((pImpl_->stopOnError_ &&
                 pImpl_->stopOnError_(errorCode == asio::error::operation_aborted ?
                                      // special case for operation_aborted on timer expiration - need to send timed_out explicitly
                                      // this should only be resulted from the timer calling cancel()
                                      asio::error::timed_out :
                                      errorCode)) ||
                !attemptsLeft)
            {
                stopTimer();
                pImpl_->completionHandler_(errorCode == asio::error::operation_aborted ?
                                           asio::error::timed_out :
                                           errorCode);
                return;
            }

            pImpl_->attempts_ = attemptsLeft;
            asyncConnect();
        }

    private:

        struct impl
        {
            template<typename CompletionHandlerT>
            impl(connection_type &connection,
                 CompletionHandlerT &&completionHandler)
                    : connection_(connection),
                      completionHandler_(std::forward<CompletionHandlerT>(completionHandler))
            {}

            template<typename CompletionHandlerT, typename Callable>
            impl(connection_type &connection,
                 CompletionHandlerT &&completionHandler,
                 Callable &&stopOnError)
                    : connection_(connection),
                      completionHandler_(std::forward<CompletionHandlerT>(completionHandler)),
                      stopOnError_(std::forward<Callable>(stopOnError))
            {}

            executor_type get_executor() const noexcept
            {
                return asio::get_associated_executor(completionHandler_,
                                                     connection_.get_executor());
            }

            allocator_type get_allocator() const noexcept
            {
                // TODO: get completion handler allocator
                return allocator_type();
            }

            connection_type &connection_;
            completion_handler_t completionHandler_;
            std::function<bool(const asio::error_code &)> stopOnError_;

            // this should be default constructable or should I pass it in the constructor?
            endpoint_type endpoint_;

            // TODO: make timer initialization from get_executor()
            asio::steady_timer timer_{connection_.get_executor()}; // this does not compile! -> {get_executor()};
            asio::steady_timer::duration timeout_ = default_timeout();
            size_t attempts_ = infinite_attempts();
        };

        // TODO: make unique?
        std::shared_ptr<impl> pImpl_;

        // cancels the connection on timeout!
        void startTimer()
        {
            pImpl_->timer_.expires_after(pImpl_->timeout_); // it will automatically cancel a pending timer
            pImpl_->timer_.async_wait(
                    [pImpl = pImpl_](const asio::error_code &errorCode)
                    {
                        // will occur on connection error before timeout
                        if (errorCode == asio::error::operation_aborted)
                            return;

                        // TODO: handle timer errors? What are the possible errors?
                        assert(!errorCode && "unexpected timer error!");

                        // stop attempts
                        pImpl->connection_.cancel();
                    });
        }

        void stopTimer()
        {
            pImpl_->timer_.cancel();
        }

        /**
         * Will be trying to connect until:<br>
         * - has run out of attempts
         * - has been required to stop by stopOnError callback (if it was set)
         * @param endpoint
         * @param attempts
         */
        void asyncConnect()
        {
            startTimer();
            pImpl_->connection_.async_connect(pImpl_->endpoint_, std::move(*this));
        }
    };

    template<typename Connection,
            typename CompletionHandler,
            typename Callable>
    auto make_composed_connection_attempt(Connection &connection,
                                          CompletionHandler &&completionHandler,
                                          Callable &&stopOnError) ->
    composed_connection_attempt<Connection, CompletionHandler>
    {
        return composed_connection_attempt<Connection, CompletionHandler>(connection,
                                                                          std::forward<CompletionHandler>(
                                                                                  completionHandler),
                                                                          std::forward<Callable>(stopOnError));
    }

    template<typename Connection,
            typename Endpoint,
            typename Duration,
            typename CompletionToken,
            typename Callable>
    auto async_connection_attempt(Connection &connection,
                                  Endpoint &&endpoint,
                                  size_t attempts,
                                  Duration &&timeout,
                                  CompletionToken &&completionToken,
                                  Callable &&stopOnError)
    {

        // TODO: get rid of lambda
        auto initiation = [](auto &&completion_handler,
                             Connection &connection,
                             Endpoint &&endpoint,
                             size_t attempts,
                             Duration &&timeout,
                             Callable &&stopOnError)
        {
            using completion_handler_t = typename
            std::decay<decltype(completion_handler)>::type;

            auto composedConnectionAttempt = make_composed_connection_attempt(
                    connection,
                    std::forward<completion_handler_t>(completion_handler),
                    std::forward<Callable>(stopOnError));

            composedConnectionAttempt(std::forward<Endpoint>(endpoint),
                                      attempts,
                                      std::forward<Duration>(timeout));
        };

        return asio::async_initiate<CompletionToken, void(asio::error_code)>(
                initiation,
                completionToken,
                std::ref(connection),
                std::forward<Endpoint>(endpoint),
                attempts,
                std::forward<Duration>(timeout),
                std::forward<Callable>(stopOnError));
    }

    template<typename Connection,
            typename Endpoint,
            typename Duration,
            typename CompletionToken,
            typename Callable>
    auto async_connection_attempt(Connection &connection,
                                  Endpoint &&endpoint,
                                  Duration &&timeout,
                                  CompletionToken &&completionToken,
                                  Callable &&stopOnError)
    {
        // TODO: get rid of boilerplate
        auto initiation = [](auto &&completion_handler,
                             Connection &connection,
                             Endpoint &&endpoint,
                             Duration &&timeout,
                             Callable &&stopOnError)
        {
            using completion_handler_t = typename
            std::decay<decltype(completion_handler)>::type;

            auto composedConnectionAttempt = make_composed_connection_attempt(
                    connection,
                    std::forward<completion_handler_t>(completion_handler),
                    std::forward<Callable>(stopOnError));

            composedConnectionAttempt(std::forward<Endpoint>(endpoint),
                                      std::forward<Duration>(timeout));
        };

        return asio::async_initiate<CompletionToken, void(asio::error_code)>(
                initiation,
                completionToken,
                std::ref(connection),
                std::forward<Endpoint>(endpoint),
                std::forward<Duration>(timeout),
                std::forward<Callable>(stopOnError));
    }

    class send_queue
    {
    public:
        using command_t = std::pair<asio::const_buffer,
                std::function<void(const asio::error_code &, size_t)>>;

        template<typename ConstBuffer, typename Callable>
        void push(ConstBuffer &&buffer, Callable &&onSent)
        {
            std::lock_guard<std::mutex> lockGuard{queueMutex_};
            if (isPendingPromise_)
            {
                pendingCommand_.set_value(command_t(std::forward<ConstBuffer>(buffer),
                                                    std::forward<Callable>(onSent)));
                isPendingPromise_ = false;
                return;
            }

            sendQueue_.emplace(std::forward<ConstBuffer>(buffer),
                               std::forward<Callable>(onSent));
        }


        // only one consumer invokes this (not asynchronously)
        std::future<command_t> pop()
        {
            std::lock_guard<std::mutex> lockGuard{queueMutex_};
            if (!sendQueue_.empty())
            {
                std::promise<command_t> promise{};
                promise.set_value(std::move(sendQueue_.front()));
                sendQueue_.pop();
                return promise.get_future();
            }

            assert(!isPendingPromise_ && "isPendingPromise_ must be false here");
            isPendingPromise_ = true;
            pendingCommand_ = {};
            return pendingCommand_.get_future();
        }

    private:
        std::queue<command_t> sendQueue_;
        std::mutex queueMutex_;
        std::promise<command_t> pendingCommand_;
        std::atomic_bool isPendingPromise_{false};
    };

}