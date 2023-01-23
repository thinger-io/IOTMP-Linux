#include "http_request.hpp"
#include <unordered_map>
#include "../util/logger.hpp"

namespace thinger::http {

    using namespace util::url;


    static const std::string GET_STR = "GET";
    static const std::string HEAD_STR = "HEAD";
    static const std::string POST_STR = "POST";
    static const std::string PUT_STR = "PUT";
    static const std::string DELETE_STR = "DELETE";
    static const std::string TRACE_STR = "TRACE";
    static const std::string OPTIONS_STR = "OPTIONS";
    static const std::string CONNECT_STR = "CONNECT";
    static const std::string PATCH_STR = "PATCH";

    method get_method(const std::string &method) {
        static std::unordered_map<std::string, http::method> methods = {
            {GET_STR, method::GET},
            {HEAD_STR, method::HEAD},
            {POST_STR, method::POST},
            {PUT_STR, method::PUT},
            {DELETE_STR, method::DELETE},
            {TRACE_STR, method::TRACE},
            {OPTIONS_STR, method::OPTIONS},
            {CONNECT_STR, method::CONNECT},
            {PATCH_STR, method::PATCH}
        };
        auto it = methods.find(method);
        if (it != methods.end()) {
            return it->second;
        }
        return method::UNKNOWN;
    }

    const std::string& get_method(http::method method)
    {
        switch(method){
            case method::GET:
                return GET_STR;
            case method::HEAD:
                return HEAD_STR;
            case method::POST:
                return POST_STR;
            case method::PUT:
                return PUT_STR;
            case method::DELETE:
                return DELETE_STR;
            case method::TRACE:
                return TRACE_STR;
            case method::OPTIONS:
                return OPTIONS_STR;
            case method::CONNECT:
                return CONNECT_STR;
            case method::PATCH:
                return PATCH_STR;
            default:
                static std::string empty;
                return empty;
        }
    }

    /*
    void http_request::debug(std::ostream& os) const{
        os << "[HTTP REQUEST] " + get_url() << std::endl;
        os << " > HOST " << get_host() << ":" << get_port() <<  (is_ssl() ? " (SSL)" : " (NO SSL)") << std::endl;
        os << " > " << http::get_method(method_) << " " << get_uri() << std::endl;
        debug_headers(os);
        if(!content_.empty()){
            os << std::endl << content_;
        }
    }
     */

    void http_request::log(const char* scope, int level) const{
        LOG_LEVEL(level, "[%s] %s %s", scope, http::get_method(method_).c_str(), get_uri().c_str());

        http_headers::log(scope, level+1);

        if(!content_.empty()){
            LOG_LEVEL(level+2, "");
            std::istringstream f(content_);
            std::string line;
            while (std::getline(f, line)) {
                LOG_LEVEL(level+2, "%s", line.c_str());
            }
        }
    }

    void http_request::set_chunked_callback(std::function<void(int, const std::string&)> callback){
        on_chunked_ = std::move(callback);
    }

    std::function<void(int, const std::string&)> http_request::get_chunked_callback(){
        return on_chunked_;
    }

    std::shared_ptr<http_request>
    http_request::create_http_request(http::method method, const std::string& url, const std::string& unix_socket){
        auto request = std::make_shared<http_request>();
        if(request->set_url(url)){
            request->set_method(method);
            request->add_header(http::header::accept, "*/*");
            if(!unix_socket.empty()){
                request->set_unix_socket(unix_socket);
            }
            return request;
        }
        // return an invalid request (something failed while parsing the URL)
        return std::make_shared<http_request>();
    }

    std::string http_request::get_base_path() const{
        std::string base_path = is_ssl() ? "https://" : "http://";
        base_path += is_default_port() ? get_host() : get_host() + ":" + get_port();
        return base_path;
    }

    std::string http_request::get_url() const{
        return get_base_path() + get_uri();
    }

