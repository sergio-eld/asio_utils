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

    /**
     * Algorithm creates bridge between 2 connection points.
     * @tparam ConnectionTA
     * @tparam ConnectionTB
     * @tparam ImplT
     */
    template<typename ConnectionTA, typename ConnectionTB, typename ImplT>
    class connection_adapter_basic
    {
    public:
        using connection_type_a = ConnectionTA;
        using connection_type_b = ConnectionTB;
        using implementation_type = ImplT;

    private:
        using error_type = typename implementation_type::error_type;
        using impl = implementation_type;

    public:
        using config_type_a = typename traits::connection<connection_type_a>::config_type;
        using config_type_b = typename traits::connection<connection_type_b>::config_type;

        template<typename = typename std::enable_if<util::conjunction<
                     std::is_default_constructible<connection_type_a>,
                     std::is_default_constructible<connection_type_b>,
                     std::is_default_constructible<implementation_type>>::value>::type>
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

        template<typename ConfigTA, typename ConfigTB, typename CompletionT, typename DirectionTag>
        auto async_run(ConfigTA &&a_configFrom,
                       ConfigTB &&a_configTo,
                       DirectionTag,
                       CompletionT &&completionToken)
        {
            using result_t =
                asio::async_result<std::decay_t<CompletionT>, void(const error_type &)>;
            using completion_t = typename result_t::completion_handler_type;

            completion_t completion{ std::forward<CompletionT>(completionToken) };
            result_t result{ completion };

            std::lock_guard<std::mutex> lg{ get_mutex(DirectionTag()) };
            const auto &currentState = get_state(DirectionTag());

            auto &connectionFrom = get_connection(DirectionTag());
            const auto configFrom = std::forward<ConfigTA>(a_configFrom);
            const auto &configFromCurrent = implementation_type::get_config(connectionFrom);

            auto &connectionTo =
                get_connection(typename tag::detail::reverse_adapter_tag<DirectionTag>::type());
            const auto configTo = std::forward<ConfigTB>(a_configTo);
            const auto &configToCurrent = implementation_type::get_config(connectionTo);

            const bool equalConfigsFrom = impl::compare_configs(configFrom, configFromCurrent),
                       equalConfigsTo = impl::compare_configs(configTo, configToCurrent);

            if (currentState != connection_state::idle &&   //
                equalConfigsFrom &&                         //
                equalConfigsTo)
            {
                completion(impl::error::already_started());
                return result.get();
            }

            stop_active_connection(DirectionTag());

            if (!equalConfigsFrom)
                impl::configure(connectionFrom, configFrom);

            if (!equalConfigsTo)
                impl::configure(connectionTo, configTo);

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
                impl::cancel(source);
            else
                impl::cancel(destination);
        }

        template<typename DirectionTag>
        void async_receive(DirectionTag)
        {
            impl::async_receive(get_connection(DirectionTag()),
                                asio::buffer(get_buffer(DirectionTag())),
                                [this](const error_type &errorCode, size_t bytesRead)
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
            impl::async_send(get_connection(tag::detail::reverse_adapter_tag<DirectionTag>()),
                             asio::buffer(get_buffer(DirectionTag()).data(), bytesReceived),
                             [this](const error_type &errorCode, size_t bytesSent)
                             {
                                 std::lock_guard<std::mutex> lg{ get_mutex(DirectionTag()) };

                                 if (!handle_error(errorCode), DirectionTag())
                                     return;

                                 get_state(DirectionTag()) = connection_state::reading;
                                 async_receive(DirectionTag());
                             });
        }

        template<typename DirectionTag>
        bool handle_error(const error_type &errorCode, DirectionTag)
        {
            if (errorCode == impl::error::operation_aborted())
            {
                get_state(DirectionTag()) = connection_state::idle;

                assert(pImpl_->completion_handler_ &&
                       "Completion handler is expected to be in valid state!");
                pImpl_->completion_handler_(error_type());
                pImpl_->completion_handler_ = {};

                return false;
            }

            assert(get_state(DirectionTag()) != connection_state::idle &&
                   "Unexpected connection state");

            if (get_state(DirectionTag()) == connection_state::reading ?
                    impl::remote_host_has_disconnected(get_source_connection(DirectionTag()),
                                                       errorCode) :
                    impl::remote_host_has_disconnected(get_destination_connection(DirectionTag()),
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
                 sharedPromise](const error_type &errorCode) mutable
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

            typename implementation_type::buffer_type buffer_a_to_b_,   //
                buffer_b_to_a_;

            std::atomic<connection_state> state_a_to_b_{ connection_state::idle },   //
                state_b_to_a_{ connection_state::idle };
            std::mutex mutex_a_to_b_,   //
                mutex_b_to_a_;

            std::function<void(const error_type &)> completion_handler_;
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

    // TODO: move customization

    template<typename ConnectionTA, typename ConnectionTB>
    connection_adapter_basic<ConnectionTA, ConnectionTB, custom::connection_asio_impl>
        make_connection_adapter(ConnectionTA &&connectionL, ConnectionTB &&connectionR)
    {
        return connection_adapter_basic<ConnectionTA,
                                        ConnectionTB,
                                        custom::connection_asio_impl>(
            std::forward<ConnectionTA>(connectionL),
            std::forward<ConnectionTB>(connectionR));
    }
}
