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

#include "thinger_map.hpp"
#include "pson.h"
#include "iotmp_message.hpp"
#include "thinger_result.hpp"
#include "../../util/logger.hpp"
#include <functional>

#ifdef __has_include
#  if __has_include(<httplib.h>)
#    include <httplib.h>
#    include "pson_to_json.hpp"
#    include "../../util/json_decoder.hpp"
#    define THINGER_USE_LOCAL_HTTPLIB
#  endif
#endif

namespace thinger::iotmp{


    class input{
    public:

        template <class T>
        inline void operator=(T value) {
            data_ = value;
        }

        template <class T>
        inline operator T() {
            return (T) data_;
        }

        inline operator protoson::pson&(){
            return data_;
        }

        inline protoson::pson& operator[](const char *name) {
            return data_[name];
        }

        inline protoson::pson* operator->() {
            return &data_;
        }

        input(uint16_t stream_id, protoson::pson& data, bool describe=false) :
            stream_id_(stream_id), data_(data), describe_(describe)
        {

        }

        virtual ~input() {

        }

    protected:
        uint16_t        stream_id_  = 0;
        protoson::pson& data_;
        protoson::pson* params_     = nullptr;
        protoson::pson* path_       = nullptr;
        bool describe_              = false;

    public:

        const char* path(const char* field, const char* def="") {
            return path_ == nullptr ? def : (const char*)(*path_)[field];
        }

        const char* params(const char* field, const char* def="") {
            return params_ == nullptr ? def : (const char*)(*params_)[field];
        }

        void set_path_fields(pson& path){
            path_ = &path;
        }

