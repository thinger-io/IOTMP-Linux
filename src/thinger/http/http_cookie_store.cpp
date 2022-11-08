#include "http_cookie_store.hpp"

namespace thinger::http{

    http_cookie_store::http_cookie_store(){

    }

    http_cookie_store::~http_cookie_store(){

    }

    bool http_cookie_store::update_from_headers(const http_headers& headers){
        bool updated = false;
        std::vector<std::string> cookieHeaders = headers.get_headers_with_key(header::set_cookie);
        for(const auto& header : cookieHeaders){
            http_cookie newCookie = http_cookie::parse(header);
            if(!newCookie.getName().empty()){
                cookies_[newCookie.getName()] = newCookie;
                updated = true;
            }
        }
        return updated;
    }

    std::string http_cookie_store::get_cookie_string() const{
        std::string cookieString = "";
        for(const auto& cookie : cookies_){
            if(!cookieString.empty()){
                cookieString += "; ";
            }
            cookieString += cookie.second.getName();
            cookieString += "=";
            cookieString += cookie.second.getValue();
        }
        return cookieString;
    }

}