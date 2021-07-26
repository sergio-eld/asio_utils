#pragma once

#include "asio_utils/traits.h"
#include "asio_utils/utility.h"

#include <type_traits>

#include <functional>

#include <atomic>
#include <memory>
#include <mutex>

// TODO: remove
#include <iostream>

namespace eld
{
    namespace custom
    {
        template<typename T>
        void cancel(T &t)
        {
            t.cancel();
        }

        /**
         * Customization point to get current config.
         * @tparam T
         * @param t
         * @return
         */
        template<typename T>
        constexpr auto config(const T &t)
        {
            return t.config();
        }

        template<typename ImplT, typename CompletionT>
        constexpr auto make_completion(ImplT &impl, CompletionT &&completion)
        {
            return impl.make_completion(std::forward<CompletionT>(completion));
        }

        /**
         * Customization point for asynchronous receive command.
         * @tparam BufferT
         * @tparam CompletionT
         * @tparam ConnectionT
         * @param connection
         * @param buffer
         * @param completion
         * @return
         */
        template<typename BufferT, typename CompletionT, typename ConnectionT>
        constexpr auto async_receive(ConnectionT &connection,
                                     BufferT &&buffer,
                                     CompletionT &&completion) noexcept
        {
            return connection.async_receive(std::forward<BufferT>(buffer),
                                            std::forward<CompletionT>(completion));
        }

        /**
         * Customization point for asynchronous send command.
         * Asynchronously sends the whole contents of the given buffer.
         * @tparam ConstBufferT
         * @tparam CompletionT
         * @tparam ConnectionT
         * @param connection
         * @param constBuffer
         * @param completion
         * @return
         */
        template<typename ConstBufferT, typename CompletionT, typename ConnectionT>
        constexpr auto async_send(ConnectionT &connection,
                                  ConstBufferT &&constBuffer,
                                  CompletionT &&completion) noexcept
        {
            return connection.async_send(std::forward<ConstBufferT>(constBuffer),
                                         std::forward<CompletionT>(completion));
        }

        template<typename ImplT, typename DirectionTagT>
        constexpr auto get_const_buffer(const ImplT &impl, DirectionTagT directionTag)
        {
            return impl.const_buffer(directionTag);
        }

        template<typename ImplT, typename DirectionTagT>
        constexpr auto get_mutable_buffer(ImplT &impl, DirectionTagT directionTag)
        {
            return impl.mutable_buffer(directionTag);
        }

        template<typename ImplT,
                 typename ErrorT,
                 typename TransferTypeT,
                 typename ConfigFromT,
                 typename ConfigToT>
        constexpr bool transfer_interrupted(const ImplT &impl,
                                            ErrorT &&error,
                                            size_t bytesTransferred,
                                            TransferTypeT transferType,
                                            const ConfigFromT &configConnectionFrom,
                                            const ConfigToT &configConnectionTo)
        {
            return impl.transfer_interrupted(std::forward<ErrorT>(error),
                                             bytesTransferred,
                                             transferType,
                                             configConnectionFrom,
                                             configConnectionTo);
        }

    }

    namespace detail
    {
        namespace detail
        {
            template<class T, class Tuple, std::size_t... I>
            constexpr T make_from_tuple_impl(Tuple &&t, std::index_sequence<I...>)
            {
                static_assert(
                    std::is_constructible<T, decltype(std::get<I>(std::declval<Tuple>()))...>(),
                    "T is not constructible from given Tuple");
                return T(std::get<I>(std::forward<Tuple>(t))...);
            }
        }   // namespace detail

        template<class T, class Tuple>
        constexpr T make_from_tuple(Tuple &&t)
        {
            return detail::make_from_tuple_impl<T>(
                std::forward<Tuple>(t),
                std::make_index_sequence<std::tuple_size<std::remove_reference_t<Tuple>>::value>{});
        }
    }

    namespace tag
    {
        struct direction_a_to_b_t
        {
        };
        struct direction_b_to_a_t
        {
        };
        struct direction_both_t
        {
        };
    }

    constexpr tag::direction_a_to_b_t direction_a_to_b;
    constexpr tag::direction_b_to_a_t direction_b_to_a;
    constexpr tag::direction_both_t direction_both;

