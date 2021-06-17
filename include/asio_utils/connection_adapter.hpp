#pragma once

#include "asio_utils/traits.h"
#include "asio_utils/utils.h"

#include <asio.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <tuple>

#include <array>
#include <cstdint>

/**
 * This utility provides an adapter class to resend byte arrays from one type of
 * device, socket or connection to another
 */

namespace eld
{
    namespace detail
    {
    }

    namespace tag
    {
        struct adapter_a_to_b
        {
        };
        struct adapter_b_to_a
        {
        };

        namespace detail
        {
            template<typename>
            struct reverse_adapter_tag;

            template<>
            struct reverse_adapter_tag<adapter_a_to_b>
            {
                using type = adapter_b_to_a;
            };

            template<>
            struct reverse_adapter_tag<adapter_b_to_a>
            {
                using type = adapter_a_to_b;
            };
        }

    }

    template<typename ConnectionTA, typename ConnectionTB, typename AdapterConfigT>
    class connection_adapter_basic
    {
    public:
        using connection_type_a = ConnectionTA;
        using connection_type_b = ConnectionTB;
        using adapter_config_type = AdapterConfigT;

        using config_type_a = typename traits::connection<connection_type_a>::config_type;
        using config_type_b = typename traits::connection<connection_type_b>::config_type;

        template<typename = typename std::enable_if<util::conjunction<
                     std::is_default_constructible<connection_type_a>,
                     std::is_default_constructible<connection_type_b>,
                     std::is_default_constructible<adapter_config_type>>::value>::type>
        constexpr connection_adapter_basic()
        {
        }

        // TODO: hide?
        connection_adapter_basic(connection_adapter_basic &&) = default;

        template<typename ConTA, typename ConTB>
        connection_adapter_basic(ConTA &&connectionA, ConTB &&connectionB)
          : pImpl_(std::make_unique<state>(std::forward<ConTA>(connectionA),
                                           std::forward<ConTB>(connectionB)))
        {
        }

        template<typename ConfigTFrom,
                 typename ConfigTTo,
                 typename CompletionT,
                 typename DirectionTag>
        auto async_run(ConfigTFrom &&a_configFrom,
                       ConfigTTo &&a_configTo,
                       DirectionTag,
                       CompletionT &&completionToken)
        {
            using result_t =
                asio::async_result<std::decay_t<CompletionT>, void(const asio::error_code &)>;
            using completion_t = typename result_t::completion_handler_type;

            completion_t completion{ std::forward<CompletionT>(completionToken) };
            result_t result{ completion };

            std::lock_guard<std::mutex> lg{ get_mutex(DirectionTag()) };
            const auto &currentState = get_state(DirectionTag());

            auto &connectionFrom = get_connection(DirectionTag());
            const auto configFrom = std::forward<ConfigTFrom>(a_configFrom);
            const auto &configFromCurrent = custom::get_config(connectionFrom);

            auto &connectionTo =
                get_connection(typename tag::detail::reverse_adapter_tag<DirectionTag>::type());
            const auto configTo = std::forward<ConfigTTo>(a_configTo);
            const auto &configToCurrent = custom::get_config(connectionTo);

            const bool equalConfigsFrom = custom::compare_configs(configFrom, configFromCurrent),
                       equalConfigsTo = custom::compare_configs(configTo, configToCurrent);

            if (currentState != connection_state::idle &&   //
                equalConfigsFrom &&                         //
                equalConfigsTo)
            {
                completion(asio::error::already_started);
                return result.get();
            }

            stop_active_connection(DirectionTag());

            if (!equalConfigsFrom)
                custom::configure(connectionFrom, configFrom);

            if (!equalConfigsTo)
                custom::configure(connectionTo, configTo);

            currentState = connection_state::reading;
            async_receive(DirectionTag());

            return result.get();
        }

        template<typename DirectionTag>
        void stop(DirectionTag)
        {
            std::lock_guard<std::mutex> lg{ get_mutex(DirectionTag()) };
            stop_active_connection(DirectionTag());
        }

        ~connection_adapter_basic()
        {
            auto futureAToB = sync_stop_connection(tag::adapter_a_to_b());
            auto futureBToA = sync_stop_connection(tag::adapter_b_to_a());
        }

    private:
        enum class connection_state
        {
            idle,
            running,   // TODO: remove running
            reading,
            writing
        };

        auto &get_mutex(tag::adapter_a_to_b) { return pImpl_->mutex_a_to_b_; }
        auto &get_mutex(tag::adapter_b_to_a) { return pImpl_->mutex_a_to_b_; }

        auto &get_state(tag::adapter_a_to_b) { return pImpl_->state_a_to_b_; }
        auto &get_state(tag::adapter_b_to_a) { return pImpl_->state_b_to_a_; }

        auto &get_source_connection(tag::adapter_a_to_b) { return pImpl_->connectionA_; }
        auto &get_source_connection(tag::adapter_b_to_a) { return pImpl_->connectionB_; }

        auto &get_destination_connection(tag::adapter_a_to_b) { return pImpl_->connectionB_; }
        auto &get_destination_connection(tag::adapter_b_to_a) { return pImpl_->connectionA_; }

        //        auto &get_connection(tag::adapter_a_to_b) { return pImpl_->connectionA_; }
        //        auto &get_connection(tag::adapter_b_to_a) { return pImpl_->connectionB_; }

        auto &get_buffer(tag::adapter_a_to_b) { return pImpl_->buffer_a_to_b_; }
        auto &get_buffer(tag::adapter_b_to_a) { return pImpl_->buffer_b_to_a_; }

