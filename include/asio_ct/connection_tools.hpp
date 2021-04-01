#pragma once

#include <type_traits>
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

    template<typename Connection, typename SteadyTimer>
    auto make_connection_attempt(Connection &connection,
                                 SteadyTimer &&steadyTimer) -> connection_attempt<Connection>
    {
        return connection_attempt<Connection>(connection, std::forward<SteadyTimer>(steadyTimer));
    }

    template<typename Connection,
            typename SteadyTimer,
            typename Callable>
    auto make_connection_attempt(Connection &connection,
                                 SteadyTimer &&steadyTimer,
                                 Callable &&stopOnError) -> connection_attempt<Connection>
    {
        return connection_attempt<Connection>(connection,
                                              std::forward<SteadyTimer>(steadyTimer),
                                              std::forward<Callable>(stopOnError));
    }
}