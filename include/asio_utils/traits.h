#pragma once

#include <type_traits>

#include "asio_utils/utility.h"

namespace eld
{
    namespace traits
    {
        template<typename T>
        struct connection
        {
            using config_type = typename T::config_type;
        };
    }

    /**
     * Contains customization points with default implementations.
     */
    namespace custom
    {
        namespace detail
        {


        }

        // TODO: add make_completion function

    }
}