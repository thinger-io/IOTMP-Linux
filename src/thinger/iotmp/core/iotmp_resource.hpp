// The MIT License (MIT)
//
// Copyright (c) 2017 THINK BIG LABS SL
// Author: alvarolb@gmail.com (Alvaro Luis Bustamante)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef THINGER_CLIENT_RESOURCE_HPP
#define THINGER_CLIENT_RESOURCE_HPP

#include "iotmp_message.hpp"
#include "thinger_result.hpp"
#include <thinger/util/logger.hpp>
#include <functional>
#include <string_view>
#include <nlohmann/json.hpp>

#ifdef __has_include
#  if __has_include(<httplib.h>)
#    include <httplib.h>
#    define THINGER_USE_LOCAL_HTTPLIB
#  endif
#endif

namespace thinger::iotmp{

    using json = nlohmann::json;

    // Helper function to safely get values from JSON with default fallback
    // Supports nested paths using dot notation (e.g., "size.cols")
    template <typename T>
    auto get_value(const nlohmann::json& json, const std::string& path, const T& default_value)
    -> typename std::conditional<
            std::is_same<T, nlohmann::json>::value || std::is_same<T, std::string>::value,
            const T&,
            T>::type {

        const auto* current = &json;

        if(!path.empty()){
            size_t start = 0;
            size_t end = path.find('.');

            // Traverse the JSON over path segments
            while (end != std::string::npos) {
                const std::string_view current_path_segment(path.data() + start, end - start);
                auto found = current->find(current_path_segment);
                if (found != current->end()) {
                    current = &(*found);
                } else {
                    return default_value;
                }
                start = end + 1;
                end = path.find('.', start);
            }

            // Last segment or the case without any '.' in the path.
            const std::string_view last_path_segment(path.data() + start, path.length() - start);
            const auto& found = current->find(last_path_segment);
            if (found != current->end()) {
                current = &(*found);
            } else {
                return default_value;
            }
        }

        if constexpr (std::is_same<T, nlohmann::json>::value) {
            return *current;
        } else if constexpr (std::is_same<T, std::string>::value) {
            if(current->is_string()) return current->get_ref<const std::string&>();
            return default_value;
        } else if constexpr (std::is_same<T, bool>::value) {
            if(current->is_boolean()) return current->get<bool>();
            return default_value;
        } else if constexpr (std::is_arithmetic<T>::value) {
            if(current->is_number()) return current->get<T>();
            return default_value;
        } else {
            return default_value;
        }
    }

    // Empty static values for use as defaults
    namespace empty{
        static const nlohmann::json json;
        static const nlohmann::json array = nlohmann::json::array();
        static const std::string string;
    }

    // Proxy class for auto-initializing JSON fields with default values
    class json_proxy {
        json& data_;
        bool describe_;
    public:
        json_proxy(json& data, bool describe) : data_(data), describe_(describe) {}

        // Conversion operator - auto-initializes with default value if null and in describe mode
        template<class T>
        operator T() {
            if(data_.is_null()) {
                data_ = T{};
            }
            return data_.get<T>();
        }

        // Allow assignment
        template<class T>
        json_proxy& operator=(T value) {
            data_ = value;
            return *this;
        }

        // Allow nested access
        json_proxy operator[](const char* name) {
            return json_proxy(data_[name], describe_);
        }

        // Access to underlying json
        json& get() { return data_; }
        const json& get() const { return data_; }

        // Implicit conversion to json& for backwards compatibility
        operator json&() { return data_; }
        operator const json&() const { return data_; }
    };

    class input{
    public:

        template <class T>
        inline void operator=(T value) {
            data_ = value;
        }

        template <class T>
        inline operator T() {
            // Auto-initialize if null (for DESCRIBE mode)
            if(data_.is_null()) {
                data_ = T{};
            }
            return data_.get<T>();
        }

        inline operator json&(){
            return data_;
        }

        inline json_proxy operator[](const char *name) {
            return json_proxy(data_[name], describe_);
        }

        inline json* operator->() {
            return &data_;
        }

        input(uint16_t stream_id, json& data, bool describe=false) :
            stream_id_(stream_id), data_(data), describe_(describe)
        {

        }

