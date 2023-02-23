#ifndef THINGER_HTTP_UTIL_URL_HPP
#define THINGER_HTTP_UTIL_URL_HPP

#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <map>

namespace thinger::http::util::url{

    std::string uri_path_encode(const std::string &value);

    std::string url_encode(const std::string &value);

    bool url_decode(const std::string &in, std::string &out);

    std::string url_decode(const std::string &in);

    void parse_url_encoded_data(const std::string &in, std::multimap<std::string, std::string>& store);

    void parse_url_encoded_data(std::string::const_iterator& start, std::string::const_iterator& end, std::multimap<std::string, std::string>& store);

    std::string get_url_encoded_data(const std::multimap<std::string, std::string>& store);

}

#endif