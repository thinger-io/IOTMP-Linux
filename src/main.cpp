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
    std::string username, device, password, hostname, transport, fs_path;
    int verbosity = 0;

    po::options_description desc("IOTMP Async Client");
    desc.add_options()
        ("help", "show help")
        ("username,u", po::value<std::string>(&username), "username")
        ("device,d", po::value<std::string>(&device), "device identifier")
        ("password,p", po::value<std::string>(&password), "device password")
        ("host,h", po::value<std::string>(&hostname)->default_value("iot.thinger.io"), "server hostname")
        ("transport,t", po::value<std::string>(&transport)->default_value("ssl"), "transport type: ssl, ws, tcp")
        ("fs-path,f", po::value<std::string>(&fs_path), "filesystem base path")
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

    // Inicializar extensiones
    terminal shell(iotmp_client);
    filesystem fs(iotmp_client, fs_path.empty() ? std::filesystem::current_path() : std::filesystem::path(fs_path));
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