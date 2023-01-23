#include <sstream>
#include "../util/logger.hpp"
#include "http_response_factory.hpp"
#include "http_response.hpp"

namespace thinger::http{

    boost::tribool http_response_factory::consume(char input, bool head_request) {
        // keep control of headers size
        if(state_<=expecting_newline_3){
            headers_size_++;
            if(headers_size_>MAX_HEADERS_SIZE) return false;
        }
        // parse response
        switch (state_) {
            case http_version_h:
                if (input == 'H') {
                    state_ = http_version_t_1;
                    return boost::indeterminate;
                }
                return false;
            case http_version_t_1:
                if (input == 'T') {
                    state_ = http_version_t_2;
                    return boost::indeterminate;
                }
                return false;
            case http_version_t_2:
                if (input == 'T') {
                    state_ = http_version_p;
                    return boost::indeterminate;
                }
                return false;
            case http_version_p:
                if (input == 'P') {
                    state_ = http_version_slash;
                    return boost::indeterminate;
                }
                return false;
            case http_version_slash:
                if (input == '/') {
                    state_ = http_version_major_start;
                    return boost::indeterminate;
                }
                return false;
            case http_version_major_start:
                if (is_digit(input)) {
                    tempInt_ = input - '0';
                    state_ = http_version_major;
                    return boost::indeterminate;
                }
                return false;
            case http_version_major:
                if (input == '.') {
                    if(!resp) resp = std::make_shared<http_response>();
                    on_http_major_version(tempInt_);
                    state_ = http_version_minor_start;
                    return boost::indeterminate;
                }
                else if (is_digit(input)) {
                    tempInt_ = tempInt_ * 10 + input - '0';
                    return boost::indeterminate;
                }
                return false;
            case http_version_minor_start:
                if (is_digit(input)) {
                    tempInt_ = input - '0';
                    state_ = http_version_minor;
                    return boost::indeterminate;
                }
                return false;
            case http_version_minor:
                if (is_digit(input)) {
                    tempInt_ = tempInt_ * 10 + input - '0';
                    return boost::indeterminate;
                }
                else if(input == ' '){
                    on_http_minor_version(tempInt_);
                    tempInt_ = 0;
                    state_ = status_code;
                    return boost::indeterminate;
                }
                return false;
            case status_code:
                if (is_digit(input)) {
                    tempInt_ = tempInt_ * 10 + input - '0';
                    return boost::indeterminate;
                }else if(input == ' '){
                    on_http_status_code(tempInt_);
                    state_ = reason_phrase;
                    return boost::indeterminate;
                }
                return false;
            case reason_phrase:
                if(input == '\r'){
                    on_http_reason_phrase(tempString1_);
                    state_ = expecting_newline_1;
                    return boost::indeterminate;
                }else if (is_char(input) || input == ' '){
                    tempString1_.push_back(input);
                    return boost::indeterminate;
                }
                return false;
            case expecting_newline_1:
                if (input == '\n') {
                    state_ = header_line_start;
                    return boost::indeterminate;
                }
                return false;
            case header_line_start:
                if (input == '\r') {
                    state_ = expecting_newline_3;
                    return boost::indeterminate;
                }
                else if (!empty_headers() && (input == ' ' || input == '\t')) {
                    state_ = header_lws;
                    return boost::indeterminate;
                }
                else if (!is_char(input) || is_ctl(input) || is_tspecial(input)) {
                    return false;
                }
                else {
                    tempString1_.clear();
                    tempString1_.push_back(input);
                    state_ = header_name;
                    return boost::indeterminate;
                }
            case header_lws:
                if (input == '\r') {
                    state_ = expecting_newline_2;
                    return boost::indeterminate;
                }
                else if (input == ' ' || input == '\t') {
                    return boost::indeterminate;
                }
                else if (is_ctl(input)) {
                    return false;
                }
                else {
                    state_ = header_value;
                    tempString1_.push_back(input);
                    return boost::indeterminate;
                }
            case header_name:
                if (input == ':') {
                    state_ = space_before_header_value;
                    return boost::indeterminate;
                }
                else if (!is_char(input) || is_ctl(input) || is_tspecial(input)) {
                    return false;
                }
                else {
                    tempString1_.push_back(input);
                    return boost::indeterminate;
                }
            case space_before_header_value:
                if (input == ' ') {
                    tempString2_.clear();
                    state_ = header_value;
                    return boost::indeterminate;
                }
                return false;
            case header_value:
                if (input == '\r') {
                    on_http_header(tempString1_, tempString2_);
                    state_ = expecting_newline_2;
                    return boost::indeterminate;
                }
                else if (is_ctl(input)) {
                    return false;
                }
                else {
                    tempString2_.push_back(input);
                    return boost::indeterminate;
                }
            case expecting_newline_2:
                if (input == '\n') {
                    state_ = header_line_start;
                    return boost::indeterminate;
                }
                return false;
            case expecting_newline_3:
                if (input == '\n'){
                    if(content_==lenght_delimited && get_content_length()>0){
                        // if it is a head request, we are not expecting a body
                        if(head_request) return true;

                        // set state to length delimited content
                        state_ = lenght_delimited_content;
                        // save content size to read in tempInt_
                        tempInt_ = get_content_length();
                        // verify we are abe to read the content size
                        if(on_length_delimited_content(tempInt_)){
                            return boost::indeterminate;
                        }else{
                            return false;
                        }
                    }else if(content_==chunked){
                        // if it is a head request, we are not expecting a body
                        if(head_request) return true;

                        tempInt_ = 0;
                        lastChunk_ = false;
                        state_ = chunked_content_size;
                        if(on_chunked_){
                            on_chunked_(0, std::string());
                        }
                        return boost::indeterminate;
                    }
                    return true;
                }
                return false;
            case lenght_delimited_content:
                // save content
                on_content_data(input);
                // check if we are done reading the data
                if(--tempInt_>0){
                    return boost::indeterminate;
                }else{
                    return true;
                }
            case chunked_content_size:
                // read chunked content size
                if(is_hexadecimal(input)){
                    tempInt_ = tempInt_ * 16 + get_hex_value(input);
                    return boost::indeterminate;
                }else if(input == '\r'){
                    if(tempInt_==0) lastChunk_ = true;
                    state_ = chunked_content_size_expecting_n;
                    return boost::indeterminate;
                }
                return false;
            case chunked_content_size_expecting_n:
                if(input=='\n'){
                    // check we are able to read the expected size
                    if(on_chunk_read(tempInt_)){
                        state_ = chunked_content;
                        return boost::indeterminate;
                    }else{
                        return false;
                    }
                }
                return false;
            case chunked_content:
                if(tempInt_>0){
                    on_content_data(input);
                    tempInt_--;
                    return boost::indeterminate;
                }else if(input=='\r'){
                    state_ = chunked_content_expecting_n;
                    return boost::indeterminate;
                }
                return false;
            case chunked_content_expecting_n:
                if(input=='\n'){
                    if(lastChunk_){
                        if(on_chunked_) on_chunked_(2, std::string());
                        return true;
                    }else{
                        tempInt_ = 0;
                        state_ = chunked_content_size;
                        if(on_chunked_){
                            on_chunked_(1, resp->get_content());
                            resp->get_content().clear();
                        }
                        return boost::indeterminate;
                    }
                }
                return false;
            default:
                return false;
        }
    }

