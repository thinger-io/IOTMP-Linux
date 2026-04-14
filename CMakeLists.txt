cmake_minimum_required(VERSION 3.11)
project(thinger_iotmp)

if(CMAKE_VERSION VERSION_GREATER "3.24")
    cmake_policy(SET CMP0135 NEW)
endif()

# set package version
SET(VERSION_MAJOR "1" CACHE STRING "Major version number")
set(VERSION_MINOR "3" CACHE STRING "Minor version number")
set(VERSION_PATCH "0" CACHE STRING "Patch version number")

# add version for source code
add_definitions(-DVERSION_MAJOR=${VERSION_MAJOR})
add_definitions(-DVERSION_MINOR=${VERSION_MINOR})
add_definitions(-DVERSION_PATCH=${VERSION_PATCH})

# Check if project is being built standalone or as subproject
# PROJECT_IS_TOP_LEVEL is available in CMake 3.21+
if(CMAKE_VERSION VERSION_LESS 3.21)
  if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(PROJECT_IS_TOP_LEVEL ON)
  else()
    set(PROJECT_IS_TOP_LEVEL OFF)
  endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# configure Thinger.io parameters
OPTION(STATIC "Enable static linking" OFF)

# OpenSSL
if(STATIC)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
endif()
find_package(OpenSSL REQUIRED)
if(OPENSSL_FOUND)
    message(STATUS "OpenSSL Version: ${OPENSSL_VERSION}")
endif()

# Boost
set(Boost_USE_MULTITHREADED ON)
if(STATIC)
    set(Boost_USE_STATIC_LIBS ON)
else()
    set(Boost_USE_STATIC_LIBS OFF)
endif()
find_package(Boost REQUIRED COMPONENTS program_options date_time process iostreams)

add_definitions( -DTHINGER_ENABLE_STREAM_LISTENER)
add_definitions( -DTHINGER_SERVER="iot.thinger.io")
add_definitions( -DTHINGER_KEEP_ALIVE_SECONDS=60)
add_definitions( -DTHINGER_RECONNECT_SECONDS=15)
add_definitions( -DTHINGER_CONNECT_TIMEOUT=15)
if(OPENSSL_FOUND)
    add_definitions(-DTHINGER_PORT=25206)
    add_definitions(-DTHINGER_OPEN_SSL=1)
else()
    add_definitions(-DTHINGER_PORT=25204)
endif()

include(FetchContent)

# spdlog
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.15.3
)
FetchContent_MakeAvailable(spdlog)
add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

# json
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
)
FetchContent_MakeAvailable(json)

# httplib
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.16.0
)
FetchContent_MakeAvailable(httplib)

# thinger-http
set(THINGER_HTTP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(THINGER_HTTP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(THINGER_HTTP_ENABLE_LOGGING ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  thinger_http
  GIT_REPOSITORY https://github.com/thinger-io/thinger-http.git
  GIT_TAG main
)
FetchContent_MakeAvailable(thinger_http)

if (NOT PROJECT_IS_TOP_LEVEL)

  # Source files
  file(GLOB_RECURSE SOURCE_FILES
    "src/thinger/*.hpp"
    "src/thinger/*.h"
    "src/thinger/*.cpp"
    "src/thinger/*.cc"
    "src/thinger/*.c"
  )

  # Create library (type determined by BUILD_SHARED_LIBS or explicit SHARED/STATIC)
  add_library(${PROJECT_NAME} ${SOURCE_FILES})
  
  # Set default to static if not specified
  if(NOT DEFINED BUILD_SHARED_LIBS)
    set_property(TARGET ${PROJECT_NAME} PROPERTY TYPE STATIC_LIBRARY)
    message(STATUS "Building ${PROJECT_NAME} as static library")
  else()
    message(STATUS "Building ${PROJECT_NAME} as shared library")
  endif()

  target_compile_definitions(${PROJECT_NAME} PUBLIC
      THINGER_SERVER="iot.thinger.io"
      THINGER_ENABLE_STREAM_LISTENER
  )

  add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

  # Link dependencies with modern CMake
  target_link_libraries(${PROJECT_NAME} PUBLIC
      thinger::http
      spdlog::spdlog
      nlohmann_json::nlohmann_json
      httplib::httplib
      OpenSSL::SSL
      OpenSSL::Crypto
      Boost::program_options
      Boost::date_time
      Boost::process
      $<$<PLATFORM_ID:Linux>:util>
  )

  target_include_directories(${PROJECT_NAME} PUBLIC 
      "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/src>"
      "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
  )

else()

  message(STATUS "Generating binary of ${PROJECT_NAME}")

  # Platform-specific configurations
  if(NOT APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-psabi")
  endif()

  # Static linking configuration
  if(STATIC)
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

    if(APPLE)
      # macOS doesn't support full static linking, but we can statically link libstdc++
      SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -pthread")
    else()
      # Linux: generate a fully static binary
      SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static -pthread")
    endif()
  endif()

  # Core and extension files
  file(GLOB_RECURSE SOURCE_FILES
      "src/thinger/iotmp/core/*.cpp"
      "src/thinger/iotmp/extensions/*.cpp"
  )

  add_executable(${PROJECT_NAME}
      src/main.cpp
      ${SOURCE_FILES}
  )

  target_link_libraries(${PROJECT_NAME} PRIVATE
      thinger::http
      spdlog::spdlog
      nlohmann_json::nlohmann_json
      OpenSSL::SSL
      OpenSSL::Crypto
      Boost::program_options
      Boost::process
      $<$<PLATFORM_ID:Linux>:util>
  )

  target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

endif()