        virtual ~input() {

        }

    protected:
        uint16_t    stream_id_  = 0;
        json&       data_;
        json*       params_     = nullptr;
        json*       path_       = nullptr;
        bool        describe_   = false;

    public:

        std::string path(std::string_view field, std::string_view def = "") const {
            if(path_ == nullptr || !path_->contains(field.data())) return std::string(def);
            const auto& value = (*path_)[field.data()];
            return value.is_string() ? value.get<std::string>() : std::string(def);
        }

        std::string params(std::string_view field, std::string_view def = "") const {
            if(params_ == nullptr || !params_->contains(field.data())) return std::string(def);
            const auto& value = (*params_)[field.data()];
            return value.is_string() ? value.get<std::string>() : std::string(def);
        }

        void set_path_fields(json& path){
            path_ = &path;
        }

        void set_params(json& params){
            params_ = &params;
        }

        void set_describe(bool describe) {
            describe_ = describe;
        }

        uint16_t get_stream_id(){
            return stream_id_;
        }

        bool describe() {
            return describe_;
        }

        inline bool has_params() const {
            return params_ != nullptr;
        }

        inline json& get_params() {
            static json empty = json::object();
            return params_ != nullptr ? *params_ : empty;
        }

        inline bool is_empty() const {
            return data_.is_null() || (data_.is_object() && data_.empty()) ||
                   (data_.is_array() && data_.empty());
        }

        inline bool fill_state() const {
            return is_empty();
        }

        inline json& payload() {
            return data_;
        }

        inline const json& payload() const {
            return data_;
        }
    };

    class output {

    public:

        template <class T>
        inline void operator=(T value) {
            data_ = value;
        }

        template <class T>
        inline operator T() {
            return data_.get<T>();
        }

        inline operator json&(){
            return data_;
        }

        inline json& operator[](const char *name) {
            return data_[name];
        }

        output(json& data, bool describe=false) : data_(data), code_(0), describe_(describe), success_(true){

        }

        virtual ~output() {

        }

    protected:
        json&   data_;
        int     code_;
        bool    describe_;
        bool    success_;

    public:

        bool describe() {
            return describe_;
        }

        void set_describe(bool describe) {
            describe_ = describe;
        }

        inline void set_return_code(int code) {
            code_ = code;
        }

        inline int get_return_code() const {
            return code_;
        }

        // Simple error without code (uses 500 by default)
        inline void set_error(const char* message) {
            success_ = false;
            data_["error"] = message;
        }

        // Error with specific code
        inline void set_error(int code, const char* message) {
            success_ = false;
            code_ = code;
            data_["error"] = message;
        }

        inline bool is_success() const {
            return success_;
        }

        inline bool is_empty() {
            return data_.is_null() || (data_.is_object() && data_.empty()) ||
                   (data_.is_array() && data_.empty());
        }

        inline json& payload() {
            return data_;
        }

        inline const json& payload() const {
            return data_;
        }
    };

    class iotmp_resource {

    public:
        enum io_type {
            none                  = 0,
            run                   = 1,
            input_wrapper         = 2,
            output_wrapper        = 3,
            input_output_wrapper  = 4
        };

    private:

        // calback for function, input, output, or input/output
        struct callback{
            std::function<void()> run_;
            std::function<void(input& in)> input_;
            std::function<void(output& out)> output_;
            std::function<void(input& in, output& out)> input_output_;
        };

#ifdef THINGER_ENABLE_STREAM_LISTENER
        std::function<void(uint16_t, json_t& path_parameters, json_t& parameters, bool enabled, result_handler)> stream_listener_;
#endif

        io_type     io_type_            = none;
        callback    callback_;
        uint16_t    stream_id_          = 0;
        bool        stream_echo_        = true;

#ifdef THINGER_USE_LOCAL_HTTPLIB
        httplib::Server* server_        = nullptr;
        std::string name_;
#endif

    public:
        iotmp_resource(){

        }

#ifdef THINGER_USE_LOCAL_HTTPLIB
        iotmp_resource & operator()(httplib::Server& server){
            server_ = &server;
            return *this;
        }
#endif

        bool stream_enabled(){
            return stream_id_!=0;
        }

