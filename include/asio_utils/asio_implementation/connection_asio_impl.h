#pragma once

#include "asio_utils/generic/generic_impl.h"
#include "asio_utils/traits.h"

#include <asio.hpp>

#include <memory>
#include <string>

namespace eld
{
    struct udp_socket_config
    {
        asio::ip::udp::endpoint local_endpoint,   //
            remote_endpoint;
    };

    constexpr inline bool operator==(const udp_socket_config& lhv, const udp_socket_config &rhv)
    {
        return std::tie(lhv.local_endpoint, lhv.remote_endpoint) ==
            std::tie(rhv.local_endpoint, rhv.remote_endpoint);
    }

    struct tcp_socket_config
    {
        asio::ip::tcp::endpoint local_endpoint,   //
            remote_endpoint;
    };

    constexpr inline bool operator==(const tcp_socket_config& lhv, const tcp_socket_config &rhv)
    {
        return std::tie(lhv.local_endpoint, lhv.remote_endpoint) ==
               std::tie(rhv.local_endpoint, rhv.remote_endpoint);
    }

    struct serial_port_config
    {
        //        std::string device_name; // is this necessary for reconfiguration?
        asio::serial_port::baud_rate baud_rate;
        asio::serial_port::flow_control flow_control;
        asio::serial_port::parity parity;
        asio::serial_port::stop_bits stop_bits;
        asio::serial_port::character_size character_size;
    };

    namespace traits
    {
        template<>
        struct connection<asio::ip::udp::socket>
        {
            using config_type = eld::udp_socket_config;
        };

        template<>
        struct connection<asio::ip::tcp::socket>
        {
            using config_type = eld::tcp_socket_config;
        };

        template<>
        struct connection<asio::serial_port>
        {
            using config_type = serial_port_config;
        };

    }

    namespace impl_asio
    {
        namespace traits
        {
        }

        namespace detail
        {
            // TODO: make SFINAE overload that uses composed asio::async_write
            // TODO: implement
            template<typename ConnectionT>
            class async_write
            {
            public:
                async_write(ConnectionT &connection)   //
                  : pState_(std::make_shared<state>(connection))
                {
                }

                template<typename ConstBuffer, typename WriteHandler>
                auto operator()(ConstBuffer &&constBuffer, WriteHandler &&writeHandler)
                {
                }

            private:
                struct state
                {
                    state(ConnectionT &connection, size_t bytesToSend) : connection_(connection) {}

                    ConnectionT &connection_;
                    size_t bytesToSend_;
                };

                std::shared_ptr<state> pState_;
            };
        }

        struct connection_asio_impl
        {
            using error_type = asio::error_code;
            using buffer_type = std::array<uint8_t, 2048>;

            struct error
            {
                constexpr static auto operation_aborted() { return asio::error::operation_aborted; }

                constexpr static auto already_started() { return asio::error::already_started; }
            };

            template<typename ConnectionT>
            static constexpr bool remote_host_has_disconnected(const ConnectionT &,
                                                               const error_type &errorCode)
            {
                return errorCode == asio::error::eof ||   //
                       errorCode == asio::error::connection_reset;
            }

            template<typename ConfigTL, typename ConfigTR>
            static constexpr bool compare_configs(const ConfigTL &lhv, const ConfigTR &rhv)
            {
                return impl::default_false_compare<ConfigTL, ConfigTR>::value(lhv, rhv);
            }

            //////////////////////////////////////////////
            // class methods
            //////////////////////////////////////////////

            template<typename ConnectionT>
            static constexpr typename eld::traits::connection<ConnectionT>::config_type get_config(
                const ConnectionT &connection)
            {
                return connection.get_config();
            }

            template<typename T>
            static constexpr void cancel(T &t)
            {
                t.cancel();
            }

            template<typename T, typename ConfigT>
            static void configure(T &t, ConfigT &&config)
            {
                t.configure(std::forward<ConfigT>(config));
            }