    bool http_request::set_url(const std::string& url){
        static const boost::regex uri_regex(R"((wss?|https?):\/\/(?:(.*):(.*)@)?([a-zA-Z\.\-0-9_]+)(?::(\d+))*(.*))");
        boost::match_results<std::string::const_iterator> results;
        if (boost::regex_match(url, results, uri_regex)) {
            std::string protocol = results[1];
            std::string username = results[2];
            std::string password = results[3];
            std::string host = results[4];
            std::string port = results[5];
            std::string uri = results[6];
            set_host(host);
            if(!port.empty()){
                set_port(port);
            }
            set_protocol(protocol);
            set_ssl(protocol==https || protocol==wss);
            set_uri(uri.empty() ? "/" : uri);


            // set upgrade headers if it is a websocket
            if(protocol==wss || protocol==ws){
                set_header(http::header::connection, "upgrade");
                set_header(http::header::upgrade, "websocket");
            }

            return true;
        }
        return false;
    }

    std::shared_ptr<http_request> http_request::create_http_request(const std::string& method, const std::string& url){
        return create_http_request(http::get_method(method), url);
    }

    bool http_request::has_query_parameters() const{
        return !uri_params_.empty();
    }

    bool http_request::has_resource() const{
        return !resource_.empty();
    }

    const std::string& http_request::get_body() const{
        return content_;
    }

    std::string& http_request::get_body(){
        return content_;
    }

    std::string& http_request::get_resource(){
        return resource_;
    }

    const std::string& http_request::get_resource() const{
        return resource_;
    }

    void http_request::set_resource(const std::string& resource){
        // update resource
        resource_ = resource;
        // refresh uri
        refresh_uri();
    }

    bool http_request::has_uri_parameters() const{
        return !uri_params_.empty();
    }

    bool http_request::has_uri_value(const std::string& key, const std::string& value) const{
        auto position = uri_params_.find(key);
        return position != uri_params_.end() && position->second == value;
    }

    bool http_request::has_uri_parameter(const std::string& key) const{
        return uri_params_.find(key) != uri_params_.end();
    }

    const std::multimap<std::string, std::string>& http_request::get_uri_parameters() const{
        return uri_params_;
    }

    const std::string& http_request::get_uri_parameter(const std::string& key) const{
        auto position = uri_params_.find(key);
        if(position != uri_params_.end()){
            return position->second;
        }
        static const std::string empty;
        return empty;
    }

    void http_request::add_uri_parameter(const std::string& key, const std::string& value){
        // store uri value
        uri_params_.emplace(key, value);
        // update uri
        refresh_uri();
    }

    bool http_request::has_content() const{
        return !content_.empty() || has_header(http::header::content_length);
    }

    void http_request::set_content(std::string content, std::string content_type){
        set_content(std::move(content));
        set_header(http::header::content_type, std::move(content_type));
    }

    void http_request::set_content(std::string content){
        content_ = std::move(content);
        set_header(http::header::content_length, boost::lexical_cast<std::string>(content.size()));
    }

    const std::string& http_request::get_method_string() const{
        return http::get_method(method_);
    }

    method http_request::get_method() const{
        return method_;
    }

    void http_request::set_method(http::method method){
        // set method
        method_ = method;
        // force methods with a supposed body to force a default content length
        if((method_==http::method::POST || method_==http::method::PUT || method==http::method::PATCH) && !has_header(http::header::content_length)){
            set_header(http::header::content_length, "0");
        }
    }

    std::string http_request::get_query_string() const{
        return get_url_encoded_data(uri_params_);
    }

    void http_request::set_method(const std::string& method){
        method_ = http::get_method(method);
    }

    std::string& http_request::get_uri(){
        return uri_;
    }

    const std::string& http_request::get_uri() const{
        return uri_;
    }

    void http_request::refresh_uri(){
        if(uri_params_.empty()){
            uri_ = util::url::uri_path_encode(resource_);
        }else{
            uri_ = util::url::uri_path_encode(resource_) + "?" + get_url_encoded_data(uri_params_);
        }
    }

