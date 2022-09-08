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

#define DEFAULT_VERBOSITY_LEVEL     0
#define DEFAULT_HOSTNAME            "backend.thinger.io"
#define DEFAULT_USERNAME            ""
#define DEFAULT_DEVICE              ""
#define DEFAULT_CREDENTIAL          ""

int main(int argc, char *argv[])
{
    // initialize
    std::string username;
    std::string device;
    std::string credentials;
    std::string hostname;

    namespace po = boost::program_options;
    po::options_description desc("options_description [options]");
    desc.add_options()
        ("help", "show this help")
        ("verbosity,v",
         po::value<loguru::Verbosity>(&loguru::g_stderr_verbosity)->default_value(DEFAULT_VERBOSITY_LEVEL),
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
         "target hostname");

    po::parse_command_line(argc, argv, desc);

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 1;
    }

    // initialize logging library
    loguru::init(argc, argv, {});

    // check credentials are provided
    if(username.empty() || device.empty() || credentials.empty()){
        LOG_F(ERROR, "username, device, or credentials not provided");
        return 1;
    }

    // initialize device with credentials
    thinger_device thing(username, device, credentials);

    // set target hostname
    thing.set_host(hostname.c_str());

    // initialize
    thinger_shell shell(thing);

    thinger_proxy proxy(thing);

    // define thing resources here. i.e, this is a sum example
    thing["sum"] = [](pson& in, pson& out){
        out["result"] = (int) in["value1"] + (int) in["value2"];
    };

    // start thinger.io client
    thing.start();

    return 0;
}