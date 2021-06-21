#pragma once

#include "asio_utils/traits.h"
#include "asio_utils/utility.h"

#include <atomic>
#include <memory>
#include <mutex>

namespace eld
{
    namespace direction
    {
        namespace tag
        {
            struct a_to_b
            {
            };
            struct b_to_a
            {
            };

            template<typename>
            struct reverse;

            template<>
            struct reverse<tag::a_to_b>
            {
                using type = tag::b_to_a;
            };

            template<>
            struct reverse<tag::b_to_a>
            {
                using type = tag::a_to_b;
            };
        }

        constexpr tag::a_to_b a_to_b{};
        constexpr tag::b_to_a b_to_a{};

        constexpr tag::b_to_a reverse(tag::a_to_b)
        {
            return {};
        }

        constexpr tag::a_to_b reverse(tag::b_to_a)
        {
            return {};
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

        // does not compile!
//        template<typename = typename std::enable_if<util::conjunction<
//            std::is_default_constructible<connection_type_a>,
//            std::is_default_constructible<connection_type_b>,
//            std::is_default_constructible<implementation_type>>::value>::type>
//        constexpr connection_adapter_basic()
//        {
//        }

        // TODO: hide?
        connection_adapter_basic(connection_adapter_basic &&) noexcept = default;

        template<typename ConTA, typename ConTB, typename... ImplArgsT>
        connection_adapter_basic(ConTA &&connectionA, ConTB &&connectionB, ImplArgsT &&...implArgs)
            : pImpl_(std::make_unique<state>(std::forward<ConTA>(connectionA),
                                             std::forward<ConTB>(connectionB))),
              impl_(std::forward<ImplArgsT>(implArgs)...)
        {
        }

        template<typename ConfigTA, typename ConfigTB, typename CompletionT, typename DirectionTag>
        auto async_run(ConfigTA &&a_configFrom,
                       ConfigTB &&a_configTo,
                       DirectionTag directionTag,
                       CompletionT &&completionToken)
        {
            // TODO: create completion from impl_
            using result_t =
            asio::async_result<std::decay_t<CompletionT>, void(const error_type &)>;
            using completion_t = typename result_t::completion_handler_type;

            completion_t completion{ std::forward<CompletionT>(completionToken) };
            result_t result{ completion };

            std::lock_guard<std::mutex> lg{ get_mutex(directionTag) };
            const auto &currentState = get_state(directionTag);

            auto &connectionFrom = get_source_connection(directionTag);
            const auto configFrom = std::forward<ConfigTA>(a_configFrom);
            const auto &configFromCurrent = implementation_type::get_config(connectionFrom);

            auto &connectionTo =
                get_destination_connection(directionTag);
            const auto configTo = std::forward<ConfigTB>(a_configTo);
            const auto &configToCurrent = implementation_type::get_config(connectionTo);

            const bool equalConfigsFrom = impl_.compare_configs(configFrom, configFromCurrent),
                equalConfigsTo = impl_.compare_configs(configTo, configToCurrent);

            if (currentState != connection_state::idle &&   //
                equalConfigsFrom &&                         //
                equalConfigsTo)
            {
                completion(impl::error::already_started());
                return result.get();
            }

            if (currentState != connection_state::idle)
                stop_active_connection(directionTag);

            if (!equalConfigsFrom)
                impl_.configure(connectionFrom, configFrom);

            if (!equalConfigsTo)
                impl_.configure(connectionTo, configTo);

            get_state(directionTag) = connection_state::reading;
            async_receive(DirectionTag());

            return result.get();
        }

        template <typename DirectionTag, typename CompletionT>
        auto async_run(DirectionTag directionTag, CompletionT &&completionToken)
        {
            return async_run(impl_.get_config(get_source_connection(directionTag)),
                             impl_.get_config(get_destination_connection(directionTag)),
                             directionTag,
                             std::forward<CompletionT>(completionToken));
        }


        template<typename DirectionTag>
        void stop(DirectionTag)
        {
            std::lock_guard<std::mutex> lg{ get_mutex(DirectionTag()) };
            stop_active_connection(DirectionTag());
        }

        ~connection_adapter_basic()
        {
            auto futureAToB = sync_stop_connection(direction::a_to_b);
            auto futureBToA = sync_stop_connection(direction::b_to_a);
        }

    private:
        enum class connection_state
        {
            idle,
            reading,
            writing
        };

        auto &get_mutex(direction::tag::a_to_b) { return pImpl_->mutex_a_to_b_; }
        auto &get_mutex(direction::tag::b_to_a) { return pImpl_->mutex_a_to_b_; }

        auto &get_state(direction::tag::a_to_b) { return pImpl_->state_a_to_b_; }
        auto &get_state(direction::tag::b_to_a) { return pImpl_->state_b_to_a_; }

        auto &get_source_connection(direction::tag::a_to_b) { return pImpl_->connectionA_; }
        auto &get_source_connection(direction::tag::b_to_a) { return pImpl_->connectionB_; }

        auto &get_destination_connection(direction::tag::a_to_b) { return pImpl_->connectionB_; }
        auto &get_destination_connection(direction::tag::b_to_a) { return pImpl_->connectionA_; }

        auto &get_buffer(direction::tag::a_to_b) { return pImpl_->buffer_a_to_b_; }
        auto &get_buffer(direction::tag::b_to_a) { return pImpl_->buffer_b_to_a_; }

        template<typename DirectionTag>
        void stop_active_connection(DirectionTag)
        {
            auto &state_ = get_state(DirectionTag());
            assert(state_ != connection_state::idle && "Unexpected state!");

            auto &source = get_source_connection(DirectionTag());
            auto &destination = get_destination_connection(DirectionTag());

            // TODO: don't use cancel, it will cancel ALL asynchronous operations for connection
            if (state_ == connection_state::reading)
                impl_.cancel(source);
            else
                impl_.cancel(destination);
        }

        template<typename DirectionTag>
        void async_receive(DirectionTag)
        {
            impl_.async_receive(get_source_connection(DirectionTag()),
                                asio::buffer(get_buffer(DirectionTag())),
                                [this](const error_type &errorCode, size_t bytesRead)
                                {
                                  std::lock_guard<std::mutex> lg{ get_mutex(DirectionTag()) };

                                  if (!handle_error(errorCode, DirectionTag()))
                                      return;

                                  get_state(DirectionTag()) = connection_state::writing;
                                  async_send(bytesRead, DirectionTag());
                                });
        }

        template<typename DirectionTag>
        void async_send(size_t bytesReceived, DirectionTag)
        {
            impl_.async_send(get_destination_connection(DirectionTag()),
                             asio::buffer(get_buffer(DirectionTag()).data(), bytesReceived),
                             [this](const error_type &errorCode, size_t bytesSent)
                             {
                               std::lock_guard<std::mutex> lg{ get_mutex(DirectionTag()) };

                               if (!handle_error(errorCode, DirectionTag()))
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
                impl_.remote_host_has_disconnected(get_source_connection(DirectionTag()),
                                                   errorCode) :
                impl_.remote_host_has_disconnected(get_destination_connection(DirectionTag()),
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
            std::lock_guard<std::mutex> lg{ get_mutex(DirectionTag()) };

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

            typename implementation_type::buffer_type buffer_a_to_b_{},   //
            buffer_b_to_a_{};

            std::atomic<connection_state> state_a_to_b_{ connection_state::idle },   //
            state_b_to_a_{ connection_state::idle };
            std::mutex mutex_a_to_b_,   //
            mutex_b_to_a_;

            std::function<void(const error_type &)> completion_handler_;
        };

        std::unique_ptr<state> pImpl_;
        implementation_type impl_;
    };

    template<typename ConnectionTA, typename ConnectionTB, typename ImplT>
    connection_adapter_basic<ConnectionTA, ConnectionTB, ImplT> make_connection_adapter(
        ConnectionTA &&connectionL,
        ConnectionTB &&connectionR,
        ImplT &&adapterConfig)
    {
        return connection_adapter_basic<ConnectionTA, ConnectionTB, ImplT>(
            std::forward<ConnectionTA>(connectionL),
            std::forward<ConnectionTB>(connectionR));
    }

    template<typename ConnectionTA, typename ConnectionTB, typename ImplT,
        typename =
        typename std::enable_if<std::is_default_constructible<ConnectionTA>::value &&
                                std::is_default_constructible<ConnectionTA>::value>::type>
    connection_adapter_basic<ConnectionTA, ConnectionTB, ImplT> make_connection_adapter(ImplT &&adapterConfig)
    {
        return connection_adapter_basic<ConnectionTA, ConnectionTB, ImplT>(
            std::forward<ConnectionTA>(ConnectionTA()),
            std::forward<ConnectionTB>(ConnectionTB()));
    }
}
