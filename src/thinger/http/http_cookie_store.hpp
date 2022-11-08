#ifndef THINGER_HTTP_COOKIE_STORE_HPP
#define THINGER_HTTP_COOKIE_STORE_HPP

#include <unordered_map>
#include "http_cookie.hpp"
#include "http_headers.hpp"

namespace thinger::http {

    class http_cookie_store {
    public:
        http_cookie_store();
        virtual ~http_cookie_store();
        bool update_from_headers(const http_headers& headers);
        std::string get_cookie_string() const;
    private:
        std::unordered_map<std::string, http_cookie> cookies_;
    };

}

#endif