cmake_minimum_required(VERSION 3.11...3.21)
project(thinger_iotmp)

if (${CMAKE_VERSION} VERSION_GREATER "3.24")
  cmake_policy(SET CMP0135 NEW)
endif()

# set package version
SET(VERSION_MAJOR "1" CACHE STRING "Major version number")
set(VERSION_MINOR "2" CACHE STRING "Minor version number")
set(VERSION_PATCH "3" CACHE STRING "Patch version number")

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

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# configure Thinger.io parameters
OPTION(ASIO_CLIENT "Enable ASIO CLIENT" ON)
OPTION(STATIC "Enable static linking" OFF)
OPTION(BIN_EXTENSION "Set binary file extension" OFF)

SET(ARCH "x86_64" CACHE STRING "Compiler architecure")

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

find_package(Boost REQUIRED COMPONENTS system thread regex program_options date_time filesystem process)
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

# Add atomic for 32 bit arm
if("${ARCH}" MATCHES "^armv7m" OR
   "${ARCH}" MATCHES "^mips" OR
   "${ARCH}" MATCHES "^x32")
  message(STATUS "Linking against libatomic")
  list(APPEND ADDITIONAL_LIBS atomic)
endif()

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
  GIT_TAG v1.14.1
)
FetchContent_MakeAvailable(spdlog)
add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
list(APPEND ADDITIONAL_LIBS spdlog::spdlog)

if (NOT THINGER_MASTER_PROJECT)

  message(STATUS "Generating install of ${PROJECT_NAME}")

  # Source files
  file(GLOB_RECURSE SOURCE_FILES
    "src/thinger/*.hpp"
    "src/thinger/*.h"
    "src/thinger/*.cpp"
    "src/thinger/*.cc"
    "src/thinger/*.c"
  )

  add_library(${PROJECT_NAME} STATIC ${SOURCE_FILES})

  target_compile_definitions(${PROJECT_NAME} PUBLIC -DTHINGER_SERVER="iot.thinger.io")
  target_compile_definitions(${PROJECT_NAME} PUBLIC -DTHINGER_ENABLE_STREAM_LISTENER)

  add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

  # json
  if ( TARGET nlohmann_json )
      message(STATUS "Adding json to thinger library")
      list(APPEND ADDITIONAL_LIBS nlohmann_json::nlohmann_json)
  endif()

  # httplib
  if( TARGET httplib )
      #target_link_libraries(${PROJECT} PUBLIC httplib)
      if ( TARGET nlohmann_json )
        message(STATUS "Adding httplib to thinger library")
        list(APPEND ADDITIONAL_LIBS httplib::httplib)
      endif()
  endif()
  target_link_libraries(${PROJECT_NAME} ${ADDITIONAL_LIBS})

  set(THINGER_INCLUDES_LEVEL "")
  target_include_directories(${PROJECT_NAME} ${THINGER_INCLUDES_LEVEL} PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/src>"
                                      "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

  export(TARGETS ${PROJECT_NAME} FILE thingerConfigTargets.cmake)
  export(PACKAGE ${PROJECT_NAME})

else()

  message(STATUS "Generating binary of ${PROJECT_NAME}")

  # Source files
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
    target_compile_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:DEBUG>:${TEST_OPTIONS}>")
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
  endif()

  target_link_libraries(${PROJECT_NAME} ${ADDITIONAL_LIBS})
endif()

if(BIN_EXTENSION)
  set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}.${ARCH}")
  message(STATUS "Binary name is ${PROJECT_NAME}.${ARCH}")
endif()