    enum class transfer_type
    {
        send,
        receive
    };

    namespace generic
    {
        /**
         * \todo List of steps where a error can occur.
         *      - Per direction:
         *          - error on send
         *          - error on receive
         * Connection adapter Class.
         * @tparam ConnectionA
         * @tparam ConnectionB
         * @tparam DirectionT
         */
        template<typename ConnectionA, typename ConnectionB, typename DirectionT, typename ImplT>
        class connection_adapter
        {
        public:
            using connection_a_type = ConnectionA;
            using connection_b_type = ConnectionB;

            using direction_type = DirectionT;
            using implementation_t = ImplT;

            template<
                typename TupleParamsA,
                typename TupleParamsB,
                bool DefaultConstructibleImpl = std::is_default_constructible<implementation_t>(),
                typename std::enable_if<DefaultConstructibleImpl, int>::type * = nullptr>
            connection_adapter(TupleParamsA &&tupleParamsA, TupleParamsB &&tupleParamsB)
              : connectionA_(detail::make_from_tuple<connection_a_type>(tupleParamsA)),
                connectionB_(detail::make_from_tuple<connection_b_type>(tupleParamsB)),
                impl_()
            {
                async_run(direction_type());
            }

            template<typename TupleParamsA, typename TupleParamsB, typename TupleBufferParams>
            connection_adapter(TupleParamsA &&tupleArgsA,
                               TupleParamsB &&tupleArgsB,
                               TupleBufferParams &&tupleImplArgs)
              : connectionA_(detail::make_from_tuple<connection_a_type>(tupleArgsA)),
                connectionB_(detail::make_from_tuple<connection_b_type>(tupleArgsB)),
                impl_(detail::make_from_tuple<implementation_t>(tupleImplArgs))
            {
                async_run(direction_type());
            }

            // TODO: can a move constructor be used in a running state?
            connection_adapter(connection_adapter &&) noexcept = default;
            connection_adapter &operator=(connection_adapter &&) = delete;

            connection_adapter(const connection_adapter &) = delete;
            connection_adapter &operator=(const connection_adapter &) = delete;

            void cancel()
            {
                custom::cancel(connectionA_);
                custom::cancel(connectionB_);
            }

            constexpr auto config_a() const { return custom::config(connectionA_); }

            constexpr auto config_b() const { return custom::config(connectionB_); }

            // Do I need this?
            bool stopped() const { return !running_; }

            // Does it have to be public?
            template<typename CompletionT>
            auto stopped(CompletionT &&completionToken)
            {
                auto &&completion =
                    custom::make_completion(impl_, std::forward<CompletionT>(completionToken));
                onStopped_ = [handler = std::move(completion.handler)] { handler(); };

                return completion.result.get();
            }

            ~connection_adapter()
            {
                // TODO: use traits customization
                stopped(impl_.use_future).wait();
            }

        private:
            connection_a_type &get_connection_from(tag::direction_a_to_b_t) { return connectionA_; }
            connection_b_type &get_connection_to(tag::direction_a_to_b_t) { return connectionB_; }

            connection_b_type &get_connection_from(tag::direction_b_to_a_t) { return connectionB_; }
            connection_a_type &get_connection_to(tag::direction_b_to_a_t) { return connectionA_; }

            template<typename DirectionTagT>
            void async_receive(DirectionTagT directionTag)
            {
                custom::async_receive(
                    get_connection_from(directionTag),
                    custom::get_mutable_buffer(impl_, directionTag),
                    [this](const auto &error, size_t bytesTransferred)
                    {
                        const auto &configFrom =
                            custom::config(get_connection_from(DirectionTagT()));
                        const auto &configTo = custom::config(get_connection_to(DirectionTagT()));
                        using transfer_type =
                            std::integral_constant<transfer_type, transfer_type::receive>;

                        if (!custom::transfer_interrupted(impl_,
                                                          error,
                                                          bytesTransferred,
                                                          transfer_type(),
                                                          configFrom,
                                                          configTo))
                            return async_send(DirectionTagT());

                        // TODO: thread safety
                        // TODO: set stopped
                        if (onStopped_)
                        {
                            onStopped_();
                            onStopped_ = {};
                        }
                    });
            }