        template<typename DirectionTag>
        void stop_active_connection(DirectionTag)
        {
            auto &state_ = get_state(DirectionTag());
            assert(state_ != connection_state::idle && "Unexpected state!");

            auto &source = get_source_connection(DirectionTag());
            auto &destination = get_destination_connection(DirectionTag());

            if (state_ == connection_state::reading)
                custom::cancel(source);
            else
                custom::cancel(destination);
        }

        template<typename DirectionTag>
        void async_receive(DirectionTag)
        {
            custom::async_receive(get_connection(DirectionTag()),
                                  asio::buffer(get_buffer(DirectionTag())),
                                  [this](const asio::error_code &errorCode, size_t bytesRead)
                                  {
                                      std::lock_guard<std::mutex> lg{ get_mutex(DirectionTag()) };

                                      if (!handle_error(errorCode), DirectionTag())
                                          return;

                                      get_state(DirectionTag()) = connection_state::writing;
                                      async_send(bytesRead, DirectionTag());
                                  });
        }

        template<typename DirectionTag>
        void async_send(size_t bytesReceived, DirectionTag)
        {
            custom::async_send(get_connection(tag::detail::reverse_adapter_tag<DirectionTag>()),
                               asio::buffer(get_buffer(DirectionTag()).data(), bytesReceived),
                               [this](const asio::error_code &errorCode, size_t bytesSent)
                               {
                                   std::lock_guard<std::mutex> lg{ get_mutex(DirectionTag()) };

                                   if (!handle_error(errorCode), DirectionTag())
                                       return;

                                   get_state(DirectionTag()) = connection_state::reading;
                                   async_receive(DirectionTag());
                               });
        }

        template<typename DirectionTag>
        bool handle_error(const asio::error_code &errorCode, DirectionTag)
        {
            if (errorCode == asio::error::operation_aborted)
            {
                get_state(DirectionTag()) = connection_state::idle;

                assert(pImpl_->completion_handler_ &&
                       "Completion handler is expected to be in valid state!");
                pImpl_->completion_handler_(asio::error_code());
                pImpl_->completion_handler_ = {};

                return false;
            }

            assert(get_state(DirectionTag()) != connection_state::idle &&
                   "Unexpected connection state");

            if (get_state(DirectionTag()) == connection_state::reading ?
                    custom::remote_host_has_disconnected(get_source_connection(DirectionTag()),
                                                         errorCode) :
                    custom::remote_host_has_disconnected(get_destination_connection(DirectionTag()),
                                                         errorCode))
            {
                get_state(DirectionTag()) = connection_state::idle;

                assert(pImpl_->completion_handler_ &&
                       "Completion handler is expected to be in valid state!");
                pImpl_->completion_handler_(errorCode);
                pImpl_->completion_handler_ = {};

                return false;
            }
            return true;
        }

        template<typename DirectionTag>
        std::future<void> sync_stop_connection(DirectionTag)
        {
            std::lock_guard<std::mutex> lg{ get_mutex(tag::adapter_a_to_b()) };

            // will not compile if try to move promise into lambda
            auto sharedPromise = std::make_shared<std::promise<void>>();
            auto future = sharedPromise->get_future();

            if (get_state(DirectionTag()) == connection_state::idle)
            {
                sharedPromise->set_value();
                return future;
            }

            pImpl_->completion_handler_ =
                [completionHandler = std::move(pImpl_->completion_handler_),
                 sharedPromise](const asio::error_code &errorCode) mutable
            {
                sharedPromise->set_value();
                completionHandler(errorCode);
            };

            stop_active_connection(DirectionTag());
            return future;
        }

    private:
        // workaround. Mandatory copy elision is not supported prior to C++17
        struct state
        {
            template<typename ConTA, typename ConTB>
            state(ConTA &&connectionA, ConTB &&connectionB)
              : connectionA_(std::forward<ConTA>(connectionA)),
                connectionB_(std::forward<ConTB>(connectionB))
            {
            }

            connection_type_a connectionA_;
            connection_type_b connectionB_;

            typename adapter_config_type::buffer_type buffer_a_to_b_,   //
                buffer_b_to_a_;

            std::atomic<connection_state> state_a_to_b_{ connection_state::idle },   //
                state_b_to_a_{ connection_state::idle };
            std::mutex mutex_a_to_b_,   //
                mutex_b_to_a_;

            std::function<void(const asio::error_code &)> completion_handler_;
        };

        std::unique_ptr<state> pImpl_;
    };

    template<typename ConnectionTA, typename ConnectionTB, typename AdapterConfig>
    connection_adapter_basic<ConnectionTA, ConnectionTB, AdapterConfig> make_connection_adapter(
        ConnectionTA &&connectionL,
        ConnectionTB &&connectionR,
        AdapterConfig &&adapterConfig)
    {
        return connection_adapter_basic<ConnectionTA, ConnectionTB, AdapterConfig>(
            std::forward<ConnectionTA>(connectionL),
            std::forward<ConnectionTB>(connectionR));
    }

    // TODO: move customization to another header
    namespace custom
    {
        struct default_adapter_config
        {
            using buffer_type = std::array<uint8_t, 2048>;
        };
    }

    template<typename ConnectionTA, typename ConnectionTB>
    connection_adapter_basic<ConnectionTA, ConnectionTB, custom::default_adapter_config>
        make_connection_adapter(ConnectionTA &&connectionL, ConnectionTB &&connectionR)
    {
        return connection_adapter_basic<ConnectionTA, ConnectionTB, custom::default_adapter_config>(
            std::forward<ConnectionTA>(connectionL),
            std::forward<ConnectionTB>(connectionR));
    }
}
