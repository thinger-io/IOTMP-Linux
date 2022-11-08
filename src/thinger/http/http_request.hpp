#ifndef HTTP_REQUEST_HPP
#define HTTP_REQUEST_HPP

#include <boost/asio.hpp>
#include <boost/regex.hpp>
#include <utility>
#include "http_headers.hpp"
#include "http_cookie_store.hpp"
#include "util/url.hpp"

namespace thinger::http {

    enum class method{
        GET,
        HEAD,
        POST,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATCH,
        UNKNOWN
    };

    method get_method(const std::string& method);
    const std::string& get_method(method);
    static const std::string https = "https";
    static const std::string wss = "wss";
    static const std::string ws = "ws";
    static const std::string https_port = "443";
    static const std::string http = "http";
    static const std::string http_port = "80";

class http_request : public http_headers{

public:

    // constructor
    http_request() = default;
    ~http_request() override = default;

    // setters
    bool set_url(const std::string& url);
    void set_host(std::string host);
    void set_port(const std::string &port);
    void set_protocol(std::string protocol);
    void set_ssl(bool ssl);
    void set_unix_socket(const std::string& unix_socket);
    void set_uri(const std::string &uri);
    void set_method(const std::string &method);
    void set_method(http::method method);
    void set_content(std::string content);
    void set_content(std::string content, std::string content_type);
    void add_uri_parameter(const std::string &key, const std::string &value);
    void set_resource(const std::string& resource);

    // getters
    std::string get_url() const;
    std::string get_base_path() const;
    const std::string& get_body() const;
    bool has_resource() const;
    bool has_query_parameters() const;
    bool has_uri_parameter(const std::string& key) const;
    bool has_uri_value(const std::string& key, const std::string& value) const;
    bool has_uri_parameters() const;
    const std::string& get_uri_parameter(const std::string& key) const;
    const std::multimap<std::string, std::string>& get_uri_parameters() const;
    bool has_content() const;
    method get_method() const;
    const std::string& get_method_string() const;
    std::string get_query_string() const;
    std::string& get_uri();
    const std::string& get_uri() const;
    const std::string& get_unix_socket();
    const std::string& get_protocol() const;
    bool is_default_port() const;
    const std::string &get_port() const;
    http_cookie_store& get_cookie_store();
    const std::string &get_host() const;
    bool is_ssl() const;
    const std::string& get_resource() const;
    std::string& get_resource();
    std::string& get_body();

    template<class T>
    T get_uri_parameter(const std::string& key, T default_value) const{
        if(has_uri_parameter(key)){
            const std::string& ts_str = get_uri_parameter(key);
            if(!ts_str.empty()){
                try
                {
                    T value = boost::lexical_cast<T>(ts_str);
                    return value;
                }catch(const boost::bad_lexical_cast &){

                }
            }
        }
        return default_value;
    }

    // chunked callbacks
    std::function<void(int, const std::string&)> get_chunked_callback();
    void set_chunked_callback(std::function<void(int, const std::string&)> callback);

    // stream related
    bool end_stream() override;
    size_t get_size() override;
    void to_buffer(std::vector<boost::asio::const_buffer> &buffer) const override;
    void process_header(std::string key, std::string value) override;

    // logs
    void log(const char* scope, int level) const override;

    // factory methods
    static std::shared_ptr<http_request> create_http_request(const std::string& method, const std::string& url);
    static std::shared_ptr<http_request> create_http_request(http::method method, const std::string& url, const std::string& unix_socket="");

    // other
    void refresh_uri();

private:
    bool ssl_ = false;
    method method_ = method::UNKNOWN;
    std::string uri_;
    std::string resource_;
    std::multimap<std::string, std::string> uri_params_;
    std::string content_;
    std::string host_;
    std::string port_;
    std::string protocol_;
    std::string unix_socket_;
    http_cookie_store cookie_store_;
    std::function<void(int, const std::string&)> on_chunked_;

};

}

#endif
