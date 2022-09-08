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

#ifndef THINGER_CLIENT_HPP
#define THINGER_CLIENT_HPP

#include "pson.h"
#include "thinger_map.hpp"
#include "thinger_resource.hpp"
#include "thinger_message.hpp"
#include "thinger_encoder.hpp"
#include "thinger_decoder.hpp"
#include "thinger_message.hpp"
#include "thinger_io.hpp"
#include "thinger_log.hpp"
#include "thinger_streams.hpp"

#define KEEP_ALIVE_MILLIS 50000

#ifndef THINGER_AUTH_MODE
#define THINGER_AUTH_MODE THINGER_AUTH_MODE_LOGIN
#endif

#ifdef __has_include
#  if __has_include(<functional>)
#    include <functional>
#    define THINGER_USE_FUNCTIONAL
#  endif
#endif

#ifdef __has_include
#  if __has_include(<FreeRTOS.h>)
#    include <FreeRTOS.h>
#    define THINGER_FREE_RTOS_MULTITASK
#  endif
#endif

#ifdef __has_include
#  if __has_include(<mutex>)
#    include <mutex>
#    define THINGER_MULTITHREAD
#  endif
#endif

#if defined(THINGER_FREE_RTOS_MULTITASK) || defined(THINGER_MULTITHREAD)
#define THINGER_MULTITASK
    #define synchronized(code)  \
        lock();                 \
        code                    \
        unlock();
#else
#define synchronized(code)  \
        code
#endif

namespace thinger_client{

    enum THINGER_STATE{
        NETWORK_CONNECTING,
        NETWORK_CONNECTED,
        NETWORK_CONNECT_ERROR,
        SOCKET_CONNECTING,
        SOCKET_CONNECTED,
        SOCKET_CONNECTION_ERROR,
        SOCKET_DISCONNECTED,
        SOCKET_TIMEOUT,
        SOCKET_READ_ERROR,
        SOCKET_WRITE_ERROR,
        THINGER_AUTHENTICATING,
        THINGER_AUTHENTICATED,
        THINGER_AUTH_FAILED,
        THINGER_STOP_REQUEST,
        THINGER_KEEP_ALIVE_SENT,
        THINGER_KEEP_ALIVE_RECEIVED
    };

    using namespace protoson;

    class thinger : public thinger_io{
    public:
        thinger(const char* username="", const char* device="", const char* credentials="", const char* host="backend.thinger.io", uint16_t port=25206, bool secure=true) :
                encoder(*this),
                decoder(*this),
                last_keep_alive(0),
                keep_alive_response(true),
                streams_(*this),
                host_(host),
                port_(port),
#ifdef THINGER_OPEN_SSL
                secure_(true),
#else
                secure_(false),
#endif
                username_(username),
                device_id_(device),
                device_password_(credentials)
        {
#ifdef THINGER_FREE_RTOS_MULTITASK
            semaphore_ = xSemaphoreCreateMutex();
#endif
        }

