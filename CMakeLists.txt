cmake_minimum_required(VERSION 3.10...3.21)
project(iotmp_linux)

# set package version
set(VERSION_MAJOR 0)
set(VERSION_MINOR 0)
set(VERSION_PATCH 1)

# add version for source code
add_definitions(-DVERSION_MAJOR=${VERSION_MAJOR})
add_definitions(-DVERSION_MINOR=${VERSION_MINOR})
add_definitions(-DVERSION_PATCH=${VERSION_PATCH})

# Check if iotmp_linux is being via add_subdirectory
#set(IOTMP_LINUX_MASTER_PROJECT ON)
if(NOT DEFINED IOTMP_LINUX_MASTER_PROJECT)
  if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(IOTMP_LINUX_MASTER_PROJECT ON)
  else()
    set(IOTMP_LINUX_MASTER_PROJECT OFF)
  endif()
endif()

# install options
option(IOTMP_LINUX_INSTALL "Generate the install target" ${IOTMP_LINUX_MASTER_PROJECT})

#set(THINGER_IOTMP_TARGET_NAME               ${PROJECT_NAME})

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
include_directories("src/loguru")

# configure Thinger.io parameters
OPTION(ASIO_CLIENT "Enable ASIO CLIENT" ON)

# OpenSSL
#set(OPENSSL_USE_STATIC_LIBS TRUE)

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

file(GLOB_RECURSE SOURCE_FILES
    "src/*.hpp"
    "src/*.h"
    "src/*.cpp"
    "src/*.cc"
    "src/*.c"
)

if(IOTMP_LINUX_BUILD_SHARED OR BUILD_SHARED_LIBS)
    add_library(iotmp_linux SHARED ${SOURCE_FILES} ${IOTMP_LINUX_ALL_HEADERS})
    target_compile_definitions(iotmp_linux PUBLIC IOTMP_LINUX_SHARED_LIB)
else()
    add_library(iotmp_linux STATIC ${SOURCE_FILES} ${IOTMP_LINUX_ALL_HEADERS})
endif()

add_library(thinger::thinger ALIAS iotmp_linux)
target_include_directories(iotmp_linux ${IOTMP_LINUX_INCLUDES_LEVEL} PUBLIC "$BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/src/thinger"
                                      "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

target_link_libraries(iotmp_linux ${ADDITIONAL_LIBS})

if (IOTMP_LINUX_INSTALL)
  message(STATUS "Generating install")

  #add_library(${PROJECT_NAME}::${THINGER_IOTMP_TARGET_NAME} ALIAS ${THINGER_IOTMP_TARGET_NAME})
  message(STATUS "thinger_iotmp_linux_client_include_build_dir ${THINGER_IOTMP_INCLUDE_BUILD_DIR}")
  message(STATUS "thinger_iotmp_linux_client_include_build_dir ${THINGER_IOTMP_INCLUDE_INCLUDE_INSTALL_DIR}")

  install(DIRECTORY src/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
  install(
    TARGETS iotmp_linux
    EXPORT iotmp_linux
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )

else()
  if(APPLE)

  else()
    # generate a static binary
    #SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static -pthread")

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-psabi")

    # strip binary
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -s")
  endif()

  add_executable(thinger ${SOURCE_FILES})

  if(APPLE)
    # use this options to test with sanitizer
    add_definitions(-DBOOST_LOG_DYN_LINK=1)
    list(APPEND TEST_OPTIONS -fsanitize=address -fno-omit-frame-pointer -fsanitize-recover=address -g)
    target_compile_options(thinger PRIVATE "$<$<CONFIG:DEBUG>:${TEST_OPTIONS}>")
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
  endif()

  target_link_libraries(thinger ${ADDITIONAL_LIBS})
endif()


