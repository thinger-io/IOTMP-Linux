#include "http_response.hpp"
#include "http_mime_types.hpp"
#include "data/out_string.hpp"

#include <memory>
#include "../util/logger.hpp"

namespace thinger::http{

    namespace status_strings{

        const std::string ok =
                "HTTP/1.1 200 OK";
        const std::string created =
                "HTTP/1.1 201 Created";
        const std::string accepted =
                "HTTP/1.1 202 Accepted";
        const std::string no_content =
                "HTTP/1.1 204 No Content";
        const std::string multiple_choices =
                "HTTP/1.1 300 Multiple Choices";
        const std::string moved_permanently =
                "HTTP/1.1 301 Moved Permanently";
        const std::string moved_temporarily =
                "HTTP/1.1 302 Moved Temporarily";
        const std::string not_modified =
                "HTTP/1.1 304 Not Modified";
        const std::string bad_request =
                "HTTP/1.1 400 Bad Request";
        const std::string unauthorized =
                "HTTP/1.1 401 Unauthorized";
        const std::string forbidden =
                "HTTP/1.1 403 Forbidden";
        const std::string not_found =
                "HTTP/1.1 404 Not Found";
        const std::string not_allowed =
                "HTTP/1.1 405 Method Not Allowed";
        const std::string timed_out =
                "HTTP/1.1 408 Operation Timed Out";
        const std::string internal_server_error =
                "HTTP/1.1 500 Internal Server Error";
        const std::string not_implemented =
                "HTTP/1.1 501 Not Implemented";
        const std::string bad_gateway =
                "HTTP/1.1 502 Bad Gateway";
        const std::string service_unavailable =
                "HTTP/1.1 503 Service Unavailable";
        const std::string switching_protocols =
                "HTTP/1.1 101 Switching Protocols";
        const std::string too_many_requests =
                "HTTP/1.1 429 Too Many Requests";
        const std::string temporary_redirect =
                "HTTP/1.1 307 Temporary Redirect";
        const std::string permanent_redirect =
                "HTTP/1.1 308 Permanent Redirect";
        const std::string upgrade_required =
                "HTTP/1.1 426 Upgrade Required";
        const std::string conflict =
                "HTTP/1.1 409 Conflict";

        const std::string& get_status_string(http_response::status_type status){
            switch(status){
                case http_response::status_type::ok:
                    return ok;
                case http_response::status_type::created:
                    return created;
                case http_response::status_type::accepted:
                    return accepted;
                case http_response::status_type::no_content:
                    return no_content;
                case http_response::status_type::multiple_choices:
                    return multiple_choices;
                case http_response::status_type::moved_permanently:
                    return moved_permanently;
                case http_response::status_type::moved_temporarily:
                    return moved_temporarily;
                case http_response::status_type::temporary_redirect:
                    return temporary_redirect;
                case http_response::status_type::permanent_redirect:
                    return permanent_redirect;
                case http_response::status_type::not_modified:
                    return not_modified;
                case http_response::status_type::bad_request:
                    return bad_request;
                case http_response::status_type::unauthorized:
                    return unauthorized;
                case http_response::status_type::forbidden:
                    return forbidden;
                case http_response::status_type::not_found:
                    return not_found;
                case http_response::status_type::not_allowed:
                    return not_allowed;
                case http_response::status_type::timed_out:
                    return timed_out;
                case http_response::status_type::internal_server_error:
                    return internal_server_error;
                case http_response::status_type::not_implemented:
                    return not_implemented;
                case http_response::status_type::bad_gateway:
                    return bad_gateway;
                case http_response::status_type::service_unavailable:
                    return service_unavailable;
                case http_response::status_type::switching_protocols:
                    return switching_protocols;
                case http_response::status_type::too_many_requests:
                    return too_many_requests;
                case http_response::status_type::upgrade_required:
                    return upgrade_required;
                case http_response::status_type::conflict:
                    return conflict;
                default:
                    return internal_server_error;
            }
        }
    }

