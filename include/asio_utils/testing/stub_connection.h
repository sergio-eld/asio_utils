#pragma once

namespace eld
{
    namespace testing
    {
        class stub_connection
        {
        public:

            struct endpoint
            {
                int v = 0;

                constexpr bool operator==(const endpoint &other) const { return v == other.v; }
                constexpr bool operator!=(const endpoint &other) const { return !(*this == other); }
            };

            using config_type = endpoint;

            stub_connection(const stub_connection &) = default;
            stub_connection(stub_connection &&) = default;

            stub_connection &operator=(const stub_connection &) = default;
            stub_connection &operator=(stub_connection &&) = default;





        };
    }
}