        void set_params(pson& params){
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

        inline bool is_empty() const {
            return data_.is_empty();
        }

        inline bool fill_state() const {
            return data_.is_empty();
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
            return (T) data_;
        }

        inline operator protoson::pson&(){
            return data_;
        }

        inline protoson::pson& operator[](const char *name) {
            return data_[name];
        }

        output(protoson::pson& data, bool describe=false) : data_(data), code_(0), describe_(describe){

        }

        virtual ~output() {

        }

    protected:
        protoson::pson& data_;
        int code_;
        bool describe_;

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

        inline int get_return_code() {
            return code_;
        }

        inline bool is_empty() {
            return data_.is_empty();
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
        std::function<void(uint16_t, pson& path_parameters, pson& parameters, bool enabled, result_handler)> stream_listener_;
#endif

        io_type     io_type_            = none;
        callback    callback_;
        uint16_t    stream_id_          = 0;
        bool        stream_echo_        = true;

#ifdef THINGER_USE_LOCAL_HTTPLIB
        httplib::Server* server_        = nullptr;
        const char* name_               = nullptr;
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
        void start_stream(uint16_t stream_id, pson& parameters){

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



        void stop_stream(pson& parameters){
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

        iotmp_resource& operator()(const char* name) {
#ifdef THINGER_USE_LOCAL_HTTPLIB
            name_ = name;
#endif
            return *this;
        }

        /*
        iotmp_resource & operator()(access_type type){
            access_type_ = type;
            return *this;
        }


        iotmp_resource & operator()(pson& data){
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

        iotmp_resource & operator()(pson& in, pson& out){
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

        void fill_api(protoson::pson_object& content){
            if(io_type_!=none){
                content["fn"] = io_type_;
            }
        }

        void describe(iotmp_message& message){
            switch(io_type_){
                case output_wrapper: {
                    pson out;
                    output wrapper(out, true);
                    callback_.output_(wrapper);
                    if(!out.is_empty()) message[message::ok::PAYLOAD]["out"].swap(out);
                }
                    break;
                case input_output_wrapper: {
                    pson in, out;
                    input input_wrapper(message.get_stream_id(), in, true);
                    output output_wrapper(out, true);
                    callback_.input_output_(input_wrapper, output_wrapper);
                    if(!in.is_empty())  message[message::ok::PAYLOAD]["in"].swap(in);
                    if(!out.is_empty()) message[message::ok::PAYLOAD]["out"].swap(out);
                }
                    break;
                case input_wrapper: {
                    pson in;
                    input wrapper(message.get_stream_id(), in, true);
                    callback_.input_(wrapper);
                    if (!in.is_empty()) message[message::ok::PAYLOAD]["in"].swap(in);
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
        void run_resource(iotmp_message& request, iotmp_message& response){
            switch(io_type_){
                case input_wrapper: {
                    input in(request.get_stream_id(), request[message::run::PAYLOAD]);
                    if(request.has_field(0)) in.set_path_fields(request[0]);
                    if(request.has_field(message::run::PARAMETERS)) in.set_params(request[message::run::PARAMETERS]);
                    callback_.input_(in);
                }
                    break;
                case output_wrapper: {
                    output out(response[message::run::PAYLOAD]);
                    callback_.output_(out);
                    if(out.get_return_code()!=0) response[message::ok::PARAMETERS] = out.get_return_code();
                }
                    break;
                case run: {
                    callback_.run_();
                }
                    break;
                case input_output_wrapper: {
                    input in(request.get_stream_id(), request[message::run::PAYLOAD]);
                    if(request.has_field(0)) in.set_path_fields(request[0]);
                    if(request.has_field(message::run::PARAMETERS)) in.set_params(request[message::run::PARAMETERS]);

                    output out(response[message::run::PAYLOAD], request[message::run::PAYLOAD].is_empty());
                    callback_.input_output_(in, out);
                    if(out.get_return_code()!=0) response[message::ok::PARAMETERS] = out.get_return_code();
                }
                    break;
                case none:
                    break;
            }
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

        bool matches(const char* res_path, const char* req_path, pson& matches){
            while(*res_path){
                if(*res_path==':'){
                    const char* start_key = ++res_path;
                    while(*res_path && *res_path!='/') ++res_path;
                    size_t key_size = res_path-start_key;
                    char key[key_size+1];
                    memcpy(key, start_key, key_size);
                    key[key_size] = 0;

                    const char* start_value = req_path;
                    while(*req_path && *req_path!='/') ++req_path;
                    size_t value_size = req_path-start_value;
                    char value[value_size+1];
                    memcpy(value, start_value, value_size);
                    value[value_size] = 0;

                    matches[(const char*)key] = (const char*)value;
                }else if(*res_path++!=*req_path++){
                    return false;
                }
            };
            return !*res_path && !*req_path;
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
                LOG_INFO("starting local endpoint: %s", path.c_str());
                server_->Get(path.c_str(), [this](const httplib::Request& req, httplib::Response& res) {
                    protoson::pson in, path;
                    input input_body(0, in, true);
                    if(matches(name_, req.path.substr(1).c_str(), path)) input_body.set_path_fields(path);
                    callback_.input_(input_body);
                    if(!in.is_empty()){
                        // convert output to json
                        std::stringstream json_result;
                        json_encoder encoder(json_result);
                        encoder.encode(in);
                        res.set_content(json_result.str(), "application/json");
                    }
                });
                server_->Post(path.c_str(), [this](const httplib::Request& req, httplib::Response& res) {
                    // decode input
                    protoson::pson in, path;
                    input input_body(0, in);
                    if(matches(name_, req.path.substr(1).c_str(), path)) input_body.set_path_fields(path);
                    // try to decode json to pson
                    if(protoson::json_decoder::parse(req.body, in)){
                        // call the regular input callback
                        callback_.input_(input_body);
                    }else{
                        res.status = 400;
                    }
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
                    protoson::pson out;
                    output out_wrapper(out);

                    // call input output callback;
                    callback_.output_(out_wrapper);

                    // convert generated data
                    if(!out.is_empty()){
                        // convert output to json
                        std::stringstream json_result;
                        json_encoder encoder(json_result);
                        encoder.encode(out);

                        // set return content type
                        res.set_content(json_result.str(), "application/json");
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
                    protoson::pson in, out;
                    input input_body(0, in, true);
                    output output_body(out, true);
                    callback_.input_output_(input_body, output_body);
                    if(!in.is_empty()){
                        // convert output to json
                        std::stringstream json_result;
                        json_encoder encoder(json_result);
                        encoder.encode(in);
                        res.set_content(json_result.str(), "application/json");
                    }
                });

                server_->Post(path.c_str(), [this](const httplib::Request& req, httplib::Response& res) {

                    // decode input
                    protoson::pson in;
                    if(!req.body.empty() && !protoson::json_decoder::parse(req.body, in)){
                        res.status = 400;
                        return;
                    }

                    // define output
                    protoson::pson out;

                    input in_wrapper(0, in);
                    output out_wrapper(out);

                    // call input output callback;
                    callback_.input_output_(in_wrapper, out_wrapper);

                    // set response payload
                    if(!out.is_empty()){
                        std::stringstream json_result;
                        json_encoder encoder(json_result);
                        encoder.encode(out);
                        res.set_content(json_result.str(), "application/json");
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
        void set_stream_handler(std::function<void(uint16_t stream_id, pson& path_parameters, pson& parameters, bool enabled, result_handler)> stream_listener){
            stream_listener_ = stream_listener;
        }

        void handle_stream(uint16_t stream_id, pson& path_parameters, pson& parameters, bool enabled, result_handler handler){
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
        std::function<void(uint16_t, pson& path_parameters, pson& parameters, bool enabled, result_handler)> get_stream_listener(){
            return stream_listener_;
        }

#endif

    };

}

#endif