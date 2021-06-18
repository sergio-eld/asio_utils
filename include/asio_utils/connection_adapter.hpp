#pragma once

#include "asio_utils/asio_implementation/connection_asio_impl.h"
#include "asio_utils/generic/connection_adapter.h"

/**
 * This utility provides an adapter class to resend byte arrays from one type of
 * device, socket or connection to another
 */

namespace eld
{
    template<typename ConnectionTA, typename ConnectionTB>
    connection_adapter_basic<ConnectionTA, ConnectionTB, impl_asio::connection_asio_impl>
        make_connection_adapter(ConnectionTA &&connectionL, ConnectionTB &&connectionR)
    {
        return connection_adapter_basic<ConnectionTA,
                                        ConnectionTB,
                                        impl_asio::connection_asio_impl>(
            std::forward<ConnectionTA>(connectionL),
            std::forward<ConnectionTB>(connectionR));
    }

    template<typename ConnectionTA,
             typename ConnectionTB,
             typename =
                 typename std::enable_if<std::is_default_constructible<ConnectionTA>::value &&
                                         std::is_default_constructible<ConnectionTA>::value>::type>
    connection_adapter_basic<ConnectionTA, ConnectionTB, impl_asio::connection_asio_impl>
        make_connection_adapter()
    {
        return connection_adapter_basic<ConnectionTA,
                                        ConnectionTB,
                                        impl_asio::connection_asio_impl>(
            std::forward<ConnectionTA>(ConnectionTA()),
            std::forward<ConnectionTB>(ConnectionTB()));
    }

}