        void set_stream_id(uint16_t stream_id){
            stream_id_ = stream_id;
        }

        uint16_t get_stream_id(){
            return stream_id_;
        }

/*
        void start_stream(uint16_t stream_id, json_t& parameters){

            if(parameters.is_number()) {
                stream_id_sampling_ = stream_id;
                streaming_freq_ = stream_id;
                last_streaming_ = 0;
            }else {
                stream_id_ = stream_id;
                streaming_freq_ = 0;
            }

#ifdef THINGER_ENABLE_STREAM_LISTENER
            if(stream_listener_){
                stream_listener_(stream_id, parameters, true);
            }
#endif
        }



        void stop_stream(json_t& parameters){
#ifdef THINGER_ENABLE_STREAM_LISTENER
            if(stream_listener_){
                stream_listener_(stream_id_, parameters, false);
            }
#endif
            stream_id_ = 0;
            streaming_freq_ = 0;
        }

        bool stream_required(unsigned long timestamp){
            // sample interval is activated
            if(streaming_freq_>0){
                if(timestamp-last_streaming_>=streaming_freq_){
                    last_streaming_ = timestamp;
                    return true;
                }
            }
            return false;
        }

        uint16_t get_stream_id() const{
            return stream_id_;
        }

        void set_stream_id(uint16_t stream_id){
            stream_id_ = stream_id;
        }

        bool stream_enabled() const{
            return stream_id_ > 0;
        }

         */

        iotmp_resource& operator()(std::string_view name) {
#ifdef THINGER_USE_LOCAL_HTTPLIB
            name_ = std::string(name);
#endif
            return *this;
        }

        /*
        iotmp_resource & operator()(access_type type){
            access_type_ = type;
            return *this;
        }


        iotmp_resource & operator()(json_t& data){
            switch(io_type_){
                case input_wrapper:
                    callback_.input_(data);
                    break;
                case input_output_wrapper:
                    callback_.input_output_(data, data);
                    break;
                case output_wrapper:
                    callback_.output_(data);
                    break;
                default:
                    break;
            }
            return *this;
        }

        iotmp_resource & operator()(json_t& in, json_t& out){
            switch(io_type_){
                case input_wrapper:
                    callback_.input_(out);
                    break;
                case input_output_wrapper:
                    callback_.input_output_(in, out);
                    break;
                case output_wrapper:
                    callback_.output_(out);
                    break;
                default:
                    break;
            }
            return *this;
        }
         */


        bool stream_echo(){
            return stream_echo_;
        }

        void set_stream_echo(bool enabled){
            stream_echo_ = enabled;
        }

        io_type get_io_type(){
            return io_type_;
        }

        void fill_api(json_t& content){
            if(io_type_!=none){
                content["fn"] = io_type_;
            }
        }

        void describe(iotmp_message& message){
            switch(io_type_){
                case output_wrapper: {
                    json out;
                    output wrapper(out, true);
                    callback_.output_(wrapper);
                    if(!out.is_null()) {
                        message[message::field::PAYLOAD]["out"].swap(out);
                    }
                }
                    break;
                case input_output_wrapper: {
                    json in, out;
                    input input_wrapper(message.get_stream_id(), in, true);
                    output output_wrapper(out, true);
                    callback_.input_output_(input_wrapper, output_wrapper);
                    if(!in.is_null()) {
                        message[message::field::PAYLOAD]["in"].swap(in);
                    }
                    if(!out.is_null()) {
                        message[message::field::PAYLOAD]["out"].swap(out);
                    }
                }
                    break;
                case input_wrapper: {
                    json in;
                    input wrapper(message.get_stream_id(), in, true);
                    callback_.input_(wrapper);
                    if(!in.is_null()) {
                        message[message::field::PAYLOAD]["in"].swap(in);
                    }
                }
                    break;
                default:
                    break;
            }
        }

    public:

