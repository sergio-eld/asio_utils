#pragma once

#include <type_traits>
#include <iterator>
#include <queue>
#include <atomic>
#include <mutex>

#include <chrono>
#include <future>
#include <functional>

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


    // TODO: make_connection_attempts as a non-persistent composed task
    // TODO: require duration
    // version with synchronization
    // connections strand is used
//    template<typename Connection,
//            typename Endpoint,
//            typename Duration,
//            typename = detail::require_endpoint<Endpoint, Connection>>
//    std::future<Connection> make_connection_attempt(Connection &&connection,
//                                                    Endpoint &&endpoint,
//                                                    Duration &&timeout, // may be defaulted
//                                                    size_t attempts
//                                                    )
//    {
//
//    };


    // TODO: functional non-persistent object
    template<typename Connection>
    class connection_attempt
    {
    public:
        using connection_type = Connection;
        using endpoint_type = typename Connection::endpoint_type;

        template<typename Endpoint>
        using require_endpoint = typename std::enable_if<std::is_same<Endpoint, endpoint_type>::value>::type;

        constexpr static auto default_timeout()
        {
            return std::chrono::milliseconds(3000);
        }

        constexpr static size_t infinite_attempts()
        {
            return size_t() - 1;
        }

        explicit connection_attempt(Connection &connection)
                : connection_(connection)
        {}

        template<typename Callable>
        explicit connection_attempt(Connection &connection,
                                    Callable &&stopOnError)
                : connection_(connection),
                  stopOnError_(std::forward<Callable>(stopOnError))
        {}

        template<typename Endpoint,
                typename Duration,
                typename = require_endpoint<Endpoint>>
        std::future<bool> operator()(Endpoint &&endpoint,
                                     size_t attempts,
                                     Duration &&timeout = default_timeout())
        {
            connectionResult_ = {};
            asyncConnect(std::forward<Endpoint>(endpoint),
                         attempts,
                         std::forward<Duration>(timeout));
            return connectionResult_.get_future();
        }

        // default attempts = infinite_attempts
        template<typename Endpoint,
                typename Duration,
                typename = require_endpoint<Endpoint>>
        std::future<bool> operator()(Endpoint endpoint,
                                     Duration &&timeout = default_timeout())
        {
            connectionResult_ = {};
            asyncConnect(std::forward<Endpoint>(endpoint),
                         infinite_attempts(),
                         std::forward<Duration>(timeout));
            return connectionResult_.get_future();
        }

    private:
        connection_type &connection_;
        asio::steady_timer timer_
                {connection_.get_executor()}; // this does not compile -> {asio::get_associated_executor(connection_)};

        std::function<bool(const asio::error_code &)> stopOnError_;
        std::promise<bool> connectionResult_;

        // cancels the connection on timeout!
        template<typename Duration>
        void startTimer(const Duration &timeout)
        {
            timer_.expires_after(timeout); // it will automatically cancel a pending timer

            timer_.async_wait(
                    [this, timeout](const asio::error_code &errorCode)
                    {
                        // will occur on connection error before timeout
                        if (errorCode == asio::error::operation_aborted)
                            return;

                        // TODO: handle timer errors? What are the possible errors?
                        assert(!errorCode && "unexpected timer error!");

                        // stop attempts
                        connection_.cancel();
                    });
        }

        void stopTimer()
        {
            timer_.cancel();
        }

        /**
         * Will be trying to connect until:<br>
         * - has run out of attempts
         * - has been required to stop by stopOnError callback (if it was set)
         * @param endpoint
         * @param attempts
         */
        template<typename Duration>
        void asyncConnect(endpoint_type endpoint,
                          size_t attempts,
                          Duration &&timeout)
        {
            startTimer(timeout);

            connection_.async_connect(endpoint, [this,
                    endpoint,
                    attempts,
                    timeout = std::forward<Duration>(timeout)](const asio::error_code &errorCode)
            {
                const auto attemptsLeft = attempts == infinite_attempts() ?
                                          infinite_attempts() :
                                          attempts - 1;

                if ((stopOnError_ &&
                     stopOnError_(errorCode == asio::error::operation_aborted ?
                                  // special case for operation_aborted on timer expiration - need to send timed_out explicitly
                                  // this should only be resulted from the timer calling cancel()
                                  asio::error::timed_out :
                                  errorCode)) ||
                    !attemptsLeft)
                {
                    stopTimer();
                    connectionResult_.set_value(false);
                    return;
                }

                asyncConnect(endpoint,
                             attemptsLeft,
                             timeout);
            });
        }
    };

    template<typename Connection>
    auto make_connection_attempt(Connection &connection) -> connection_attempt<Connection>
    {
        return connection_attempt<Connection>(connection);
    }

    template<typename Connection,
            typename Callable>
    auto make_connection_attempt(Connection &connection,
                                 Callable &&stopOnError) -> connection_attempt<Connection>
    {
        return connection_attempt<Connection>(connection,
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