        virtual ~thinger(){

        }

#ifdef THINGER_USE_FUNCTIONAL
        void set_state_listener(std::function<void(THINGER_STATE)> listener){
#else
        void set_state_listener(void (*listener)(THINGER_STATE state)){
#endif
            state_listener_ = listener;
        }

    protected:
        thinger_write_encoder encoder;
        thinger_read_decoder decoder;
        unsigned long last_keep_alive;
        bool keep_alive_response;
        thinger_streams streams_;
        const char* host_;
        unsigned int port_;
        bool secure_;
        const char* username_;
        const char* device_id_;
        const char* device_password_;
        thinger_map<const char*, thinger_resource> resources_;
        thinger_map<const char*, thinger_resource> topics_;

#ifdef THINGER_USE_FUNCTIONAL
        std::function<void(THINGER_STATE state)> state_listener_;
#else
        void (*state_listener_)(THINGER_STATE state) = nullptr;
#endif

#ifdef THINGER_FREE_RTOS_MULTITASK
        SemaphoreHandle_t semaphore_;
#endif

#ifdef THINGER_MULTITHREAD
        std::mutex mutex_;
#endif

    public:
        /**
        * Can be override to start reconnection process
        */
        virtual void disconnected(){
            // stop all streaming resources after disconnect
            streams_.clear();
        }

        void connected(){
            // stop all streaming resources after disconnect
            streams_.clear();
        }

        void set_host(const char* host){
            host_ = host;
        }

        const char* get_host(){
            return host_;
        }

        void set_port(unsigned short port){
            port_ = port;
        }

        unsigned int get_port(){
            return port_;
        }

        void set_secure_connection(bool secure){
            secure_ = secure;
        }

        bool set_secure_connection(){
            return secure_;
        }

        void set_credentials(const char* username, const char* device_id, const char* device_password){
            username_ = username;
            device_id_ = device_id;
            device_password_ = device_password;
        }

    protected:

#ifdef THINGER_FREE_RTOS_MULTITASK
        bool lock(){
            return xSemaphoreTake(semaphore_, portMAX_DELAY);
        }

        bool unlock(){
            return xSemaphoreGive(semaphore_);
        }
#endif

#ifdef THINGER_MULTITHREAD
        void lock(){
            mutex_.lock();
        }

        void unlock(){
            mutex_.unlock();
        }
#endif

        void notify_state(THINGER_STATE state, const char* reason=""){
            switch(state){
                case NETWORK_CONNECTING:
                    THINGER_LOG_TAG("NETWORK", "Starting connection...");
                    break;
                case NETWORK_CONNECTED:
                    THINGER_LOG_TAG("NETWORK", "Connected!");
                    break;
                case NETWORK_CONNECT_ERROR:
                    THINGER_LOG_ERROR_TAG("NETWORK", "Cannot connect!");
                    break;
                case SOCKET_CONNECTING:
                    THINGER_LOG_TAG("SOCKET", "Connecting to %s:%d (TLS: %d)", host_, port_, secure_);
                    break;
                case SOCKET_READ_ERROR:
                    THINGER_LOG_ERROR_TAG("SOCKET", "Error while reading: %s", reason);
                    break;
                case SOCKET_WRITE_ERROR:
                    THINGER_LOG_ERROR_TAG("SOCKET", "Error while writing: %s", reason);
                    break;
                case SOCKET_CONNECTED:
                    THINGER_LOG_TAG("SOCKET", "Connected!");
                    break;
                case SOCKET_CONNECTION_ERROR:
                    THINGER_LOG_ERROR_TAG("SOCKET", "Error while connecting: %s", reason);
                    break;
                case SOCKET_DISCONNECTED:
                    THINGER_LOG_TAG("SOCKET", "is now closed!");
                    break;
                case SOCKET_TIMEOUT:
                    THINGER_LOG_ERROR_TAG("SOCKET", "timeout!");
                    break;
                case THINGER_AUTHENTICATING:
                    THINGER_LOG_TAG("THINGER", "Authenticating. user: '%s', device: '%s'", username_, device_id_);
                    break;
                case THINGER_AUTHENTICATED:
                    THINGER_LOG_TAG("THINGER", "Authenticated!");
                    break;
                case THINGER_AUTH_FAILED:
                    THINGER_LOG_ERROR_TAG("THINGER", "Auth Failed! Check username, device id, or device credentials.");
                    break;
                case THINGER_STOP_REQUEST:
                    THINGER_LOG_ERROR_TAG("THINGER", "Client was requested to stop.");
                    break;
                case THINGER_KEEP_ALIVE_SENT:
                    THINGER_LOG_TAG("THINGER", "Keep alive sent");
                    break;
                case THINGER_KEEP_ALIVE_RECEIVED:
                    THINGER_LOG_TAG("THINGER", "Keep alive received");
                    break;
            }
            if(state_listener_){
                state_listener_(state);
            }
        }

        void subscribe_resources(){
            for(auto it = topics_.begin(); it.valid(); it.next()) {
                const char *resource = it.item().left;
                thinger_resource &res = it.item().right;
                thinger_message start_stream(message::type::START_STREAM);
                start_stream[message::start_stream::RESOURCE] = resource;

                start_stream[message::start_stream::PARAMETERS]["type"] = "mqtt";
                pson_array& scopes = start_stream[message::start_stream::PARAMETERS]["scopes"];

                if(res.get_io_type() == thinger_resource::input_wrapper) {
                    scopes.add("subscribe");
                } else if (res.get_io_type() == thinger_resource::output_wrapper) {
                    scopes.add("publish");
                }else{
                    // skip resources that does not provide an input or output
                    continue;
                }

                if (send_message_with_ack(start_stream)) {
                    static pson dummy;
                    streams_.start(start_stream.get_stream_id(), res, dummy, start_stream[message::start_stream::PARAMETERS]);
                }
            }
        }

        bool connect(const char* username, const char* device_id, const char* credential){
            // reset keep alive status for each connection
            keep_alive_response = true;
            // create an authentication message
            thinger_message message(message::type::CONNECT);
            // initialize auth type to 1 (username/password)
            message[message::connect::PARAMETERS] = 0x01;
            // get credentials field
            pson_array& array = message[message::connect::CREDENTIALS];
            // add username, device and password
            array.add(username).add(device_id).add(credential);
            // send and wait server response
            return send_message_with_ack(message);
        }

    public:

        thinger_resource & operator[](const char* path){
            return resources_[path](path);
        }

        thinger_resource& resource(const char* path){
            return resources_[path](path);
        }

        thinger_resource& topic(const char* path){
            return topics_[path](path);
        }

        /**
         * Read a property stored in the server
         * @param property_identifier property identifier
         * @param data pson structure to be filled with the property data received from server
         * @return true if the property read was ok
         */
        bool get_property(const char* property_identifier, protoson::pson& data){
            thinger_message message(message::type::RUN);
            message[message::run::RESOURCE] = server::READ_DEVICE_PROPERTY;
            message[message::run::PARAMETERS] = property_identifier;
            return send_message(message, data);
        }

        /**
         * Set a property in the server
         * @param property_identifier property identifier
         * @param data pson structure with the data to be stored in the server
         * @param confirm_write flag to control whether it is required a write confirmation or not
         * @return
         */
        bool set_property(const char* property_identifier, pson& data, bool confirm_write=false){
            thinger_message message(message::type::RUN);
            message[message::run::RESOURCE] = server::SET_DEVICE_PROPERTY;
            message[message::run::PARAMETERS] = property_identifier;
            message[message::run::PAYLOAD].swap(data);
            return send_message_with_ack(message, confirm_write);
        }

        /**
         * Execute a resource in a remote device (without data)
         * @param device_name remote device identifier (must be connected to your account)
         * @param resource_name remote resource identifier (must be defined in the remote device)
         * @return
         */
        bool call_device(const char* device_name, const char* resource_name, bool confirm_call=false){
            thinger_message message(message::type::RUN);
            message[message::run::RESOURCE] = server::CALL_DEVICE;
            pson_array& data = message[message::run::PARAMETERS];
            data.add(device_name).add(resource_name);
            return send_message_with_ack(message, confirm_call);
        }

        /**
        * Execute a resource in a remote device (with a custom pson payload)
        * @param device_name remote device identifier (must be connected to your account)
        * @param resource_name remote resource identifier (must be defined in the remote device)
        * @param data pson structure to be sent to the remote resource input
        * @return
        */
        bool call_device(const char* device_name, const char* resource_name, pson& data, bool confirm_call=false){
            thinger_message message(message::type::RUN);
            message[message::run::RESOURCE] = server::CALL_DEVICE;
            pson_array& params = message[message::run::PARAMETERS];
            params.add(device_name).add(resource_name);
            message[message::run::PAYLOAD].swap(data);
            return send_message_with_ack(message, confirm_call);
        }

        /**
        * Execute a resource in a remote device (with a payload from a local resource)
        * @param device_name remote device identifier (must be connected to your account)
        * @param resource_name remote resource identifier (must be defined in the remote device)
        * @param resource local device resource, as defined in code, i.e., thing["location"]
        * @return
        */
        bool call_device(const char* device_name, const char* resource_name, thinger_resource& resource, bool confirm_call=false){
            /*
            thinger_message message(message::type::RUN);
            message.get_field(message::run_fields::ACTION) = message::run_action::CALL_DEVICE;
            message.get_field(message::call_device::TARGET_DEVICE) = device_name;
            message.get_field(message::field::RESOURCE) = resource_name;
            resource.fill_state(message);
            return send_message_with_ack(message, confirm_call);
             */
            return false;
        }

        /**
         * Cal a server endpoint (without any data)
         * @param endpoint_name endpoint identifier, as defined in the server
         * @return
         */
        bool call_endpoint(const char* endpoint_name, bool confirm_call=false){
            thinger_message message(message::type::RUN);
            message[message::run::RESOURCE] = server::CALL_ENDPOINT;
            message[message::run::PARAMETERS] = endpoint_name;
            return send_message_with_ack(message, confirm_call);
        }

        /**
         * Call a server endpoint
         * @param endpoint_name endpoint identifier, as defined in the server
         * @param data data in pson format to be used as data source for the endpoint call
         * @return
         */
        bool call_endpoint(const char* endpoint_name, pson& data, bool confirm_call=false){
            thinger_message message(message::type::RUN);
            message[message::run::RESOURCE] = server::CALL_ENDPOINT;
            message[message::run::PARAMETERS] = endpoint_name;
            message[message::run::PAYLOAD].swap(data);
            return send_message_with_ack(message, confirm_call);
        }

        /**
         * Call a server endpoint
         * @param endpoint_name endpoint identifier, as defined in the server
         * @param resource resource to be used as data source for the endpoint call, i.e., thing["location"]
         * @return
         */
        bool call_endpoint(const char* endpoint_name, thinger_resource& resource, bool confirm_call=false){
            /*
            thinger_message message(message::type::RUN);
            message.get_field(message::run_fields::ACTION) = message::run_action::CALL_ENDPOINT;
            message.get_field(message::field::RESOURCE) = endpoint_name;
            resource.fill_state(message);
            return send_message_with_ack(message, confirm_call);
             */
            return false;
        }

        /**
         * Call a server endpoint
         * @param endpoint_name endpoint identifier, as defined in the server
         * @param resource_name name of the resource to be used as data source for the endpoint call, i.e., "location"
         * @return
         */
        bool call_endpoint(const char* endpoint_name, const char* resource_name, bool confirm_call=false){
            return call_endpoint(endpoint_name, resources_[resource_name], confirm_call);
        }

        /**
         * Write arbitrary data to a given bucket identifier
         * @param bucket_id bucket identifier
         * @param data data to write defined in a pson structure
         * @return
         */
        bool write_bucket(const char* bucket_id, pson& data, bool confirm_write=false){
            thinger_message message(message::type::RUN);
            message[message::run::RESOURCE] = server::WRITE_BUCKET;
            message[message::run::PARAMETERS] = bucket_id;
            message[message::run::PAYLOAD].swap(data);
            return send_message_with_ack(message, confirm_write);
        }

        /**
         * Write a resource to a given bucket identifier
         * @param bucket_id bucket identifier
         * @param resource_name resource defined in the code, i.e., thing["location"]
         * @return
         */
        bool write_bucket(const char* bucket_id, thinger_resource& resource, bool confirm_write=false){
            /*
            thinger_message message(message::type::RUN);
            message.get_field(message::run_fields::ACTION) = message::run_action::WRITE_BUCKET;
            message.get_field(message::field::RESOURCE) = bucket_id;
            resource.fill_state(message);
            return send_message_with_ack(message, confirm_write);
             */
            return false;
        }

        /**
         * Write a resource to a given bucket identifier
         * @param bucket_id bucket identifier
         * @param resource_name resource identifier defined in the code, i.e, "location"
         * @return
         */
        bool write_bucket(const char* bucket_id, const char* resource_name, bool confirm_write=false){
            return write_bucket(bucket_id, resources_[resource_name], confirm_write);
        }

        /**
         * Stream the given resource
         * @param resource resource defined in the code, i.e, thing["location"]
         * @param type STREAM_EVENT or STREAM_SAMPLE, depending if the stream was an event or a scheduled sampling
         */
        bool stream_resource(thinger_resource& resource, uint16_t stream_id){
            thinger_message request(message::type::STREAM_DATA), response(message::type::STREAM_DATA);
            resource.run_resource(request, response);
            bool result = false;
            if(request.has_field(message::stream::PAYLOAD)){
                request.set_stream_id(stream_id);
                request[message::stream::PARAMETERS] = message::stream::parameters::RESOURCE_INPUT;
                result |= send_message(request);
            }
            if(response.has_field(message::stream::PAYLOAD)){
                response.set_stream_id(stream_id);
                response[message::stream::PARAMETERS] = message::stream::parameters::RESOURCE_OUTPUT;
                result |= send_message(response);
            }
            return result;
        }

        /**
         * Stream the given resource
         * @param resource resource defined in the code, i.e, thing["location"]
         * @param type STREAM_EVENT or STREAM_SAMPLE, depending if the stream was an event or a scheduled sampling
         */
        bool stream_resource(thinger_resource& resource, pson& pson_out){
            thinger_message message(message::type::STREAM_DATA);
            message[message::common::STREAM_ID] = resource.get_stream_id();
            message[message::stream::PAYLOAD].swap(pson_out);
            return send_message(message);
        }

        /**
         * Stream the given resource
         * @param resource resource defined in the code, i.e, thing["location"]
         * @param type STREAM_EVENT or STREAM_SAMPLE, depending if the stream was an event or a scheduled sampling
         * @param buffer data
         * @param buffer size
         */
        bool stream_resource(thinger_resource& resource, const uint8_t* data, size_t size){
            thinger_message message(message::type::STREAM_DATA);
            message[message::common::STREAM_ID] = resource.get_stream_id();
            message[message::stream::PAYLOAD].set_bytes(data, size);
            return send_message(message);
        }

        /**
         * Stream the given resource
         * @param stream_id
         * @param data
         * @param size
         */
        bool stream_resource(uint16_t stream_id, const uint8_t* data, size_t size){
            thinger_message message(message::type::STREAM_DATA);
            message[message::common::STREAM_ID] = stream_id;
            message[message::stream::PAYLOAD].set_bytes(data, size);
            return send_message(message);
        }

        /**
         * Stream the given resource. There should be any process listening for such resource, i.e., over a server websocket.
         * @param resource resource defined in the code, i.e, thing["location"]
         * @return true if there was some external process listening for this resource and the resource was transmitted
         */
        bool stream(thinger_resource& resource){
            return resource.stream_enabled() && stream_resource(resource, resource.get_stream_id());
        }

        bool stop_stream(uint16_t stream_id){
            thinger_message message(message::type::STOP_STREAM);
            message[message::common::STREAM_ID] = stream_id;
            return send_message_with_ack(message);
        }

        /**
         * Stop streaming the given resource. There should be any process listening for such resource, i.e., over a server websocket.
         * @param resource resource defined in the code, i.e, thing["location"]
         * @return true if there was some external process listening for this resource and the resource was transmitted
         */
        bool stop_stream(thinger_resource& resource){
            if(resource.stream_enabled()){
                return stop_stream(resource.get_stream_id());
            }
            return false;
        }

        /**
         * Stream the given resource. There should be any process listening for such resource, i.e., over a server websocket.
         * @param resource resource defined in the code, i.e, thing["location"]
         * @param data raw buffer data
         * @param size raw buffer size
         * @return true if there was some external process listening for this resource and the resource was transmitted
         */
        bool stream(thinger_resource& resource, const uint8_t* data, size_t size){
            if(resource.stream_enabled()){
                stream_resource(resource, data, size);
                return true;
            }
            return false;
        }

        /**
         * Stream the given resource. There should be any process listening for such resource, i.e., over a server websocket.
         * @param resource resource identifier defined in the code, i.e, "location"
         * @return true if there was some external process listening for this resource and the resource was transmitted
         */
        bool stream(const char* resource){
            return stream(resources_[resource]);
        }

        /**
         * This method should be called periodically, indicating the current timestamp, and if there are bytes
         * available in the connection
         * @param current_time in milliseconds, i.e., unix epoch or millis from start.
         * @param bytes_available true or false indicating if there is input data available for reading.
         */
        void handle(unsigned long current_time, bool bytes_available)
        {
            // handle input
            if(bytes_available){
                thinger_message message(message::type::RESERVED);
                synchronized(bool result = read_message(message);)
                if(result) handle_request_received(message);
                else disconnected();
            }

            // handle keep alive (send keep alive to server to prevent disconnection)
            if(current_time-last_keep_alive>KEEP_ALIVE_MILLIS){
                if(keep_alive_response){
                    last_keep_alive = current_time;
                    keep_alive_response = false;
                    send_keep_alive();
                }else{
                    disconnected();
                }
            }

            // handle streaming resources
            streams_.handle_streams(current_time);
        }

    private:

        /**
         * Decode a message  from the current connection. It should be called when there are bytes available for reading.
         * @param message reference to the message that will be filled with the decoded information
         * @return true or false if the message passed in reference was filled with a valid message.
         */
        bool read_message(thinger_message& message){
            uint32_t type = 0;
            bool success = decoder.pb_decode_varint32(type);

            // decode message size & message itself
            if(success){
                message.set_message_type(static_cast<message::type>(type));
                uint32_t size = 0;
                success = decoder.pb_decode_varint32(size) && decoder.decode(message, size);
            }

            if(success){
                if(message.get_message_type()!=message::STREAM_DATA ||
                        (message.has_field(message::stream::PAYLOAD) &&
                            !message[message::stream::PAYLOAD].is_bytes())){
                    THINGER_LOG_TAG("MSG__IN", "%s", message.dump(true).c_str());
                }
            }else{
                THINGER_LOG_ERROR_TAG("MSG_ERR", "Cannot read input message!");
            }

            return success;
        }

        /**
         * Wait for a server response, and optionally store the response payload on the provided PSON structure
         * @param request source message that will be used forf
         * @param payload
         * @return true if the response was received and succeed (REQUEST_OK in signal flag)
         */
        bool wait_response(thinger_message& request, protoson::pson* payload = nullptr){
            uint16_t request_stream_id = request[message::common::STREAM_ID];
            do{
                // try to read an incoming message
                thinger_message response(message::type::RESERVED);
                if(!read_message(response)) return false;

                uint16_t response_stream_id = request[message::common::STREAM_ID];
                if(request_stream_id == response_stream_id){
                    if(payload != nullptr && response.has_field(message::ok::PAYLOAD)){
                        payload->swap(response[message::ok::PAYLOAD]);
                    }
                    return response.get_message_type()==message::type::OK;
                }
                handle_request_received(response);
            }while(true);
        }

        /**
         * Write a message to the socket
         * @param message
         * @return true if success
         */
        bool write_message(thinger_message& message){
            if(message.get_message_type()!=message::STREAM_DATA ||
               (message.has_field(message::stream::PAYLOAD) &&
                !message[message::stream::PAYLOAD].is_bytes())){
                THINGER_LOG_TAG("MSG_OUT", "%s", message.dump(true).c_str());
            }
            thinger_encoder sink;
            sink.encode(message);
            encoder.pb_encode_varint(message.get_message_type());
            encoder.pb_encode_varint(sink.bytes_written());
            encoder.encode(message);
            return write(nullptr, 0, true);
        }

        /**
         * Send a message
         * @param message message to be sent
         * @return true if the message was written to the socket
         */
        bool send_message(thinger_message& message){
            synchronized(bool result = write_message(message);)
            return result;
        }

        /**
         * Send a message and optionally wait for server acknowledgement
         * @param message message to be sent
         * @param wait_ack true if ack is required
         * @return true if the message was acknowledged by the server.
         */
        bool send_message_with_ack(thinger_message& message, bool wait_ack=true){
            if(wait_ack && message.get_stream_id()==0) message.set_random_stream_id();
            synchronized(bool result = write_message(message) && (!wait_ack || wait_response(message));)
            return result;
        }

        /**
         * Send a message and wait for server ack and response payload
         * @param message message to be sent
         * @param data protoson::pson structure to be filled with the response payload
         * @return true if the message was acknowledged by the server.
         */
        bool send_message(thinger_message& message, protoson::pson& data){
            if(message.get_stream_id()==0) message.set_random_stream_id();
            synchronized(bool result = write_message(message) && wait_response(message, &data););
            return result;
        }

        /**
         * Send a keep alive to the server
         * @return true if the keep alive was written to the socket
         */
        bool send_keep_alive(){
            thinger_message message(message::type::KEEP_ALIVE);
            return send_message(message);
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

        thinger_resource* get_resource(const char* request_path, pson& path_matches){
            for(auto it=resources_.begin(); it.valid(); it.next()){
                if(matches(it.item().left, request_path, path_matches)){
                    return &(it.item().right);
                }
            }
            return nullptr;
        }

        bool send_response(thinger_message& request, message::type type){
            thinger_message response(request.get_stream_id(), type);
            return send_message(response);
        }

        void handle_resource_request(thinger_resource& resource, thinger_message& request){

            switch(request.get_message_type()){

                // default action over the stream (run the resource)
                case message::type::RUN:
                {
                    thinger_message response(request.get_stream_id(), message::type::OK);
                    resource.run_resource(request, response);
                    send_message(response);
                    /**
                     * If a run request is received with an input, and there is a current ongoing stream over
                     * this resource, then, stream the resource to update any subscribers with the new input.
                     */
                    if(resource.stream_enabled() && resource.stream_echo()){
                        stream(resource);
                    }

                }
                    break;

                // flag for starting a resource stream
                case message::type::START_STREAM:
                {
                    // start streaming over resource
                    streams_.start(request.get_stream_id(), resource, request[0], request[message::start_stream::PARAMETERS]);

                    // send ok and check if send current state
                    if(resource.stream_enabled()){
                        stream_resource(resource, request.get_stream_id());
                    }
                }
                    break;

                // flag for stopping a resource stream
                case message::type::STOP_STREAM:{
                    // stop streaming
                    streams_.stop(request.get_stream_id(), resource, request[message::start_stream::PARAMETERS]);
                    // send ok
                    send_response(request, message::type::OK);
                }
                    break;

                // flag for describing the resource API
                case message::type::DESCRIBE:
                {
                    thinger_message response(request.get_stream_id(), message::type::OK);
                    resource.describe(response);
                    send_message(response);
                }
                    break;

                // stream input
                case message::type::STREAM_DATA: {
                    thinger_message response(request.get_stream_id(), message::type::STREAM_DATA);
                    resource.run_resource(request, response);
                    if(resource.stream_echo()){
                        stream_resource(resource, request.get_stream_id());
                    }
                }
                    break;
                default:
                    send_response(request, message::type::ERROR);
                    break;
            }
        }

        /**
         * Handle an incoming request from the server
         * @param request the message sent by the server
         */
        void handle_request_received(thinger_message& request)
        {

            thinger_resource * thing_resource = nullptr;

            switch(request.get_message_type()){
                case message::RUN:
                    if(request.has_field(message::run::RESOURCE)){
                        thing_resource = get_resource(request[message::run::RESOURCE], request[0]);
                    }
                    break;
                case message::DESCRIBE:
                    if(request.has_field(message::describe::RESOURCE)){
                        thing_resource = get_resource(request[message::describe::RESOURCE], request[0]);
                    }
                    break;
                case message::START_STREAM:
                    if(request.has_field(message::start_stream::RESOURCE)) {
                        thing_resource = get_resource(request[message::start_stream::RESOURCE], request[0]);
                    }
                    break;
                case message::STREAM_DATA:
                case message::STOP_STREAM:
                    if(request.has_field(message::common::STREAM_ID)){
                        thing_resource = streams_.find(request[message::common::STREAM_ID]);
                    }
                    break;
                case message::KEEP_ALIVE:
                    keep_alive_response = true;
                    notify_state(THINGER_KEEP_ALIVE_RECEIVED);
                    return;
                default:
                    break;
            }

            // it is a request associated to a valid resource
            if(thing_resource!=nullptr){
                handle_resource_request(*thing_resource, request);

            // request
            }else if(request.get_message_type()==message::type::DESCRIBE &&
                        !request.has_field(message::describe::RESOURCE)){

                thinger_message response(request.get_stream_id(),message::OK);
                for(auto it = resources_.begin(); it.valid(); it.next()){
                    it.item().right.fill_api(response[message::ok::PAYLOAD][it.item().left]);
                }
                send_message(response);

            // send error
            }else{
                send_response(request, message::type::ERROR);
            }
        }
    };
}

#endif