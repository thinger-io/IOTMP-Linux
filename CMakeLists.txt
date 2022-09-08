cmake_minimum_required(VERSION 2.8.9)
project(thinger_iotmp_linux_client)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules/")

# check c++17 support
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++17" COMPILER_SUPPORTS_CXX17)
if(COMPILER_SUPPORTS_CXX17)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
else()
    message(STATUS "The compiler ${CMAKE_CXX_COMPILER} has no C++17support. Please use a different C++ compiler.")
endif()

# include src/util ash include path
include_directories(src/util)

# configure Thinger.io parameters
OPTION(ASIO_CLIENT "Enable ASIO CLIENT" ON)
OPTION(ENABLE_LOGGING "Enable logging" ON)

# OpenSSL
set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)
if(OPENSSL_FOUND)
    include_directories(${OPENSSL_INCLUDE_DIR})
    list(APPEND ADDITIONAL_LIBS OpenSSL::SSL)
    list(APPEND ADDITIONAL_LIBS OpenSSL::Crypto)
    message(STATUS "OpenSSL Version: ${OPENSSL_VERSION} ${OPENSSL_INCLUDE_DIR} ${OPENSSL_SSL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARIES}")
endif()

# Boost Libraries
set(Boost_USE_MULTITHREADED      ON)
set(Boost_USE_STATIC_LIBS        ON)
find_package(Boost REQUIRED COMPONENTS system thread regex program_options date_time)
if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    list(APPEND ADDITIONAL_LIBS ${Boost_LIBRARIES})
    SET(ASIO_CLIENT 1)
endif()

# required for GCC < 9.0
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    list(APPEND ADDITIONAL_LIBS "stdc++fs")
endif()


# Add 'util' lib for supporting forkpty
list(APPEND ADDITIONAL_LIBS util)

add_definitions( -DASIO_CLIENT=${ASIO_CLIENT} )
add_definitions( -DTHINGER_ENABLE_STREAM_LISTENER)
add_definitions( -DTHINGER_KEEP_ALIVE_SECONDS=50)
add_definitions( -DTHINGER_RECONNECT_SECONDS=15)
add_definitions( -DTHINGER_CONNECT_TIMEOUT=15)
add_definitions( -DTHINGER_LOGURU_DEBUG)


IF(ENABLE_LOGGING)
    add_definitions( -DTHINGER_LOGURU_DEBUG)
ENDIF()

if(OPENSSL_FOUND)
    add_definitions(-DTHINGER_PORT=25206)
    add_definitions(-DTHINGER_OPEN_SSL=1)
else()
    add_definitions(-DTHINGER_PORT=25204)
endif()

file(GLOB_RECURSE SOURCE_FILES
    "src/*.hpp"
    "src/*.h"
    "src/*.cpp"
    "src/*.cc"
    "src/*.c"
)

# generate a static binary
#SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static -pthread")
add_executable(thinger ${SOURCE_FILES})
target_link_libraries(thinger ${ADDITIONAL_LIBS})