        /**
         * Handle a request and fill a possible response
         */
        bool run_resource(iotmp_message& request, iotmp_message& response){
            bool success = true;
            
            switch(io_type_){
                case input_wrapper: {
                    input in(request.get_stream_id(), request[message::field::PAYLOAD]);
                    if(request.has_field(0)) in.set_path_fields(request[0]);
                    if(request.has_field(message::field::PARAMETERS)) in.set_params(request[message::field::PARAMETERS]);
                    callback_.input_(in);
                }
                    break;
                case output_wrapper: {
                    output out(response[message::field::PAYLOAD]);
                    callback_.output_(out);
                    if(out.get_return_code()!=0) response[message::field::PARAMETERS] = out.get_return_code();
                    success = out.is_success();
                }
                    break;
                case run: {
                    callback_.run_();
                }
                    break;
                case input_output_wrapper: {
                    input in(request.get_stream_id(), request[message::field::PAYLOAD]);
                    if(request.has_field(0)) in.set_path_fields(request[0]);
                    if(request.has_field(message::field::PARAMETERS)) in.set_params(request[message::field::PARAMETERS]);

                    output out(response[message::field::PAYLOAD], request[message::field::PAYLOAD].empty());
                    callback_.input_output_(in, out);
                    if(out.get_return_code()!=0) response[message::field::PARAMETERS] = out.get_return_code();
                    success = out.is_success();
                }
                    break;
                case none:
                    break;
            }
            
            return success;
        }

        /**
         * Establish a function without input or output parameters
         */
        iotmp_resource& operator=(std::function<void()> run_function){
            set_function(run_function);
            return *this;
        }

        /**
         * Establish a function without input or output parameters
         */
        iotmp_resource& set_function(std::function<void()> run_function){
            io_type_ = run;
            callback_.run_ = run_function;
#ifdef THINGER_USE_LOCAL_HTTPLIB
            if(server_!=nullptr){
                std::string path{"/"};
                path.append(name_);
                server_->Post(path.c_str(), [this](const httplib::Request& req, httplib::Response& res) {
                    callback_.run_();
                });
            }
#endif
            return *this;
        }

        /**
         * Establish a function with input parameters
         */
        iotmp_resource& operator=(std::function<void(input&)> in_function){
            set_input(in_function);
            return *this;
        }

        bool matches(std::string_view res_path, std::string_view req_path, json_t& matches){
            size_t res_idx = 0;
            size_t req_idx = 0;

            while(res_idx < res_path.size()){
                if(res_path[res_idx] == ':'){
                    ++res_idx;
                    size_t key_start = res_idx;
                    while(res_idx < res_path.size() && res_path[res_idx] != '/') ++res_idx;
                    std::string key(res_path.substr(key_start, res_idx - key_start));

                    size_t value_start = req_idx;
                    while(req_idx < req_path.size() && req_path[req_idx] != '/') ++req_idx;
                    std::string value(req_path.substr(value_start, req_idx - value_start));

                    matches[key] = value;
                }
                else if(req_idx >= req_path.size() || res_path[res_idx] != req_path[req_idx]){
                    return false;
                }
                else {
                    ++res_idx;
                    ++req_idx;
                }
            }
            return res_idx == res_path.size() && req_idx == req_path.size();
        }

        /**
         * Establish a function with input parameters
         */
        iotmp_resource& set_input(std::function<void(input&)> in_function){
            io_type_ = input_wrapper;
            callback_.input_ = in_function;

#ifdef THINGER_USE_LOCAL_HTTPLIB
            if(server_!=nullptr){
                std::string path = std::string("/") + name_;
                const std::regex re(":([^\\/]+)");
                while (std::regex_search(path, re)) {
                    path = std::regex_replace(path, re, "([^\\/]+)");
                }
                LOG_INFO("starting local endpoint: {}", path);
                server_->Get(path.c_str(), [this](const httplib::Request& req, httplib::Response& res) {
                    json_t in, path;
                    input input_body(0, in, true);
                    std::string req_path = req.path.substr(1);
                    if(matches(name_, req_path, path)) input_body.set_path_fields(path);
                    callback_.input_(input_body);
                    if(!in.empty()){
                        // convert output to json
                        res.set_content(in.dump(), "application/json");
                    }
                });
                server_->Post(path.c_str(), [this](const httplib::Request& req, httplib::Response& res) {
                    // decode input
                    json_t in, path;
                    input input_body(0, in);
                    std::string req_path = req.path.substr(1);
                    if(matches(name_, req_path, path)) input_body.set_path_fields(path);
                    // parse json with validation
                    if(!json_t::accept(req.body)) {
                        res.status = 400;
                        return;
                    }
                    in = json_t::parse(req.body, nullptr, false);
                    callback_.input_(input_body);
                });
            }
#endif
            return *this;
        }

