#ifndef THINGER_LOGGER_HPP
#define THINGER_LOGGER_HPP
#pragma once

#include <cinttypes>

# if __has_include(<spdlog/spdlog.h>)
    #include <spdlog/spdlog.h>
    #include <fmt/printf.h>
    #include <string>
    #include <memory>
    #include <stdexcept>

    #define THINGER_LOG_SPDLOG

    extern int spdlog_verbosity_level;

    #define LOG_INFO(...) SPDLOG_INFO(fmt::sprintf(__VA_ARGS__))
    #define LOG_ERROR(...) SPDLOG_ERROR(fmt::sprintf(__VA_ARGS__))
    #define LOG_WARNING(...) SPDLOG_WARN(fmt::sprintf(__VA_ARGS__))
    #define LOG_LEVEL(LEVEL, ...) \
        if ( LEVEL <= spdlog_verbosity_level ) SPDLOG_TRACE("{} -> {}", LEVEL, fmt::sprintf(__VA_ARGS__))
    #define THINGER_LOG(...) SPDLOG_INFO(fmt::sprintf(__VA_ARGS__))
    #define THINGER_LOG(...) SPDLOG_INFO(fmt::sprintf(__VA_ARGS__))
    #define THINGER_LOG_TAG(TAG, ...) SPDLOG_INFO("[{}] {}", TAG, fmt::sprintf(__VA_ARGS__))
    #define THINGER_LOG_ERROR(...) SPDLOG_ERROR(fmt::sprintf(__VA_ARGS__))
    #define THINGER_LOG_ERROR_TAG(TAG, ...) SPDLOG_ERROR("[{}] {}", TAG, fmt::sprintf(__VA_ARGS__))


#elif __has_include(<loguru.hpp>)
    #include <loguru.hpp>
    #define LOG_INFO(...) LOG_F(INFO, __VA_ARGS__)
    #define LOG_ERROR(...) LOG_F(ERROR, __VA_ARGS__)
    #define LOG_WARNING(...) LOG_F(WARNING, __VA_ARGS__)
    #define LOG_LEVEL(...) VLOG_F(__VA_ARGS__)

    #define THINGER_LOG(...) LOG_F(INFO, __VA_ARGS__)
    #define THINGER_LOG(...) LOG_F(INFO, __VA_ARGS__)
    #define THINGER_LOG_TAG(TAG, ...) LOG_F(INFO, "[" TAG "] " __VA_ARGS__)
    #define THINGER_LOG_ERROR(...) LOG_F(ERROR, __VA_ARGS__)
    #define THINGER_LOG_ERROR_TAG(TAG, ...) LOG_F(ERROR, "[" TAG "] " __VA_ARGS__)
#elif defined(THINGER_SERIAL_DEBUG)
    #define THINGER_LOG(...) Serial.printf(__VA_ARGS__)
    #define THINGER_LOG_ERROR(...) Serial.printf(__VA_ARGS__)

#else
    #define LOG_INFO(...) void()
    #define LOG_ERROR(...) void()
    #define LOG_WARNING(...) void()
    #define LOG_LEVEL(...) void()

    #define THINGER_LOG(...) void()
    #define THINGER_LOG_ERROR(...) void()
    #define THINGER_LOG_TAG(...) void()
    #define THINGER_LOG_ERROR_TAG(...) void()
#endif

#endif