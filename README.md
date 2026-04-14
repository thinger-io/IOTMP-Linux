# Thinger.io IOTMP Linux Client

[![Build](https://github.com/thinger-io/IOTMP-Linux/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/thinger-io/IOTMP-Linux/actions/workflows/build.yml)

A modern C++20 client library for connecting Linux devices to the [Thinger.io](https://thinger.io) IoT platform using [IOTMP (IoT Message Protocol)](https://iotmp.io). Built on Boost.Asio coroutines for fully asynchronous, non-blocking communication.

## Features

- **C++20 Coroutines** - Async I/O powered by Boost.Asio coroutines (`co_await`)
- **Multiple Transports** - SSL/TLS, raw TCP, and WebSocket connections
- **Intuitive Resource API** - Define device resources (sensors, actuators, RPCs) with simple lambdas
- **Automatic Reconnection** - Configurable reconnect with exponential backoff
- **Cloud Integration** - Device properties, data buckets, endpoints, device-to-device calls, MQTT topics
- **Server Event Subscriptions** - React to property changes, MQTT messages, and custom events
- **Built-in Extensions** - Filesystem, terminal, TCP proxy, OTA updates, command execution, and version reporting
- **Parameterized Resources** - Wildcard path matching (`:param` and `*param` syntax)
- **Streaming** - Real-time data streaming with configurable intervals

## Requirements

### System Dependencies

These must be installed on your system before building:

- C++20 compatible compiler (GCC 11+, Clang 14+, or equivalent)
- CMake 3.11+
- [Boost](https://www.boost.org/) (system, program_options, date_time, process, iostreams)
- [OpenSSL](https://www.openssl.org/)

**Ubuntu / Debian:**

```bash
sudo apt install build-essential cmake libboost-all-dev libssl-dev
```

**Fedora / RHEL:**

```bash
sudo dnf install gcc-c++ cmake boost-devel openssl-devel
```

**macOS (Homebrew):**

```bash
brew install cmake boost openssl
```

### Automatically Fetched Dependencies

The following are downloaded and built automatically by CMake via `FetchContent` — no manual installation needed:

- [spdlog](https://github.com/gabime/spdlog) - Logging
- [nlohmann/json](https://github.com/nlohmann/json) - JSON handling
- [cpp-httplib](https://github.com/yhirose/cpp-httplib) - HTTP utilities
- [thinger-http](https://github.com/thinger-io/thinger-http) - Async socket and HTTP client

## CMake Integration

### Using FetchContent (recommended)

```cmake
include(FetchContent)

FetchContent_Declare(
  thinger_iotmp
  GIT_REPOSITORY https://github.com/thinger-io/IOTMP-Linux.git
  GIT_TAG master
)
FetchContent_MakeAvailable(thinger_iotmp)

target_link_libraries(your_target PRIVATE thinger_iotmp::thinger_iotmp)
```

### Using add_subdirectory

```cmake
add_subdirectory(path/to/IOTMP-Linux)
target_link_libraries(your_target PRIVATE thinger_iotmp::thinger_iotmp)
```

When used as a subdirectory, the project builds as a static library by default. Set `BUILD_SHARED_LIBS=ON` to build as a shared library.

## Quick Start

```cpp
#include "thinger/iotmp/client.hpp"

using namespace thinger::iotmp;

int main() {
    client device;
    device.set_credentials("USERNAME", "DEVICE_ID", "DEVICE_CREDENTIAL");

    // Define an output resource (e.g., a sensor)
    device["temperature"] = [](output& out) {
        out["celsius"] = 22.5f;
    };

    // Define an input resource (e.g., an actuator)
    device["led"] = [](input& in) {
        bool on = in["state"];
        // control your LED...
    };

    // Start the client (connects, authenticates, and runs the event loop)
    device.start();

    // Wait for shutdown signal (Ctrl+C)
    thinger::asio::get_workers().wait();
    return 0;
}
```

## Resource API

Resources are the core abstraction for exposing device functionality. Define them using the `operator[]` on the client with a lambda.

### Output Resources (Sensors)

Resources that produce data when queried. Ideal for sensors and readings.

```cpp
device["temperature"] = [](output& out) {
    out["celsius"] = read_temperature();
    out["fahrenheit"] = read_temperature() * 9.0f / 5.0f + 32.0f;
};
```

### Input Resources (Actuators)

Resources that receive data from the cloud. Ideal for actuators and configuration.

```cpp
// Structured input with named fields
device["relay"] = [](input& in) {
    int channel = in["channel"];
    bool state = in["state"];
    set_relay(channel, state);
};

// Direct value input (no field names)
device["brightness"] = [](input& in) {
    int level = in;
    set_brightness(level);
};
```

Supported types for automatic conversion: `bool`, `int`, `float`, `double`, `std::string`.

### Input/Output Resources (RPC)

Resources that receive input and produce output. Ideal for remote procedure calls.

```cpp
device["sum"] = [](input& in, output& out) {
    int a = in["a"];
    int b = in["b"];
    out["result"] = a + b;
};
```

### Run Resources (Actions)

Resources that perform an action without input or output.

```cpp
device["reboot"] = []() {
    system("reboot");
};
```

### Parameterized Resources (Wildcards)

Use `:param` to capture path segments, or `*param` to capture the rest of the path.

```cpp
// Capture a single segment: /gpio/4, /gpio/17, etc.
device["gpio/:pin"] = [](input& in) {
    std::string pin = in.path("pin");
    bool state = in["state"];
    set_gpio(std::stoi(pin), state);
};

// Capture the remaining path: /files/path/to/file.txt
device["files/*path"] = [](input& in, output& out) {
    std::string file_path = in.path("path");
    out["content"] = read_file(file_path);
};
```

## Cloud Features

The client provides coroutine-based methods for interacting with the Thinger.io cloud. These methods are designed to be called from within an asynchronous context (e.g., from a state callback after `STREAMS_READY`).

### Device Properties

Read and write persistent key-value properties stored on the server.

```cpp
// Read a property
json_t data;
co_await device.get_property("config", data);

// Write a property
co_await device.set_property("config", {{"interval", 60}});
```

### Data Buckets

Write time-series data to a Thinger.io data bucket.

```cpp
co_await device.write_bucket("environment", {
    {"temperature", 22.5},
    {"humidity", 65.0}
});
```

### Endpoints

Trigger server-side endpoints (email, HTTP, MQTT, etc.).

```cpp
// Trigger an endpoint without data
co_await device.call_endpoint("alert_email");

// Trigger with data payload
co_await device.call_endpoint("alert_email", {{"message", "Temperature too high!"}});
```

### Device-to-Device Calls

Invoke resources on other devices connected to the same account.

```cpp
// Call without data
co_await device.call_device("other_device", "reboot");

// Call with data
co_await device.call_device("other_device", "relay", {{"state", true}});
```

### MQTT Topics

Publish and subscribe to MQTT topics through the platform.

```cpp
// Publish to a topic (produces a stream that sends data)
auto& pub = device.topic_publish_stream("devices/status", /*qos=*/0, /*retained=*/false);
pub = [](output& out) {
    out["online"] = true;
};

// Subscribe to a topic (receives data from the topic)
auto& sub = device.topic_subscribe_stream("commands/#");
sub = [](input& in) {
    std::string payload = in["payload"];
    // handle incoming MQTT message...
};
```

### Property Change Subscriptions

React to property changes pushed from the server.

```cpp
auto& prop = device.property_stream("config", /*fetch_at_subscription=*/true);
prop = [](input& in) {
    int interval = in["interval"];
    // apply new configuration...
};
```

## Extensions

Extensions add higher-level functionality to the client by registering internal resources. Instantiate them by passing the client reference.

### Terminal

Provides an interactive shell session accessible from the Thinger.io console. Streams bidirectional data between the platform and a PTY-based shell process on the device.

```cpp
#include "thinger/iotmp/extensions/terminal/terminal.hpp"

terminal shell(device);
```

### Filesystem

Enables remote file management: listing, upload, download, move, and delete. Supports chunked streaming with flow control for large file transfers.

```cpp
#include "thinger/iotmp/extensions/fs/filesystem.hpp"

filesystem fs(device, "/path/to/base/directory");
```

The base path restricts all file operations to that directory and its children.

### Proxy

TCP tunneling through the IOTMP connection. Allows the platform to establish proxied TCP connections to local services on the device (e.g., SSH, HTTP servers).

```cpp
#include "thinger/iotmp/extensions/proxy/proxy.hpp"

proxy tcp_proxy(device);
```

### OTA (Over-the-Air Updates)

Receives firmware update binaries streamed from the platform.

```cpp
#include "thinger/iotmp/extensions/ota/ota.hpp"

ota firmware(device);
```

### Cmd (Command Execution)

Exposes a resource for executing shell commands and returning stdout/stderr.

```cpp
#include "thinger/iotmp/extensions/cmd/cmd.hpp"

cmd command(device);
```

### Version

Reports device version information (major, minor, patch) to the platform.

```cpp
#include "thinger/iotmp/extensions/version/version.hpp"

version ver(device);
```

Version numbers are set via CMake definitions: `VERSION_MAJOR`, `VERSION_MINOR`, `VERSION_PATCH`.

## Configuration

The following compile definitions can be set in CMake to customize client behavior:

| Definition | Default | Description |
|---|---|---|
| `THINGER_SERVER` | `"iot.thinger.io"` | Server hostname |
| `THINGER_PORT` | `25206` (SSL) / `25204` (TCP) | Server port |
| `THINGER_OPEN_SSL` | `1` | Enable SSL/TLS transport |
| `THINGER_KEEP_ALIVE_SECONDS` | `60` | Keep-alive interval in seconds |
| `THINGER_RECONNECT_SECONDS` | `15` | Delay before reconnecting |
| `THINGER_CONNECT_TIMEOUT` | `15` | Connection timeout in seconds |
| `THINGER_ENABLE_STREAM_LISTENER` | defined | Enable stream listener callbacks |

Example:

```cmake
target_compile_definitions(your_target PRIVATE
    THINGER_SERVER="custom.server.com"
    THINGER_PORT=25206
    THINGER_KEEP_ALIVE_SECONDS=30
)
```

## Building Standalone

When built as a top-level project, the CMake configuration produces an executable with CLI argument support.

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### Static Linking

```bash
cmake .. -DSTATIC=ON
cmake --build .
```

### Usage

```
./thinger_iotmp -u USERNAME -d DEVICE_ID -p DEVICE_PASSWORD [options]

Options:
  --help                Show help
  -u, --username        Thinger.io username
  -d, --device          Device identifier
  -p, --password        Device credential
  -h, --host            Server hostname (default: iot.thinger.io)
  -t, --transport       Transport type: ssl, ws, tcp (default: ssl)
  -v, --verbosity       Verbosity level: 0=warn, 1=info, 2=debug
```

Environment variables with the `THINGER_` prefix are also supported (e.g., `THINGER_USERNAME`, `THINGER_DEVICE`, `THINGER_PASSWORD`).

## License

MIT License. See [LICENSE](LICENSE) for details.
