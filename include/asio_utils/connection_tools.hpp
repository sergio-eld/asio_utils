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

/*
 * continuation specification:
 * First composed operation receives a callable handler object with a signature:
 * void CompletionHandler::operator()(Signature);
 *
 * chained_continuation stores a callable "raw handler" that will be invoked.
 * If no handler is set it stores the result into a shared state.
 * Stored result in shared state is immediately transferred to the next callable with
 * the same Signature once a new callable has been assigned.
 * ?Callable is invoked only once?
 *
 * Result of a CompletionHandler is specialized with the first callback Signature.
 *
 * auto chainedContinuation = async_composed_operation(Args..., use_chained_token)
 * auto nextChainedContinuation = async_continuation_chain(std::move(chainedContinuation), nextAsyncCallable);
 */
namespace eld
{
    struct chained_completion_t
    {
    };

    chained_completion_t use_chained_completion;

    /*
     * 1) chain_handler invokes current node
     * 2) current node invokes next node
     * 3) next node invokes new operation
     *
     * async_op -> handler -> node -> next_node -> async_op -> handler
     * node shares ownership of next_node
     * next_node shares ownership of async_op
     *
     * TODO: ownership of previous nodes?
     */

    template<typename Signature>
    struct chained_continuation_node_next_base;

    template<typename ... Args>
    struct chained_continuation_node_next_base<void(Args...)>
    {
        using signature_t = void(Args...);

        virtual ~chained_continuation_node_next_base() = default;

        virtual void operator()(Args...) = 0;
    };

    template<typename SignatureInput, typename Callable>
    class chained_continuation_node_next;

    // Callable must provide void operator()(void(Args...));
    template<typename ... Args, typename Callable>
    class chained_continuation_node_next<void(Args...), Callable> :
            chained_continuation_node_next_base<void(Args...)>
    {
    public:

        chained_continuation_node_next() = delete;

        template<typename CallableT>
        explicit chained_continuation_node_next(CallableT &&callable)
                : callable_(std::forward<Callable>(callable))
        {}

        // TODO: no move or copy

        // TODO: move args to callable?
        void operator()(Args... args) override
        {
            callable_(args...);
        }

    private:
        Callable callable_;
    };

    template<typename Signature>
    class chained_continuation_node;

    template<typename ... Args>
    class chained_continuation_node<void(Args...)>
    {
    public:
        using signature_t = void(Args...);

        // next_t is not needed! Next continuation is produced via async_initiate(?)
        using next_t = chained_continuation_node_next_base<signature_t>;

        // TODO: force initialization with defaulted callback?

        // invoke next node or store result
        template<typename ... ArgsT>
        void operator()(ArgsT &&... args)
        {
            std::lock_guard<std::mutex> lockGuard{mutex_};
            if (next_)
            {
                (*next_)(std::forward<ArgsT>(args)...);
                return;
            }

            // TODO: store result
            storedResult_ = std::make_tuple(std::forward<ArgsT>(args)...);
            ready_ = true;
        }


        void assign_next(std::shared_ptr<next_t> next)
        {
            assert(next && "Trying to assign an empty next node");
            {
                std::lock_guard<std::mutex> lockGuard{mutex_};
                next_ = next;
            }
            if (ready_)
                self_invoke(storedResult_, std::make_index_sequence<sizeof...(Args)>());
        }

    private:
        std::mutex mutex_;
        std::shared_ptr<next_t> next_;

        // no references allowed here
        std::tuple<Args...> storedResult_;
        std::atomic_bool ready_{false};

        template<typename ... ArgsT, size_t ... Indx>
        void self_invoke(std::tuple<ArgsT...> &tuple, std::index_sequence<Indx...>)
        {
            (*this)(std::forward<std::tuple_element_t<Indx, decltype(storedResult_)>>(
                    std::get<Indx>(tuple))...);
        }
    };


    template<typename Signature>
    class chained_continuation;

    template<typename Signature>
    class chained_continuation_handler;

    template<typename ... Args>
    class chained_continuation_handler<void(Args...)>
    {
    public:
        using sugnature_t = void(Args...);

        explicit chained_continuation_handler(chained_completion_t)
                : node_(std::make_shared<chained_continuation_node<sugnature_t>>())
        {}

        template<typename ... ArgsT>
        void operator()(ArgsT ... args)
        {
            assert(node_ && "Implementation was not initialized!");
            (*node_)(std::forward<ArgsT>(args)...);
        }

    private:
        std::shared_ptr<chained_continuation_node<sugnature_t>>
                node_;

        friend class chained_continuation<sugnature_t>;