    void http_response::to_buffer(std::vector<boost::asio::const_buffer>& buffer) const{
        buffer.emplace_back(boost::asio::buffer(status_strings::get_status_string(status_)));
        buffer.emplace_back(boost::asio::buffer(misc_strings::crlf));
        for(const auto& t: headers_){
            buffer.emplace_back(boost::asio::buffer(t.first));
            buffer.emplace_back(boost::asio::buffer(misc_strings::name_value_separator));
            buffer.emplace_back(boost::asio::buffer(t.second));
            buffer.emplace_back(boost::asio::buffer(misc_strings::crlf));
        }
        buffer.emplace_back(boost::asio::buffer(misc_strings::crlf));
        if(!content_.empty()){
            buffer.emplace_back(boost::asio::buffer(content_));
        }
    }

/*
void http_response::debug(std::ostream& os) {
    os << "[HTTP RESPONSE]" << std::endl;
    os << "> HTTP/" << std::to_string(http_version_major_) << "." << std::to_string(http_version_minor_) << " " << status
       << " " << reason_phrase_ << std::endl;
    debug_headers(os);
    if(!content_.empty()){
        os << std::endl << content_;
    }
}*/

    void http_response::log(const char* scope, int level) const{
        LOG_LEVEL(level, "[%s] %s", scope, status_strings::get_status_string(status_).c_str());
        {
            LOG_LEVEL(level + 1, "[headers]");
            http_headers::log(scope, level + 1);
        }
        if(!content_.empty()){
            LOG_LEVEL(level + 2, "[body]");
            LOG_LEVEL(level + 2, "%s", content_.c_str());
        }
    }

    namespace stock_replies{

        static const std::string ok;

        static const std::string created(
                "<html>"
                "<head><title>Created</title></head>"
                "<body><h1>201 Created</h1></body>"
                "</html>");

        static const std::string accepted(
                "<html>"
                "<head><title>Accepted</title></head>"
                "<body><h1>202 Accepted</h1></body>"
                "</html>");

        static const std::string no_content(
                "<html>"
                "<head><title>No Content</title></head>"
                "<body><h1>204 Content</h1></body>"
                "</html>");

        static const std::string multiple_choices(
                "<html>"
                "<head><title>Multiple Choices</title></head>"
                "<body><h1>300 Multiple Choices</h1></body>"
                "</html>");

        static const std::string moved_permanently(
                "<html>"
                "<head><title>Moved Permanently</title></head>"
                "<body><h1>301 Moved Permanently</h1></body>"
                "</html>");

        static const std::string moved_temporarily(
                "<html>"
                "<head><title>Moved Temporarily</title></head>"
                "<body><h1>302 Moved Temporarily</h1></body>"
                "</html>");

        static const std::string temporary_redirect(
                "<html>"
                "<head><title>Temporary Redirect</title></head>"
                "<body><h1>307 Temporary Redirect</h1></body>"
                "</html>");

        static const std::string permanent_redirect(
                "<html>"
                "<head><title>Permanet Redirect</title></head>"
                "<body><h1>308 Permanent Redirect</h1></body>"
                "</html>");

        static const std::string not_modified(
                "<html>"
                "<head><title>Not Modified</title></head>"
                "<body><h1>304 Not Modified</h1></body>"
                "</html>");

        static const std::string bad_request(
                "<html>"
                "<head><title>Bad Request</title></head>"
                "<body><h1>400 Bad Request</h1></body>"
                "</html>");

        static const std::string unauthorized(
                "<html>"
                "<head><title>Unauthorized</title></head>"
                "<body><h1>401 Unauthorized</h1></body>"
                "</html>");

        static const std::string forbidden(
                "<html>"
                "<head><title>Forbidden</title></head>"
                "<body><h1>403 Forbidden</h1></body>"
                "</html>");

        static const std::string not_found(
                "<html>"
                "<head><title>Not Found</title></head>"
                "<body><h1>404 Not Found</h1></body>"
                "</html>");

        static const std::string internal_server_error(
                "<html>"
                "<head><title>Internal Server Error</title></head>"
                "<body><h1>500 Internal Server Error</h1></body>"
                "</html>");

        static const std::string not_implemented(
                "<html>"
                "<head><title>Not Implemented</title></head>"
                "<body><h1>501 Not Implemented</h1></body>"
                "</html>");