        /**
         * Establish a function that only generates an output
         */
        iotmp_resource & operator=(std::function<void(output&)> out_function){
            set_output(out_function);
            return *this;
        }

        /**
         * Establish a function that only generates an output
         */
        iotmp_resource & set_output(std::function<void(output&)> out_function){
            io_type_ = output_wrapper;
            callback_.output_ = out_function;

#ifdef THINGER_USE_LOCAL_HTTPLIB
            if(server_!=nullptr){
                std::string path{"/"};
                path.append(name_);
                server_->Get(path.c_str(), [this](const httplib::Request& req, httplib::Response& res) {
                    // define output & wrapper
                    json_t out;
                    output out_wrapper(out);

                    // call input output callback;
                    callback_.output_(out_wrapper);

                    // convert generated data
                    if(!out.empty()){
                        // set return content type
                        res.set_content(out.dump(), "application/json");
                    }

                    // set return code if any established
                    if(out_wrapper.get_return_code()!=0){
                        res.status = out_wrapper.get_return_code();
                    }
                });
            }
#endif

            return *this;
        }

        /**
         * Establish a function that can receive input parameters and generate an output
         */
        iotmp_resource& operator=(std::function<void(input& in, output& out)> input_output_function){
            set_input_output(input_output_function);
            return *this;
        }

        /**
         * Establish a function that can receive input parameters and generate an output
         */
        iotmp_resource& set_input_output(std::function<void(input& in, output& out)> input_output_function){
            io_type_ = input_output_wrapper;
            callback_.input_output_ = input_output_function;

#ifdef THINGER_USE_LOCAL_HTTPLIB
            if(server_!=nullptr){
                std::string path{"/"};
                path.append(name_);

                server_->Get(path.c_str(), [this](const httplib::Request& req, httplib::Response& res) {
                    json_t in, out;
                    input input_body(0, in, true);
                    output output_body(out, true);
                    callback_.input_output_(input_body, output_body);
                    if(!out.empty()){
                        // convert output to json
                        res.set_content(out.dump(), "application/json");
                    }
                });

                server_->Post(path.c_str(), [this](const httplib::Request& req, httplib::Response& res) {

                    // decode input
                    json_t in;
                    if(!req.body.empty()){
                        if(!json_t::accept(req.body)) {
                            res.status = 400;
                            return;
                        }
                        in = json_t::parse(req.body, nullptr, false);
                    }

                    // define output
                    json_t out;

                    input in_wrapper(0, in);
                    output out_wrapper(out);

                    // call input output callback;
                    callback_.input_output_(in_wrapper, out_wrapper);

                    // set response payload
                    if(!out.empty()){
                        res.set_content(out.dump(), "application/json");
                    }

                    // set return code if any established
                    if(out_wrapper.get_return_code()!=0){
                        res.status = out_wrapper.get_return_code();
                    }
                });
            }
#endif

            return *this;

        }

#ifdef THINGER_ENABLE_STREAM_LISTENER
        /**
          * Establish a function for receiving stream listening events
          */
        void set_stream_handler(std::function<void(uint16_t stream_id, json_t& path_parameters, json_t& parameters, bool enabled, result_handler)> stream_listener){
            stream_listener_ = stream_listener;
        }

        void handle_stream(uint16_t stream_id, json_t& path_parameters, json_t& parameters, bool enabled, result_handler handler){
            if(stream_listener_){
                stream_listener_(stream_id, path_parameters, parameters, enabled, std::move(handler));
            }
        }

        bool has_stream_handler() const{
            return (bool) stream_listener_;
        }

        /**
          * Establish a function for receiving stream listening events
          */
        std::function<void(uint16_t, json_t& path_parameters, json_t& parameters, bool enabled, result_handler)> get_stream_listener(){
            return stream_listener_;
        }

#endif

    };

}

#endif