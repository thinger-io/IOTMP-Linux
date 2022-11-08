#ifndef THINGER_HTTP_COOKIE_HPP
#define THINGER_HTTP_COOKIE_HPP

#include <string>

namespace thinger::http{

    class http_cookie {
    public:
        // constructors
        http_cookie() = default;
        virtual ~http_cookie() = default;

        // setters
        static http_cookie parse(const std::string& cookie);

        // getters
        const std::string &getName() const;
        const std::string &getValue() const;
        const std::string &getPath() const;
        const std::string &getDomain() const;
        int64_t getExpire() const;
        bool isSecure() const;
        bool isHttpOnly() const;

    private:
        std::string name_;
        std::string value_;
        std::string path_;
        std::string domain_;
        int64_t expire_  = 0;
        bool secure_     = false;
        bool http_only_  = false;
    };

}

#endif