    bool http_response_factory::is_char(char c) {
        return c >= 0 && c <= 127;
    }

    bool http_response_factory::is_ctl(char c) {
        return (c >= 0 && c <= 31) || (c == 127);
    }

    bool http_response_factory::is_tspecial(char c) {
        switch (c) {
            case '(':
            case ')':
            case '<':
            case '>':
            case '@':
            case ',':
            case ';':
            case ':':
            case '\\':
            case '"':
            case '/':
            case '[':
            case ']':
            case '?':
            case '=':
            case '{':
            case '}':
            case ' ':
            case '\t':
                return true;
            default:
                return false;
        }
    }

    bool http_response_factory::is_digit(char c) {
        return c >= '0' && c <= '9';
    }

    bool http_response_factory::is_hexadecimal(char c){
        return (c >= '0' && c <= '9') || (c >= 'a' && c<='f') || (c>='A' && c <= 'F');
    }

    uint8_t http_response_factory::get_hex_value(char c)
    {
        if(c <= '9'){
            return c - '0';
        }else if(c<='F'){
            return c - 55;
        }else if(c<='f'){
            return c - 87;
        }
        return 0;
    }

    std::shared_ptr<http_response> http_response_factory::consume_response() {
        std::shared_ptr<http_response> request(resp);
        reset();
        request->log("HTTP CLIENT RESPONSE", 0);
        return request;
    }

    void http_response_factory::reset() {
        resp.reset();
        content_ = none;
        state_ = http_version_h;
        tempString1_.clear();
        tempString2_.clear();
        headers_size_ = 0;
        on_chunked_ = nullptr;
    }

    void http_response_factory::on_http_status_code(unsigned short status_code){
        resp->set_status(status_code);
    }

    void http_response_factory::on_http_reason_phrase(const std::string& reason){
        resp->set_reason_phrase(reason);
    }

    void http_response_factory::on_http_major_version(uint8_t major)
    {
        resp->set_http_version_major(major);
    }

    void http_response_factory::on_http_minor_version(uint16_t minor){
        resp->set_http_version_minor(minor);
    }

    bool http_response_factory::on_chunk_read(size_t size){
        if(size==0) return true;
        // check that the size meets MAX_CONTENT_SIZE
        size_t expected_size = get_content_read() +  size;
        if(expected_size>MAX_CONTENT_SIZE){
            LOG_ERROR("the response size exceeds the maximum allowed file size: %zu (%zu max)", size, MAX_CONTENT_SIZE);
            return false;
        }
        // upgrade buffer size
        resp->get_content().reserve(expected_size);
        return true;
    }

    bool http_response_factory::on_length_delimited_content(size_t size){
        if(size>MAX_CONTENT_SIZE){
            LOG_ERROR("the response size exceeds the maximum allowed file size: %zu (%zu max)", size, MAX_CONTENT_SIZE);
            return false;
        }
        resp->get_content().reserve(size);
        return true;
    }

    void http_response_factory::on_http_header(const std::string& name, const std::string& value){
        if(boost::iequals(name, http::header::transfer_encoding) && boost::iequals(value, "chunked")){
            content_ = chunked;
        }else if(boost::iequals(name, http::header::content_length)){
            content_ = lenght_delimited;
        }
        resp->process_header(name, value);
    }

    void http_response_factory::on_content_data(char content){
        resp->get_content().push_back(content);
    }

    size_t http_response_factory::get_content_length(){
        return resp->get_content_length();
    }

    size_t http_response_factory::get_content_read(){
        return resp->get_content().size();
    }

    bool http_response_factory::empty_headers(){
        return resp->empty_headers();
    }

    void http_response_factory::setOnChunked(const std::function<void(int, const std::string&)>& onChunked){
        on_chunked_ = onChunked;
    }

}