            // TODO: check completion signature
            template<typename Connection, typename MutableBuffer, typename CompletionT>
            static auto async_receive(Connection &connection,
                                      MutableBuffer &&mutableBuffer,
                                      CompletionT &&completion)
            {
                return connection.async_receive(
                    asio::buffer(std::forward<MutableBuffer>(mutableBuffer)),
                    std::forward<CompletionT>(completion));
            }

            // TODO: check completion signature
            template<typename Connection, typename ConstBuffer, typename CompletionT>
            static auto async_send(Connection &connection,
                                   ConstBuffer &&constBuffer,
                                   CompletionT &&completion)
            {
                //                return asio::async_write(connection,
                //                                         std::forward<ConstBuffer>(constBuffer),
                //                                         std::forward<CompletionT>(completion));

                // TODO: implement and use detail::async_write

                return connection.async_send(std::forward<ConstBuffer>(constBuffer),
                                             std::forward<CompletionT>(completion));
            }
        };

        template<>
        tcp_socket_config connection_asio_impl::get_config(const asio::ip::tcp::socket &connection)
        {
            // workaround for socket that has not been connected yet
            asio::error_code ec{};
            auto localEndpoint = connection.local_endpoint(ec);
            if (ec == asio::error::bad_descriptor)
                localEndpoint = asio::ip::tcp::endpoint();
            else
                throw asio::system_error(ec);

            auto remoteEndpoint = connection.remote_endpoint(ec);
            if (ec == asio::error::bad_descriptor)
                remoteEndpoint = asio::ip::tcp::endpoint();
            else
                throw asio::system_error(ec);

            return { std::move(localEndpoint), std::move(remoteEndpoint) };
        }

        // TODO: check this implementation
        template<>
        void connection_asio_impl::configure(asio::ip::tcp::socket &socket,
                                             const tcp_socket_config &config)
        {
            // TODO: is it necessary to close?
            if (!socket.is_open())
                socket.open(config.local_endpoint.protocol());
            socket.bind(config.local_endpoint);
            socket.connect(config.remote_endpoint);
        }

        template<>
        udp_socket_config connection_asio_impl::get_config(const asio::ip::udp::socket &connection)
        {
            // workaround for socket that has not been connected yet
            asio::error_code ec{};
            auto localEndpoint = connection.local_endpoint(ec);
            if (ec == asio::error::bad_descriptor)
                localEndpoint = asio::ip::udp::endpoint();
            else
                throw asio::system_error(ec);

            auto remoteEndpoint = connection.remote_endpoint(ec);
            if (ec == asio::error::bad_descriptor)
                remoteEndpoint = asio::ip::udp::endpoint();
            else
                throw asio::system_error(ec);

            return { std::move(localEndpoint), std::move(remoteEndpoint) };
        }

        template<>
        void connection_asio_impl::configure(asio::ip::udp::socket &socket,
                                             const udp_socket_config &config)
        {
            // TODO: is it necessary to close?
            if (!socket.is_open())
                socket.open(config.local_endpoint.protocol());

            socket.bind(config.local_endpoint);
            socket.connect(config.remote_endpoint);
        }

        template<>
        serial_port_config connection_asio_impl::get_config(const asio::serial_port &serialPort)
        {
            serial_port_config config;
            serialPort.get_option(config.baud_rate);
            serialPort.get_option(config.flow_control);
            serialPort.get_option(config.parity);
            serialPort.get_option(config.stop_bits);
            serialPort.get_option(config.character_size);

            return config;
        }

        // TODO: is it necessary to close serial port to change the settings?
        template<>
        void connection_asio_impl::configure(asio::serial_port &serialPort,
                                             const serial_port_config &config)
        {
            //            if (serialPort.is_open())
            //                serialPort.close();

            serialPort.set_option(config.baud_rate);
            serialPort.set_option(config.flow_control);
            serialPort.set_option(config.parity);
            serialPort.set_option(config.stop_bits);
            serialPort.set_option(config.character_size);

            //            serialPort.open();
        }

    }
}