    void http_request::set_uri(const std::string& uri){
        boost::smatch what;
        std::string::const_iterator start = uri.begin();
        std::string::const_iterator end   = uri.end();

        static const boost::regex resource_regex("(\\/[^\\?#]*)");
        if(boost::regex_search(start, end, what, resource_regex)){
            resource_ = util::url::url_decode(std::string(what[0].first, what[1].second));
            start = what[0].second;
        }

        // Regex will stop at ? or # or end of the uri. Check if there is parameters available for its parsing
        if(start!=uri.end() && *start=='?'){
            ++start;
            parse_url_encoded_data(start, end, uri_params_);
        }

        // just save the original uri to avoid generating it again
        uri_ = uri;
    }

    const std::string& http_request::get_unix_socket(){
        return unix_socket_;
    }

    void http_request::set_unix_socket(const std::string& unix_socket){
        unix_socket_ = unix_socket;
    }

    void http_request::set_ssl(bool ssl){
        ssl_ = ssl;
    }

    bool http_request::is_ssl() const{
        return ssl_;
    }

    const std::string& http_request::get_protocol() const{
        return protocol_;
    }

    void http_request::set_protocol(std::string protocol){
        protocol_ = std::move(protocol);
    }

    void http_request::set_port(const std::string& port){
        port_ = port;
        // update host header if the port is not the default one (80 or 443)
        if(port!=https_port && port!=http_port){
            set_header(http::header::host, host_ + ":" + port_);
        }
    }

    bool http_request::is_default_port() const{
        return port_.empty() ? true : ((ssl_ && https_port == port_) || http_port==port_);
    }

    const std::string& http_request::get_port() const{
        return port_.empty() ? (ssl_ ? https_port : http_port) : port_;
    }

    void http_request::set_host(std::string host){
        auto position = host.find(':');
        if(position==std::string::npos){
            host_ = std::move(host);
        }else{
            host_ = host.substr(0, position);
            port_ = host.substr(position+1, std::string::npos);
        }

        // use host always in lowercase
        boost::algorithm::to_lower(host_);

        // update host header
        if(get_port()!=https_port && get_port()!=http_port){
            set_header(http::header::host, host_ + ":" + port_);
        }else{
            set_header(http::header::host, host_);
        }
    }

    const std::string& http_request::get_host() const{
        return host_;
    }

    http_cookie_store& http_request::get_cookie_store(){
        return cookie_store_;
    }

    void http_request::process_header(std::string key, std::string value){
        // adjust host
        if(boost::iequals(key, http::header::host)){
            set_host(std::move(value));
        }else{
            // handle header by parent
            http_headers::process_header(std::move(key), std::move(value));
        }
    }

    void http_request::to_buffer(std::vector<boost::asio::const_buffer>& buffer) const{
        buffer.emplace_back(boost::asio::buffer(http::get_method(method_)));
        buffer.emplace_back(boost::asio::buffer(misc_strings::space));
        buffer.emplace_back(boost::asio::buffer(get_uri()));
        buffer.emplace_back(boost::asio::buffer(misc_strings::space));
        buffer.emplace_back(boost::asio::buffer(misc_strings::http_1_1));
        buffer.emplace_back(boost::asio::buffer(misc_strings::crlf));

        std::set<std::string> proxy_headers;

        // replace headers with proxy headers (if any)
        for(const auto& [key, value] : proxy_headers_){
            buffer.emplace_back(boost::asio::buffer(key));
            buffer.emplace_back(boost::asio::buffer(misc_strings::name_value_separator));
            buffer.emplace_back(boost::asio::buffer(value));
            buffer.emplace_back(boost::asio::buffer(misc_strings::crlf));
            proxy_headers.emplace(key);
        }

        // push headers
        for(const auto& [key, value] : headers_){
            if(proxy_headers.find(key)==proxy_headers.end()){
                buffer.emplace_back(boost::asio::buffer(key));
                buffer.emplace_back(boost::asio::buffer(misc_strings::name_value_separator));
                buffer.emplace_back(boost::asio::buffer(value));
                buffer.emplace_back(boost::asio::buffer(misc_strings::crlf));
            }
        }

        buffer.emplace_back(boost::asio::buffer(misc_strings::crlf));

        if(!content_.empty()){
            buffer.emplace_back(boost::asio::buffer(content_));
        }
    }

    size_t http_request::get_size(){
        return 0;
    }

    bool http_request::end_stream(){
        return true;
    }

}
