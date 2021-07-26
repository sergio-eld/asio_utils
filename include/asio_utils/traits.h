#pragma once

#include <type_traits>

#include "asio_utils/utility.h"

namespace eld
{
    namespace traits
    {
        template <typename T>
        struct connection
        {
            using traits = typename T::traits;
        };
    }
}