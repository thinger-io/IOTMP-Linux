#include "http_headers.hpp"
#include <boost/regex.hpp>
#include "../util/logger.hpp"

namespace thinger::http{

    void http_headers::process_header(std::string key, std::string value){
        if(boost::indeterminate(keep_alive_) && is_header(key, header::connection)){
            /*
             * Firefox send both keep-alive and upgrade values in Connection header, i.e., when opening a WebSocket,
             * so it is necessary to test values separately.
             */
            std::vector<std::string> strs;
            boost::split(strs, value, boost::is_any_of(","));
            for(auto& str:strs){
                boost::algorithm::trim(str);
                if(is_header(str, connection::keep_alive)){
                    keep_alive_ = true;
                }
                else if (is_header(str, connection::close)){
                    keep_alive_ = false;
                }
                else if(is_header(str, connection::upgrade)){
                    upgrade_ = true;
                }
            }
        }

        if(is_header(key, header::accept)){
            stream_ = boost::iequals(value, accept::event_stream);
        }else if(is_header(key, header::content_length)){
            try{
                content_length_ = boost::lexical_cast<unsigned long>(value);
            }catch(const boost::bad_lexical_cast &)
            {
                content_length_ = 0;
            }
        }

        headers_.emplace_back(std::move(key), std::move(value));
    }

    void http_headers::add_header(std::string key, std::string value){
        if(key.empty()) return;
        headers_.emplace_back(std::move(key), std::move(value));
    }

    void http_headers::add_proxy_header(std::string key, std::string value){
        if(key.empty()) return;
        proxy_headers_.emplace_back(std::move(key), std::move(value));
    }

    void http_headers::set_header(std::string key, std::string value){
        for(auto & header : headers_)
        {
            if(is_header(header.first, key)){
                header.second = std::move(value);
                return;
            }
        }
        add_header(std::move(key), std::move(value));
    }

    void http_headers::set_proxy_header(std::string key, std::string value){
        for(auto & header : proxy_headers_)
        {
            if(is_header(header.first, key)){
                header.second = std::move(value);
                return;
            }
        }
        add_proxy_header(std::move(key), std::move(value));
    }

    bool http_headers::upgrade() const{
        return upgrade_;
    }

    bool http_headers::stream() const{
        return stream_;
    }

    bool http_headers::has_header(std::string_view key) const{
        for(const auto & header : headers_)
        {
            if(is_header(header.first, key)){
                return true;
            }
        }
        return false;
    }

    const std::string& http_headers::get_header_with_key(std::string_view key) const
    {
        for(const auto & header : headers_)
        {
            if(is_header(header.first, key)){
                return header.second;
            }
        }
        static std::string emtpy;
        return emtpy;
    }

    std::vector<std::string> http_headers::get_headers_with_key(std::string_view key) const{
        std::vector<std::string> headers;
        for(const auto & header : headers_)
        {
            if(is_header(header.first, key)){
                headers.push_back(header.second);
            }
        }
        return headers;
    }

    const std::vector<http_headers::http_header>& http_headers::get_headers() const{
        return headers_;
    }

    std::vector<http_headers::http_header>& http_headers::get_headers(){
        return headers_;
    }

    bool http_headers::remove_header(std::string_view key)
    {
        for(auto it=headers_.begin(); it!=headers_.end(); ++it){
            if(is_header(it->first, key)){
                headers_.erase(it);
                return true;
            }
        }
        return false;
    }

    const std::string& http_headers::get_authorization() const
    {
        return get_header_with_key(header::authorization);
    }

    const std::string& http_headers::get_cookie() const
    {
        return get_header_with_key(header::cookie);
    }

    const std::string& http_headers::get_user_agent() const{
        return get_header_with_key(header::user_agent);
    }

    const std::string& http_headers::get_content_type() const
    {
        return get_header_with_key(header::content_type);
    }

    bool http_headers::is_content_type(const std::string& value) const
    {
        return boost::istarts_with(get_header_with_key(header::content_type), value);
    }

    bool http_headers::empty_headers() const{
        return headers_.empty();
    }

    void http_headers::debug_headers(std::ostream& os) const{
        for(const std::pair<std::string, std::string>& t: headers_){
            os << "\t> " << t.first << ": " << t.second << std::endl;
        }
    }

    void http_headers::log(const char* scope, int level) const{
        for(const auto& t: headers_){
            LOG_LEVEL(level, "%s: %s", t.first.c_str(), t.second.c_str());
        }
        for(const auto& t: proxy_headers_){
            LOG_LEVEL(level, "(PROXY REPLACE) %s: %s", t.first.c_str(), t.second.c_str());
        }
    }

    size_t http_headers::get_content_length() const{
        return content_length_;
    }

    bool http_headers::keep_alive() const
    {
        if(boost::indeterminate(keep_alive_)){
            return http_version_major_>=1 && http_version_minor_>=1;
        }else{
            return (bool) keep_alive_;
        }
    }

    void http_headers::set_keep_alive(bool keep_alive){
	    keep_alive_ = keep_alive;
	    set_header(http::header::connection, (keep_alive ? "Keep-Alive" : "Close"));
    }

    void http_headers::set_http_version_major(uint8_t http_version_major) {
        http_version_major_ = http_version_major;
    }

    void http_headers::set_http_version_minor(uint8_t http_version_minor) {
        http_version_minor_ = http_version_minor;
    }

    int http_headers::get_http_version_major() const {
        return http_version_major_;
    }

    int http_headers::get_http_version_minor() const {
        return http_version_minor_;
    }

    std::string http_headers::get_header_parameter(const std::string& header_value, std::string_view name) {

        auto start = header_value.begin();
		auto end   = header_value.end();

		if(start==end) return "";

		static boost::regex cookie_regex("([^;^\\s]+)=\"?([^;^\"]*)\"?");
		boost::smatch what;

		while (boost::regex_search(start, end, what, cookie_regex))
		{
			std::string key(what[1].first, what[1].second);
			std::string value(what[2].first, what[2].second);

			if(key==name) return value;

			start = what[0].second;
		}

		return "";
	}

    bool inline http_headers::is_header(std::string_view key, std::string_view header) const{
        return boost::iequals(key, header);
    }
}