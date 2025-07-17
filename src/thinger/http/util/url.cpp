#include "url.hpp"
#include <regex>

namespace thinger::http::util::url{

    using namespace std;

    // TODO review RFC3986
    string url_encode(const string &value) {
        ostringstream escaped;
        escaped.fill('0');
        escaped << hex;

        for(string::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
            string::value_type c = (*i);

            // Keep alphanumeric and other accepted characters intact
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                escaped << c;
            }else{
                escaped << '%' << setw(2) << int((unsigned char) c);
            }
        }

        return escaped.str();
    }

    // TODO review RFC3986
    string uri_path_encode(const string &value) {
        static const std::string reserved = "-_.~/";
        ostringstream escaped;
        escaped.fill('0');
        escaped << hex;

        for (string::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
            string::value_type c = (*i);

            // Keep alphanumeric and other accepted characters intact
            if (isalnum(c) || reserved.find(c)!=string::npos) {
                escaped << c;
            }else{
                escaped << '%' << setw(2) << int((unsigned char) c);
            }
        }

        return escaped.str();
    }

    //TODO optimize url decoding
    bool url_decode(const string &in, string &out) {
        out.clear();
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); ++i) {
            if (in[i] == '%') {
                if (i + 3 <= in.size()) {
                    int value = 0;
                    istringstream is(in.substr(i + 1, 2));
                    if (is >> hex >> value) {
                        out += static_cast<char>(value);
                        i += 2;
                    }
                    else {
                        return false;
                    }
                }
                else {
                    return false;
                }
            }
            else if (in[i] == '+') {
                out += ' ';
            }
            else {
                out += in[i];
            }
        }
        return true;
    }

    std::string url_decode(const std::string &in)
    {
        std::string out;
        if(url_decode(in, out)){
            return out;
        }
        return std::string();
    }

    void parse_url_encoded_data(const std::string &string, std::multimap<std::string, std::string>& store)
    {
        std::string::const_iterator start = string.begin();
        std::string::const_iterator end   = string.end();
        parse_url_encoded_data(start, end, store);
    }

    void parse_url_encoded_data(std::string::const_iterator& start, std::string::const_iterator& end, std::multimap<std::string, std::string>& store)
    {
        if(start==end) return;

        static const std::regex params_regex("([^=]+)=?([^&]*)&?");
        std::smatch what;

        while (std::regex_search(start, end, what, params_regex))
        {
            std::string key(what[1].first, what[1].second);
            std::string value(what[2].first, what[2].second);
            store.emplace(url_decode(key), url_decode(value));
            start = what[0].second;
        }
    }

    std::string get_url_encoded_data(const std::multimap<std::string, std::string>& store)
    {
        std::stringstream stream;
        bool first = true;
        for(const auto& query : store){
            if(!first) stream << "&";
            stream << url_encode(query.first);
            stream << "=";
            stream << url_encode(query.second);
            first = false;
        }
        return stream.str();
    }

}