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
    }

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

        template<typename SteadyTimer, typename = detail::require_steady_timer<SteadyTimer>>
        explicit connection_attempt(Connection &connection, SteadyTimer &&timer)
                : connection_(connection),
                  timer_(std::forward<SteadyTimer>(timer))
        {}

        template<typename SteadyTimer, typename Callable>
        explicit connection_attempt(Connection &connection,
                                    SteadyTimer &&timer,
                                    Callable &&stopOnError)
                : connection_(connection),
                  timer_(std::forward<SteadyTimer>(timer)),
                  stopOnError_(std::forward<Callable>(stopOnError))
        {}

        template<typename Endpoint, typename = require_endpoint<Endpoint>>
        std::future<bool> operator()(Endpoint &&endpoint,
                                     size_t attempts,
                                     std::chrono::milliseconds timeout = default_timeout())
        {
            connectionResult_ = {};
            asyncConnect(std::forward<Endpoint>(endpoint),
                         attempts,
                         timeout);
            return connectionResult_.get_future();
        }

        // default attempts = infinite_attempts
        template<typename Endpoint,
                typename = require_endpoint<Endpoint>>
        std::future<bool> operator()(Endpoint endpoint,
                                     std::chrono::milliseconds timeout = default_timeout())
        {
            connectionResult_ = {};
            asyncConnect(std::forward<Endpoint>(endpoint),
                         infinite_attempts(),
                         timeout);
            return connectionResult_.get_future();
        }

    private:
        connection_type &connection_;
        asio::steady_timer timer_;
        std::function<bool(const asio::error_code &)> stopOnError_;
        std::promise<bool> connectionResult_;

        template <typename Duration>
        void startTimer(const Duration &timeout)
        {
            timer_.expires_after(timeout);
            timer_.async_wait([this, timeout](const asio::error_code &errorCode)
                              {
                                  if (!errorCode &&
                                      stopOnError_ &&
                                      stopOnError_(asio::error::timed_out))
                                  {
                                      connection_.cancel();
                                      return;
                                  }

                                  if (errorCode == asio::error::operation_aborted)
                                      return;

                                  if (errorCode)
                                  {
                                      // TODO: handle timer errors? I guess there should be none
                                      std::cerr << "Timer error: " << errorCode << std::endl;
                                  }

                                  startTimer(timeout);
                              });
        }

        void stopTimer()
        {
            timer_.cancel();
        }

        template <typename Duration>
        void asyncConnect(endpoint_type endpoint,
                          size_t attempts,
                          const Duration &timeout)
        {
            startTimer(timeout);

            connection_.async_connect(endpoint, [this,
                    endpoint,
                    attempts,
                    timeout](const asio::error_code &errorCode)
            {
                if (!errorCode)
                    return stopTimer(),
                            connectionResult_.set_value(true);

                const auto attemptsLeft = attempts == infinite_attempts() ?
                                          infinite_attempts() :
                                          attempts - 1;

                // would it catch interrupted operation on cancel?
                if ((stopOnError_ &&
                     stopOnError_(errorCode)) ||
                    !attemptsLeft)
                    return stopTimer(),
                            connectionResult_.set_value(false);

                asyncConnect(endpoint,
                             attemptsLeft,
                             timeout);
            });
        }
    };

    template<typename Connection,
            typename SteadyTimer,
            typename = detail::require_steady_timer<SteadyTimer>>
    auto make_connection_attempt(Connection &connection,
                                 SteadyTimer &&steadyTimer) -> connection_attempt<Connection>
    {
        return connection_attempt<Connection>(connection, std::forward<SteadyTimer>(steadyTimer));
    }

    template<typename Connection,
            typename SteadyTimer,
            typename Callable,
            typename = detail::require_steady_timer<SteadyTimer>>
    auto make_connection_attempt(Connection &connection,
                                 SteadyTimer &&steadyTimer,
                                 Callable &&stopOnError) -> connection_attempt<Connection>
    {
        return connection_attempt<Connection>(connection,
                                              std::forward<SteadyTimer>(steadyTimer),
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