        auto get_node()
        {
            return node_;
        }
    };

    // stores current node
    template<typename Signature>
    class chained_continuation
    {
    public:
        explicit chained_continuation(chained_continuation_handler<Signature> &handler)
                : node_(handler.get_node())
        {}

        chained_continuation(const chained_continuation &) = delete;

        chained_continuation &operator=(const chained_continuation &) = delete;

        chained_continuation(chained_continuation &&) noexcept = default;

        chained_continuation &operator=(chained_continuation &&) noexcept = default;

        // TODO: require Callable to have compatible Signature
//        template<typename Callable, typename CompletionToken>
//        auto operator()(Callable &&nextCall, CompletionToken &&token)
//        {
//            // TODO: return next chained_continuation
//        }

        bool valid() const
        {
            return node_;
        }

        operator bool() const
        {
            return valid();
        }

        auto get_node()
        {
            return node_;
        }

        /*
         * TODO:
         *  chained_continuation<DeducedSignature> operator()(InputSignature &&callable)
         *  chained_continuation is created as a result of async_initiate.
         *  callable is stored or invoked instantly if the result is ready
         *
         */
        //

    private:

        // TODO: this is not a node, it is just a callback!
        std::shared_ptr<chained_continuation_node<Signature>> node_;
    };
}

namespace asio
{
    template<typename Signature>
    class async_result<eld::chained_completion_t,
            Signature>
    {
    public:
        using completion_handler_type = eld::chained_continuation_handler<Signature>;
        using return_type = eld::chained_continuation<Signature>;

        explicit async_result(completion_handler_type &handler)
                : continuation_(handler)
        {}

        return_type get()
        {
            return std::move(continuation_);
        }

        template<typename Initiation,
                //typename RawCompletionToken,
                typename... Args>
        static return_type initiate(
                Initiation &&initiation,
//                RawCompletionToken && token,
                eld::chained_completion_t token,
                Args &&... args)
        {
            // TODO: what?
            completion_handler_type handler{token};
            return_type chainedContinuation{handler};
            std::forward<Initiation>(initiation)(std::move(handler),
                                                 std::forward<Args>(args)...);

            return chainedContinuation;
        }

        // TODO: implement this
//        template<typename Initiation,
//                typename NextSignature,
//                typename ... Args>
//        static eld::chained_continuation<NextSignature> initiate(Initiation &&initiation,
//                                                                 return_type &&prevContinuation,
//                                                                 Args &&... args)
//        {
//            using next_continuation_t = eld::chained_continuation<NextSignature>;
//            using next_chained_handler_t = eld::chained_continuation_handler<NextSignature>;
//
//            // initiation is initialized with next_chained_handler
//
//            // create next_node that will invoke initiation
//
//
//
////            next_chained_handler_t nextHandler{eld::use_chained_completion};
////            next_continuation_t nextContinuation{nextHandler};
////
////            return_type prev = std::move(prevContinuation);
////            prev.get_node()->assign_next(nextContinuation.get_node());
////
////            completion_handler_type handler{token};
////            return_type chainedContinuation{handler};
////            std::forward<Initiation>(initiation)(std::move(handler),
////                                                 std::forward<Args>(args)...);
//        }

    private:
        return_type continuation_;
    };
}

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

        // https://stackoverflow.com/a/51188325/9363996
        template<typename F, typename... Args>
        struct is_invocable :
                std::is_constructible<
                        std::function<void(Args ...)>,
                        std::reference_wrapper<typename std::remove_reference<F>::type>
                >
        {
        };

        // https://stackoverflow.com/a/51188325/9363996
        template<typename R, typename F, typename... Args>
        struct is_invocable_r :
                std::is_constructible<
                        std::function<R(Args ...)>,
                        std::reference_wrapper<typename std::remove_reference<F>::type>
                >
        {
        };

        template<typename /*Callable*/, typename /*Signature*/>
        struct has_signature : std::false_type
        {
        };

        // TODO: proper implementation
        template<typename Callable, typename ... Args>
        struct has_signature<Callable, void(Args...)> : is_invocable<Callable, Args...>
        {
        };


        template<typename Callable, typename Signature>
        using require_signature = typename
        std::enable_if<has_signature<Callable, Signature>::value>::type;

        template<typename T, typename F>
        using require_constructible = typename
        std::enable_if<std::is_constructible<T, F>::value>::type;

        template<typename T, typename R>
        using require_same = typename std::enable_if<std::is_same<T, R>::value>::type;

    }


    // TODO: move to eld::detail?
    template<typename Connection, typename CompletionHandler,
            typename = detail::require_signature<CompletionHandler,
                    void(const asio::error_code &)>>
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

            // TODO: get rid of move
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

    // TODO: specification for this function
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
        using result_t = asio::async_result<std::decay_t<CompletionToken>,
                void(asio::error_code)>;
        using completion_t = typename result_t::completion_handler_type;

        completion_t completion{std::forward<CompletionToken>(completionToken)};
        result_t result{completion};

        auto composedConnectionAttempt = make_composed_connection_attempt(connection,
                                                                          std::forward<completion_t>(completion),
                                                                          std::forward<Callable>(stopOnError));
        composedConnectionAttempt(std::forward<Endpoint>(endpoint),
                                  attempts,
                                  std::forward<Duration>(timeout));

        return result.get();
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

        using result_t = asio::async_result<std::decay_t<CompletionToken>,
                void(asio::error_code)>;
        using completion_t = typename result_t::completion_handler_type;

        completion_t completion{std::forward<CompletionToken>(completionToken)};
        result_t result{completion};

        auto composedConnectionAttempt = make_composed_connection_attempt(connection,
                                                                          std::forward<completion_t>(completion),
                                                                          std::forward<Callable>(stopOnError));
        composedConnectionAttempt(std::forward<Endpoint>(endpoint),
                                  std::forward<Duration>(timeout));

        return result.get();
    }

    // asynchronous persistent operation
    template<typename Connection, typename CompletionHandler>
    class async_send_queue
    {
    public:
        using connection_t = Connection;
        using final_completion_t = CompletionHandler;
        using completion_signature_t = void(asio::error_code);
        using stop_on_error_signature_t = bool(const asio::error_code &, size_t);
        using send_completion_signature_t = void(const asio::error_code &, size_t);

        using send_completion_handler_t = std::function<send_completion_signature_t>;
        using send_command_t = std::pair<asio::const_buffer,
                send_completion_handler_t>;

        // TODO: get_executor
        using executor_type = typename connection_t::executor_type;

        executor_type get_executor()
        {
            return pImpl_->connection_.get_executor();
        }


        template<typename CompletionHandlerT,
                typename = detail::require_constructible<final_completion_t, CompletionHandlerT>>
        async_send_queue(connection_t &connection,
                         CompletionHandlerT &&completionHandler)
                : pImpl_(std::make_shared<impl>(connection,
                                                std::forward<CompletionHandlerT>(completionHandler)))
        {}

        template<typename CompletionHandlerT, typename Callable,
                typename = detail::require_constructible<final_completion_t, CompletionHandlerT>,
                typename = detail::require_constructible<std::function<stop_on_error_signature_t>, Callable>>
        async_send_queue(connection_t &connection,
                         CompletionHandlerT &&completionHandler,
                         Callable &&stopOnError)
                : pImpl_(std::make_shared<impl>(connection,
                                                std::forward<CompletionHandlerT>(completionHandler),
                                                std::forward<Callable>(stopOnError)))
        {}

        async_send_queue(async_send_queue &&) noexcept = default;

        async_send_queue &operator=(async_send_queue &&) noexcept = default;

        // public method to send, maybe provide a final completion handler here?
        template<typename ConstBuffer, typename CompletionToken>
        auto asyncSend(ConstBuffer &&buffer, CompletionToken &&token)
        {
            assert(pImpl_ && "Invalid async_send_queue: pImpl_ is nullptr!");

            using result_t = asio::async_result<std::decay_t<CompletionToken>,
                    send_completion_signature_t>;
            using completion_t = typename result_t::completion_handler_type;

            completion_t completion{std::forward<CompletionToken>(token)};
            result_t result{completion};

            // TODO: check if executor is provided and bind?
            auto onSentHandler =
                    [completion = std::move(completion)](const asio::error_code &errorCode,
                                                         size_t bytesSent)
                    {
                        completion(errorCode, bytesSent);
                    };

            std::lock_guard<std::mutex> lockGuard{pImpl_->mutex_};
            if (pImpl_->commands_.empty() &&
                !pImpl_->running_)
            {
                pImpl_->running_ = true;
                asyncSend(send_command_t(std::forward<ConstBuffer>(buffer),
                                         std::move(onSentHandler)));
                return result.get();
            }

            pImpl_->commands_.emplace(std::forward<ConstBuffer>(buffer),
                                      std::move(onSentHandler));
            return result.get();
        }

    private:

        struct impl
        {
            connection_t &connection_;
            final_completion_t finalCompletion_;
            std::function<stop_on_error_signature_t> stopOnError_;

            template<typename CompletionHandlerT>
            impl(connection_t &connection, CompletionHandlerT &&handler)
                    : connection_(connection),
                      finalCompletion_(std::forward<CompletionHandlerT>(handler))
            {}

            template<typename CompletionHandlerT, typename Callable>
            impl(connection_t &connection,
                 CompletionHandlerT &&handler,
                 Callable &&stopOnError)
                    : connection_(connection),
                      finalCompletion_(std::forward<CompletionHandlerT>(handler)),
                      stopOnError_(std::forward<Callable>(stopOnError))
            {}


            std::queue<send_command_t> commands_;
            std::mutex mutex_;

            // TODO: get rid of running?
            std::atomic_bool running_{false};
        };

        std::shared_ptr<impl> pImpl_;

        void lockStop()
        {
            std::lock_guard<std::mutex> lockGuard{pImpl_->mutex_};
            pImpl_->running_ = false;
        }

        // TODO: refactor?
        void asyncSend(send_command_t &&command)
        {
            asio::const_buffer& buffer = command.first;

            // TODO: use operator() of this to handle result
            asio::async_write(pImpl_->connection_, buffer,
                              [this, command =
                              std::move(command.second)](const asio::error_code &errorCode,
                                                         size_t bytesSent)
                              {
                                  // notify user about the completion of single composed operation
                                  command(errorCode, bytesSent);

                                  // stop case, ignoring user's opinion whether he wants to proceed
                                  if (errorCode == asio::error::operation_aborted)
                                  {
                                      if (pImpl_->stopOnError_)
                                          pImpl_->stopOnError_(errorCode, bytesSent);
                                      lockStop();
                                      pImpl_->finalCompletion_(errorCode);
                                      return;
                                  }

                                  // stop requested by user
                                  if (pImpl_->stopOnError_ && pImpl_->stopOnError_(errorCode, bytesSent))
                                  {
                                      lockStop();
                                      // TODO: special error code?
                                      pImpl_->finalCompletion_(asio::error::operation_aborted);
                                      return;
                                  }

                                  // TODO: other errors? continue if connection was lost?
                                  if (!errorCode)
                                  {
                                      std::lock_guard<std::mutex> lockGuard{pImpl_->mutex_};
                                      if (pImpl_->commands_.empty())
                                      {
                                          pImpl_->running_ = false;
                                          return;
                                      }

                                      send_command_t dispatchedCommand = std::move(pImpl_->commands_.front());
                                      pImpl_->commands_.pop();

                                      // TODO: use dispatch?
                                      asio::dispatch(get_executor(), [this,
                                              dispatchedCommand = std::move(dispatchedCommand)]() mutable
                                      {
                                          asyncSend(std::move(dispatchedCommand));
                                      });

                                      // old code without using dispatch
                                      // asyncSend(std::move(pImpl_->commands_.front()));
                                      // pImpl_->commands_.pop();
                                      return;
                                  }

                                  // unrecoverable error case
                                  lockStop();
                                  pImpl_->finalCompletion_(errorCode);
                                  return;
                              });
        }
    };

    namespace detail
    {
        template<typename T, typename R>
        constexpr T get_combined(T &&t, R &&, /*R is_void*/ std::true_type)
        {
            return std::forward<T>(t);
        }

        template<typename T, typename R>
        constexpr auto get_combined(T &&t, R &&r, std::false_type)
        {
            return std::make_tuple(std::forward<T>(t),
                                   std::forward<R>(r));
        }

        template<typename T, typename R>
        constexpr auto get_combined(T &&t, R &&r)
        {
            return get_combined(std::forward<T>(t), std::forward<R>(r),
                                std::is_void<decltype(std::declval<R>().get())>());
        }
    }

    // TODO: specializations for non-callback tokens
    template<typename Connection, typename CompletionToken>
    auto make_async_send_queue(Connection &connection, CompletionToken &&finalToken)
    {
        using result_t = asio::async_result<std::decay_t<CompletionToken>,
                void(asio::error_code)>;
        using completion_t = typename result_t::completion_handler_type;

        completion_t completion{std::forward<CompletionToken>(finalToken)};
        result_t result{completion};

        return detail::get_combined(async_send_queue<Connection, completion_t>(connection,
                                                                               std::move(completion)),
                                    std::move(result));
    }

    template<typename T>
    T &unwrap(T &wrappedQueue)
    {
        return wrappedQueue;
    }

    template<typename T, typename Res>
    T &unwrap(std::tuple<T, Res> &wrappedQueue)
    {
        return std::get<0>(wrappedQueue);
    }

}