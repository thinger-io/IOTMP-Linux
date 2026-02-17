/**
 * Cliente IOTMP con coroutines C++20
 */

#include <iostream>
#include <csignal>
#include <filesystem>
#include <boost/program_options.hpp>

#include <spdlog/spdlog.h>
#include <thinger/asio/workers.hpp>
#include "thinger/iotmp/client.hpp"
#include "thinger/iotmp/extensions/fs/filesystem.hpp"
#include "thinger/iotmp/extensions/terminal/terminal.hpp"
#include "thinger/iotmp/extensions/proxy/proxy.hpp"
#include "thinger/iotmp/extensions/version/version.hpp"

using namespace thinger::iotmp;
namespace po = boost::program_options;

int main(int argc, char* argv[]) {
    // Parsear argumentos
    std::string username, device, password, hostname, transport;
    int verbosity = 0;

    po::options_description desc("IOTMP Async Client");
    desc.add_options()
        ("help", "show help")
        ("username,u", po::value<std::string>(&username), "username")
        ("device,d", po::value<std::string>(&device), "device identifier")
        ("password,p", po::value<std::string>(&password), "device password")
        ("host,h", po::value<std::string>(&hostname)->default_value("iot.thinger.io"), "server hostname")
        ("transport,t", po::value<std::string>(&transport)->default_value("ssl"), "transport type: ssl, ws, tcp")
        ("verbosity,v", po::value<int>(&verbosity)->default_value(0), "verbosity level");

    po::variables_map vm;

    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::store(po::parse_environment(desc, "THINGER_"), vm);
        po::notify(vm);
    } catch(const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    if(vm.count("help")) {
        std::cout << desc << "\n";
        return 0;
    }

    if(username.empty() || device.empty() || password.empty()) {
        std::cerr << "Error: username, device and password are required\n\n";
        std::cout << desc << "\n";
        return 1;
    }

    // Inicializar logger
    thinger::logging::enable();
    if(verbosity >= 2) {
        thinger::logging::set_log_level(spdlog::level::debug);
    } else if(verbosity >= 1) {
        thinger::logging::set_log_level(spdlog::level::info);
    }

    // Determinar tipo de transporte
    transport_type trans = transport_type::SSL;

    if(transport == "ws" || transport == "websocket") {
        trans = transport_type::WEBSOCKET;
    } else if(transport == "tcp") {
        trans = transport_type::TCP;
    } else if(transport != "ssl") {
        std::cerr << "Error: invalid transport '" << transport << "'. Use: ssl, ws, tcp\n";
        return 1;
    }

    // Crear cliente
    client iotmp_client;
    iotmp_client.set_credentials(username, device, password);
    iotmp_client.set_host(hostname);
    iotmp_client.set_transport(trans);

    // Definir recursos - prueba de diferentes tipos
    iotmp_client["input_bool"] = [](input& in) {
        bool value = in["value"];
        std::cout << "bool: " << (value ? "true" : "false") << "\n";
    };

    iotmp_client["input_int"] = [](input& in) {
        int value = in["value"];
        std::cout << "int: " << value << "\n";
    };

    iotmp_client["input_float"] = [](input& in) {
        float value = in["value"];
        std::cout << "float: " << value << "\n";
    };

    iotmp_client["input_double"] = [](input& in) {
        double value = in["value"];
        std::cout << "double: " << value << "\n";
    };

    iotmp_client["input_string"] = [](input& in) {
        std::string value = in["value"];
        std::cout << "string: " << value << "\n";
    };

    iotmp_client["input_multiple"] = [](input& in) {
        bool b = in["bool_val"];
        int i = in["int_val"];
        float f = in["float_val"];
        std::string s = in["string_val"];
        std::cout << "multiple: " << b << ", " << i << ", " << f << ", " << s << "\n";
    };

    // Prueba de input directo (sin campos)
    iotmp_client["input_direct_bool"] = [](input& in) {
        bool value = in;
        std::cout << "direct bool: " << (value ? "true" : "false") << "\n";
    };

    iotmp_client["input_direct_int"] = [](input& in) {
        int value = in;
        std::cout << "direct int: " << value << "\n";
    };

    iotmp_client["input_direct_float"] = [](input& in) {
        float value = in;
        std::cout << "direct float: " << value << "\n";
    };

    iotmp_client["input_direct_string"] = [](input& in) {
        std::string value = in;
        std::cout << "direct string: " << value << "\n";
    };

    iotmp_client["temperature"] = [](output& out) {
        static float temp = 20.0f;
        temp += 0.1f;
        out["celsius"] = temp;
        out["fahrenheit"] = temp * 9.0f / 5.0f + 32.0f;
    };

    iotmp_client["sum"] = [](input& in, output& out) {
        int a = in["a"];
        int b = in["b"];
        out["result"] = a + b;
    };

    // Inicializar extensiones
    terminal shell(iotmp_client);
    std::filesystem::path fs_base_path = "/Users/alvarolb/Desktop";
    filesystem fs(iotmp_client, fs_base_path);
    proxy tcp_proxy(iotmp_client);
    version ver(iotmp_client);

    // Iniciar cliente (arranca workers automáticamente)
    std::cout << "Starting async client...\n";
    iotmp_client.start();

    // Esperar señales de cierre (Ctrl+C o SIGTERM)
    thinger::asio::get_workers().wait();

    std::cout << "Client stopped.\n";
    return 0;
}