        static const std::string bad_gateway(
                "<html>"
                "<head><title>Bad Gateway</title></head>"
                "<body><h1>502 Bad Gateway</h1></body>"
                "</html>");

        static const std::string service_unavailable(
                "<html>"
                "<head><title>Service Unavailable</title></head>"
                "<body><h1>503 Service Unavailable</h1></body>"
                "</html>");

        static const std::string too_many_requests(
                "<html>"
                "<head><title>Too Many Requests</title></head>"
                "<body><h1>429 Too Many Requests</h1></body>"
                "</html>");

        static const std::string timed_out(
                "<html>"
                "<head><title>Operation Timed Out</title></head>"
                "<body><h1>408 Operation Timed Out</h1></body>"
                "</html>");

        const std::string& to_string(http_response::status_type status){
            switch(status){
                case http_response::status_type::ok:
                    return ok;
                case http_response::status_type::created:
                    return created;
                case http_response::status_type::accepted:
                    return accepted;
                case http_response::status_type::no_content:
                    return no_content;
                case http_response::status_type::multiple_choices:
                    return multiple_choices;
                case http_response::status_type::moved_permanently:
                    return moved_permanently;
                case http_response::status_type::moved_temporarily:
                    return moved_temporarily;
                case http_response::status_type::temporary_redirect:
                    return temporary_redirect;
                case http_response::status_type::permanent_redirect:
                    return permanent_redirect;
                case http_response::status_type::not_modified:
                    return not_modified;
                case http_response::status_type::bad_request:
                    return bad_request;
                case http_response::status_type::unauthorized:
                    return unauthorized;
                case http_response::status_type::forbidden:
                    return forbidden;
                case http_response::status_type::not_found:
                    return not_found;
                case http_response::status_type::timed_out:
                    return timed_out;
                case http_response::status_type::internal_server_error:
                    return internal_server_error;
                case http_response::status_type::not_implemented:
                    return not_implemented;
                case http_response::status_type::bad_gateway:
                    return bad_gateway;
                case http_response::status_type::service_unavailable:
                    return service_unavailable;
                case http_response::status_type::too_many_requests:
                    return too_many_requests;
                default:
                    return internal_server_error;
            }
        }

    } // namespace stock_replies

    std::shared_ptr<http_response> http_response::stock_http_reply(http_response::status_type status){
        auto response = std::make_shared<http_response>();
        response->set_status(status);
        response->set_content(stock_replies::to_string(status), http::mime_types::text_html);
        return response;
    }

    http_response::http_response()= default;

    bool http_response::is_redirect_response() const{
        return status_ == status_type::temporary_redirect ||
               status_ == status_type::moved_temporarily ||
               status_ == status_type::permanent_redirect ||
               status_ == status_type::moved_permanently;
    }

    void http_response::set_status(uint16_t status_code){
        status_ = (status_type) status_code;
    }

    void http_response::set_status(http_response::status_type status_code){
        status_ = status_code;
    }

    void http_response::set_reason_phrase(const std::string& phrase){
        reason_phrase_ = phrase;
    }

    http_response::status_type http_response::get_status() const{
        return status_;
    }

    bool http_response::is_ok() const{
        return status_>=status_type::ok && status_<status_type::multiple_choices;
    }

    size_t http_response::get_size(){
        return 0;
    }

    const std::string& http_response::get_content() const{
        return content_;
    }

    std::string& http_response::get_content(){
        return content_;
    }

    size_t http_response::get_content_size() const{
        return content_.size();
    }

    void http_response::set_content(std::string content){
        content_ = std::move(content);
        set_content_length(content_.size());
    }

    void http_response::set_content(std::string content, std::string content_type){
        set_content(std::move(content));
        set_content_type(std::move(content_type));
    }

    void http_response::set_content_length(size_t content_length){
        content_length_ = content_length;
        set_header(http::header::content_length, boost::lexical_cast<std::string>(content_length));
    }

    void http_response::set_content_type(std::string&& content_type){
        set_header(http::header::content_type, std::move(content_type));
    }

}