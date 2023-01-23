// The MIT License (MIT)
//
// Copyright (c) 2022 THINGER.IO
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

#include "thinger/thinger.h"
#include <boost/program_options.hpp>
#include <filesystem>

#define DEFAULT_VERBOSITY_LEVEL     0
#define DEFAULT_TRANSPORT           ""
#define DEFAULT_HOSTNAME            "iot.thinger.io"
#define DEFAULT_USERNAME            ""
#define DEFAULT_DEVICE              ""
#define DEFAULT_CREDENTIAL          ""

int main(int argc, char *argv[])
{
    // initialize
    int verbosity_level;
    std::string username;
    std::string device;
    std::string credentials;
    std::string hostname;
    std::string transport;

    namespace po = boost::program_options;

    po::options_description desc("options_description [options]");
    desc.add_options()
        ("help", "show this help")
        ("verbosity,v",
         po::value<int>(&verbosity_level)->default_value(DEFAULT_VERBOSITY_LEVEL),
         "set verbosity level")
        ("username,u",
         po::value<std::string>(&username)->default_value(DEFAULT_USERNAME),
         "username")
        ("device,d",
         po::value<std::string>(&device)->default_value(DEFAULT_DEVICE),
         "device identifier")
        ("password,p",
         po::value<std::string>(&credentials)->default_value(DEFAULT_CREDENTIAL),
         "device credential")
        ("host,h",
         po::value<std::string>(&hostname)->default_value(DEFAULT_HOSTNAME),
         "target hostname")
        ("transport,t",
         po::value<std::string>(&transport)->default_value(DEFAULT_TRANSPORT),
         "connection transport, i.e., 'websocket'");

    // initialize default values and description
    po::parse_command_line(argc, argv, desc);

    // create a map for storing parameters
    po::variables_map vm;

    // read parameters from command line
    po::store(po::parse_command_line(argc, argv, desc), vm);

    // read parameters from environment variables
    po::store(po::parse_environment(desc, "THINGER_"), vm);

    // load them also from config file in $HOME/.thinger/iotmp.cfg
    try {
        const char* home = getenv("HOME");
        if(home != nullptr){
            std::filesystem::path config_file = std::filesystem::path{home} / ".thinger" / "iotmp.cfg";
            if(exists(config_file)){
                po::store(po::parse_config_file<char>(config_file.string().c_str(), desc), vm);
            }
        }else{
            LOG_WARNING("cannot read HOME environment variable");
        }
    } catch (const po::reading_file& e) {
        LOG_ERROR("error while loading config file: %s", e.what());
    }

    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 1;
    }

    // initialize logging library
#ifdef THINGER_LOG_LOGURU
    loguru::init(argc, argv, {});
#endif

    // check credentials are provided
    if(username.empty() || device.empty() || credentials.empty()){
        LOG_ERROR("username, device, or credentials not provided");
        return 1;
    }

    // check credentials are provided
    if(!transport.empty() && transport!="websocket"){
        LOG_ERROR("invalid transport protocol provided");
        return 1;
    }

    // run asio workers
    thinger::asio::workers.start();

    // create an iotmp client
    iotmp::client client{transport};

    // set client credentials
    client.set_credentials(username, device, credentials);

    // set target hostname
    client.set_host(hostname.c_str());

    // initialize cmd extension
    iotmp::cmd cmd(client);

    // initialize terminal extension
    iotmp::terminal shell(client);

    // initialize proxy extension
    iotmp::proxy proxy(client);

    // initialize client version extension
    iotmp::version version(client);

    // example state listener
    client.set_state_listener([&](iotmp::THINGER_STATE state){
        if(state==iotmp::THINGER_AUTHENTICATED){
            unsigned connections = 0;
            pson data;
            if(client.get_property("connections", data)){
                connections = data;
                connections++;
            }
            data = connections;
            if(client.set_property("connections", data, true)){
                THINGER_LOG("connections updated to: %d", connections);
            }
        }
    });

    // start client
    client.start();

    // wait for asio workers to complete (receive a signal)
    thinger::asio::workers.wait();

    return 0;
}