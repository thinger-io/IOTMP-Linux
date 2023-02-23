#include "http_cookie.hpp"
#include <boost/regex.hpp>

namespace thinger::http {

    http_cookie http_cookie::parse(const std::string &cookie_string) {
        http_cookie newCookie;

        std::string::const_iterator start = cookie_string.begin();
        std::string::const_iterator end   = cookie_string.end();

        if(start==end) return newCookie;

        // todo move to a static regex
        static boost::regex cookie_regex("(.*?)=(.*?)($|;|,(?! ))");
        boost::smatch what;

        while (boost::regex_search(start, end, what, cookie_regex))
        {
            std::string key(what[1].first, what[1].second);
            std::string value(what[2].first, what[2].second);

            //std::cout << "Parsed Cookie: '" << key << "' : '" << value << "'" << std::endl;
            if(newCookie.name_.empty()){
                newCookie.name_ = key;
                newCookie.value_ = value;
            }else{
                // TODO parse other attributes
            }

            start = what[0].second;
        }

        return newCookie;
    }

    const std::string& http_cookie::getName() const{
        return name_;
    }

    const std::string& http_cookie::getValue() const{
        return value_;
    }

    int64_t http_cookie::getExpire() const{
        return expire_;
    }

    const std::string& http_cookie::getPath() const{
        return path_;
    }

    const std::string& http_cookie::getDomain() const{
        return domain_;
    }

    bool http_cookie::isSecure() const{
        return secure_;
    }

    bool http_cookie::isHttpOnly() const{
        return http_only_;
    }
}