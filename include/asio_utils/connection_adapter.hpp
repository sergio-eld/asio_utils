#pragma once

#include "asio_utils/generic/connection_adapter.h"
#include "asio_utils/asio_implementation/connection_asio_impl.h"

/**
 * This utility provides an adapter class to resend byte arrays from one type of
 * device, socket or connection to another
 */

namespace eld
{
    // TODO: move customization

    template<typename ConnectionTA, typename ConnectionTB>
    connection_adapter_basic<ConnectionTA, ConnectionTB, impl_asio::connection_asio_impl>
        make_connection_adapter(ConnectionTA &&connectionL, ConnectionTB &&connectionR)
    {
        return connection_adapter_basic<ConnectionTA, ConnectionTB, impl_asio::connection_asio_impl>(
            std::forward<ConnectionTA>(connectionL),
            std::forward<ConnectionTB>(connectionR));
    }
}
