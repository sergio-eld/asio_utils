#pragma once

#include <type_traits>

namespace eld
{
    namespace util
    {
        template<typename...>
        using void_t = void;

        template<class...>
        struct conjunction : std::true_type
        {
        };
        template<class B1>
        struct conjunction<B1> : B1
        {
        };
        template<class B1, class... Bn>
        struct conjunction<B1, Bn...> : std::conditional_t<bool(B1::value), conjunction<Bn...>, B1>
        {
        };

        template<class...>
        struct disjunction : std::false_type
        {
        };
        template<class B1>
        struct disjunction<B1> : B1
        {
        };
        template<class B1, class... Bn>
        struct disjunction<B1, Bn...> : std::conditional_t<bool(B1::value), B1, disjunction<Bn...>>
        {
        };

        template<typename T>
        using require_default_constructible_t =
            typename std::enable_if<std::is_default_constructible<T>::value>::type;

        template<typename A, typename B, bool val>
        using require_type_equality_t =
            typename std::enable_if<val ? std::is_same<A, B>::value :
                                          !std::is_same<A, B>::value>::type;

        template <typename T, typename ...ArgsT>
        using is_one_of = conjunction<std::is_same<T, ArgsT>...>;

    }
}