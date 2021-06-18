#pragma once

#include "asio_utils/utility.h"

namespace eld
{
    namespace impl
    {
        template<typename TL, typename TR, typename /*has_operator_==*/ = void>
        struct default_false_compare
        {
            constexpr static bool value(const TL &, const TR &) { return false; }
        };

        template<typename TL, typename TR>
        struct default_false_compare<
            TL,
            TR,
            util::void_t<decltype(std::declval<TL>() == std::declval<TR>())>>
        {
            constexpr static bool value(const TL &lhv, const TR &rhv) { return lhv == rhv; }
        };
    }
}