            template<typename DirectionTagT>
            void async_send(DirectionTagT directionTag)
            {
                custom::async_send(
                    get_connection_to(directionTag),
                    custom::get_const_buffer(impl_, directionTag),
                    [this](const auto &error, size_t bytesTransferred)
                    {
                        const auto &configFrom =
                            custom::config(get_connection_from(DirectionTagT()));
                        const auto &configTo = custom::config(get_connection_to(DirectionTagT()));
                        using transfer_type =
                            std::integral_constant<transfer_type, transfer_type::send>;

                        if (!custom::transfer_interrupted(impl_,
                                                          error,
                                                          bytesTransferred,
                                                          transfer_type(),
                                                          configFrom,
                                                          configTo))
                            return async_receive(DirectionTagT());

                        // TODO: thread safety
                        // TODO: set stopped
                        if (onStopped_)
                        {
                            onStopped_();
                            onStopped_ = {};
                        }
                    });
            }

            template<typename DirectionTagT>
            void async_run(DirectionTagT directionTag)
            {
                async_receive(directionTag);
            }

            void async_run(tag::direction_both_t)
            {
                async_run(direction_a_to_b);
                async_run(direction_b_to_a);
            }

        private:
            connection_a_type connectionA_;
            connection_b_type connectionB_;
            std::atomic_bool running_;

            implementation_t impl_;
            std::function<void()> onStopped_;
        };

    }

    template<typename ConnectionA,
             typename ConnectionB,
             typename DirectionTag,
             typename TupleParamsA,
             typename TupleParamsB,
             typename CompletionT>
    auto make_connection_adapter(DirectionTag directionTag,
                                 TupleParamsA &&tupleParamsA,
                                 TupleParamsB &&tupleParamsB,
                                 CompletionT &&completionToken)
    {
    }

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

        constexpr tag::b_to_a reverse(tag::a_to_b) { return {}; }

        constexpr tag::a_to_b reverse(tag::b_to_a) { return {}; }

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

            {
                std::lock_guard<std::mutex> lg{ get_mutex(directionTag) };
                const auto &currentState = get_state(directionTag);

                auto &connectionFrom = get_source_connection(directionTag);
                const auto configFrom = std::forward<ConfigTA>(a_configFrom);
                const auto &configFromCurrent = implementation_type::get_config(connectionFrom);

                auto &connectionTo = get_destination_connection(directionTag);
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

#pragma message "connection_adapter::async_run invalid implementation"
                // TODO: this must call current completion handler
                if (currentState != connection_state::idle)
                    stop_active_connection(directionTag);

                if (!equalConfigsFrom)
                    impl_.configure(connectionFrom, configFrom);

                if (!equalConfigsTo)
                    impl_.configure(connectionTo, configTo);

                pImpl_->completion_handler_ = std::move(completion);

                get_state(directionTag) = connection_state::reading;
                async_receive(DirectionTag());
            }

            return result.get();
        }

        template<typename DirectionTag, typename CompletionT>
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

            // TODO: make futures that wait in destructor
            futureAToB.wait();
            futureBToA.wait();
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
                             [this](const error_type &errorCode, size_t /*bytesSent*/)
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
        ImplT && /*adapterConfig*/)
    {
        return connection_adapter_basic<ConnectionTA, ConnectionTB, ImplT>(
            std::forward<ConnectionTA>(connectionL),
            std::forward<ConnectionTB>(connectionR));
    }

    template<typename ConnectionTA,
             typename ConnectionTB,
             typename ImplT,
             typename =
                 typename std::enable_if<std::is_default_constructible<ConnectionTA>::value &&
                                         std::is_default_constructible<ConnectionTA>::value>::type>
    connection_adapter_basic<ConnectionTA, ConnectionTB, ImplT> make_connection_adapter(
        ImplT && /*adapterConfig*/)
    {
        return connection_adapter_basic<ConnectionTA, ConnectionTB, ImplT>(
            std::forward<ConnectionTA>(ConnectionTA()),
            std::forward<ConnectionTB>(ConnectionTB()));
    }
}
