#ifndef THINGER_HTTP_HEADERS_HPP
#define THINGER_HTTP_HEADERS_HPP

#include <algorithm>
#include <boost/logic/tribool.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include "http_frame.hpp"

namespace thinger::http{

    namespace misc_strings {
        static const std::string name_value_separator{": "};
        static const std::string crlf = "\r\n";
        static const std::string lf = "\n";
        static const std::string space = " ";
        static const std::string http_1_1 = "HTTP/1.1";
        static const std::string close = "close";
    } // namespace misc_strings

    namespace header{
        // TODO replace any header constant in code by those strings
        const std::string content_type = "Content-Type";
        const std::string cache_control = "Cache-Control";
        const std::string connection = "Connection";
        const std::string upgrade = "Upgrade";
        const std::string accept = "Accept";
        const std::string content_length = "Content-Length";
        const std::string transfer_encoding = "Transfer-Encoding";
        const std::string user_agent = "User-Agent";
        const std::string authorization = "Authorization";
        const std::string set_cookie = "Set-Cookie";
        const std::string cookie = "Cookie";
        const std::string location = "Location";
        const std::string host = "Host";
        const std::string referer = "Referer";
        const std::string x_frame_options = "X-Frame-Options";
    }

    namespace connection{
        const std::string keep_alive = "keep-alive";
        const std::string close = "close";
        const std::string upgrade = "upgrade";
    }

    namespace accept{
        const std::string event_stream = "text/event-stream";
    }

    namespace content_type{
        const std::string application_form_urlencoded = "application/x-www-form-urlencoded";
        const std::string text_html = "text/html";
    }

class http_headers : public http_frame{

public:
    using http_header = std::pair<std::string, std::string>;

    // constructors
    http_headers() = default;
    ~http_headers() override = default;

    // setters
    void add_header(std::string key, std::string value);
    void set_header(std::string key, std::string value);
    void set_proxy_header(std::string key, std::string value);
    void add_proxy_header(std::string key, std::string value);
    bool upgrade() const;
    bool stream() const;
    bool has_header(std::string_view key) const;
    bool remove_header(std::string_view key);
    void set_http_version_major(uint8_t http_version_major);
    void set_http_version_minor(uint8_t http_version_minor);
    void set_keep_alive(bool keep_alive);

    // getters
    std::vector<http_header>& get_headers();
    const std::vector<http_header>& get_headers() const;
    static std::string get_header_parameter(const std::string& key, std::string_view name) ;
    std::vector<std::string> get_headers_with_key(std::string_view key) const;
    const std::string& get_header_with_key(std::string_view key) const;
    const std::string& get_authorization() const;
    const std::string& get_cookie() const;
    const std::string& get_user_agent() const;
    const std::string& get_content_type() const;
    bool is_content_type(const std::string& value) const;
    bool empty_headers() const;
    size_t get_content_length() const;
    int get_http_version_major() const;
    int get_http_version_minor() const;
    bool keep_alive() const;
    bool inline is_header(std::string_view key, std::string_view header) const;

    // debug
    void debug_headers(std::ostream& os) const;
    void log(const char* scope, int level) const override;

    // process header intended to be override to process headers
    virtual void process_header(std::string key, std::string value);

protected:
    std::vector<http_header> headers_;
    std::vector<http_header> proxy_headers_;
    boost::tribool keep_alive_    = boost::indeterminate;
    bool upgrade_                 = false;
    bool stream_                  = false;
    size_t content_length_        = 0;
    uint8_t http_version_major_   = 1;
    uint8_t http_version_minor_   = 1;
};

}

#endif