cmake_minimum_required(VERSION 3.11...3.21)
project(thinger)

cmake_policy(SET CMP0135 NEW)

# set package version
set(VERSION_MAJOR 0)
set(VERSION_MINOR 0)
set(VERSION_PATCH 1)

# add version for source code
add_definitions(-DVERSION_MAJOR=${VERSION_MAJOR})
add_definitions(-DVERSION_MINOR=${VERSION_MINOR})
add_definitions(-DVERSION_PATCH=${VERSION_PATCH})

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules/")

# Check if thinger is being built by subproject of standalone
#set(THINGER_MASTER_PROJECT OFF)
if(NOT DEFINED THINGER_MASTER_PROJECT)
  if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(THINGER_MASTER_PROJECT ON)
  else()
    set(THINGER_MASTER_PROJECT OFF)
  endif()
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules/")

# check c++17 or c++20 support
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++17" COMPILER_SUPPORTS_CXX17)
CHECK_CXX_COMPILER_FLAG("-std=c++20" COMPILER_SUPPORTS_CXX20)
if(COMPILER_SUPPORTS_CXX20)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")
elseif(COMPILER_SUPPORTS_CXX17)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
else()
  message(STATUS "The compiler ${CMAKE_CXX_COMPILER} has no C++17 support. Please use a different C++ compiler.")
endif()

# include src/util ash include path
include_directories("src/loguru")

# configure Thinger.io parameters
OPTION(ASIO_CLIENT "Enable ASIO CLIENT" ON)
OPTION(STATIC "Enable static linking" OFF)

if (APPLE)
    # This is a bug in CMake that causes it to prefer the system version over
    # the one in the specified ROOT folder.
    set(OPENSSL_ROOT_DIR ${OPENSSL_ROOT_DIR} /usr/local/Cellar/openssl@1.1/1.1.1s/)
    set(OPENSSL_CRYPTO_LIBRARY ${OPENSSL_ROOT_DIR}/lib/libcrypto.dylib CACHE FILEPATH "" FORCE)
    set(OPENSSL_SSL_LIBRARY ${OPENSSL_ROOT_DIR}/lib/libssl.dylib CACHE FILEPATH "" FORCE)
endif()

find_package(OpenSSL REQUIRED)
if(OPENSSL_FOUND)
    include_directories(${OPENSSL_INCLUDE_DIR})
    list(APPEND ADDITIONAL_LIBS OpenSSL::SSL)
    list(APPEND ADDITIONAL_LIBS OpenSSL::Crypto)
    if(STATIC)
      # OpenSSL
      set(OPENSSL_USE_STATIC_LIBS TRUE)
    endif()
    message(STATUS "OpenSSL Version: ${OPENSSL_VERSION} ${OPENSSL_INCLUDE_DIR} ${OPENSSL_SSL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARIES}")
endif()

# Boost Libraries
set(Boost_USE_MULTITHREADED      ON)
if(APPLE)
    set(Boost_USE_STATIC_LIBS        OFF)
else()
    set(Boost_USE_STATIC_LIBS        ON)
endif()

find_package(Boost REQUIRED COMPONENTS system thread regex program_options date_time filesystem)
if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    list(APPEND ADDITIONAL_LIBS ${Boost_LIBRARIES})
endif()

# required for GCC < 9.0
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    list(APPEND ADDITIONAL_LIBS "stdc++fs")
endif()

# Add 'util' lib for supporting forkpty
list(APPEND ADDITIONAL_LIBS util)

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

# fmt
FetchContent_Declare(fmt URL https://github.com/fmtlib/fmt/releases/download/9.1.0/fmt-9.1.0.zip)
FetchContent_MakeAvailable(fmt)
list(APPEND ADDITIONAL_LIBS fmt::fmt)

# spdlog
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.11.0
)
FetchContent_MakeAvailable(spdlog)
add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
list(APPEND ADDITIONAL_LIBS spdlog::spdlog)

if (NOT THINGER_MASTER_PROJECT)

  message(STATUS "Generating install of ${PROJECT_NAME}")

  file(GLOB_RECURSE SOURCE_FILES
    "src/thinger/*.hpp"
    "src/thinger/*.h"
    "src/thinger/*.cpp"
    "src/thinger/*.cc"
    "src/thinger/*.c"
    "src/loguru/*.hpp"
    "src/loguru/*.h"
    "src/loguru/*.cpp"
    "src/loguru/*.cc"
    "src/loguru/*.c"
  )

  add_library(${PROJECT_NAME} STATIC ${SOURCE_FILES})

  target_compile_definitions(${PROJECT_NAME} PUBLIC -DTHINGER_SERVER="iot.thinger.io")
  target_compile_definitions(${PROJECT_NAME} PUBLIC -DTHINGER_ENABLE_STREAM_LISTENER)

  add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

  # httplib
  if( TARGET httplib )
    if ( TARGET nlohmann_json )
      #target_link_libraries(${PROJECT} PUBLIC httplib)
      message(STATUS "Adding json and httplib to thinger library")
      list(APPEND ADDITIONAL_LIBS nlohmann_json::nlohmann_json httplib::httplib)
      endif()
  endif()
  target_link_libraries(thinger ${ADDITIONAL_LIBS})

  set(THINGER_INCLUDES_LEVEL "")
  target_include_directories(${PROJECT_NAME} ${THINGER_INCLUDES_LEVEL} PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/src>"
                                      "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

  export(TARGETS ${PROJECT_NAME} FILE thingerConfigTargets.cmake)
  export(PACKAGE ${PROJECT_NAME})

else()

  message(STATUS "Generating binary of ${PROJECT_NAME}")

  file(GLOB_RECURSE SOURCE_FILES
    "src/*.hpp"
    "src/*.h"
    "src/*.cpp"
    "src/*.cc"
    "src/*.c"
  )

  if(STATIC)
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

    # generate a static binary
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static -pthread")
  endif()

  if(APPLE)

  else()
    # generate a static binary
    #SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static -pthread")

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-psabi")

    # strip binary
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -s")
  endif()

  add_executable(${PROJECT_NAME} ${SOURCE_FILES})

  if(APPLE)
    # use this options to test with sanitizer
    add_definitions(-DBOOST_LOG_DYN_LINK=1)
    list(APPEND TEST_OPTIONS -fsanitize=address -fno-omit-frame-pointer -fsanitize-recover=address -g)
    target_compile_options(thinger PRIVATE "$<$<CONFIG:DEBUG>:${TEST_OPTIONS}>")
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
  endif()

  target_link_libraries(thinger ${ADDITIONAL_LIBS})
endif()
