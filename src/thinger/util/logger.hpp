#ifndef THINGER_LOGGER_HPP
#define THINGER_LOGGER_HPP
#pragma once

#include <cinttypes>

# if __has_include(<spdlog/spdlog.h>)
    #include <spdlog/spdlog.h>
    #include <spdlog/fmt/bundled/printf.h>
    #include <string>
    #include <memory>
    #include <stdexcept>

    #define THINGER_LOG_SPDLOG

    #define LOG_INFO(...)                   SPDLOG_INFO(fmt::sprintf(__VA_ARGS__))
    #define LOG_ERROR(...)                  SPDLOG_ERROR(fmt::sprintf(__VA_ARGS__))
    #define LOG_WARNING(...)                SPDLOG_WARN(fmt::sprintf(__VA_ARGS__))
    #define LOG_DEBUG(...)                  SPDLOG_DEBUG(fmt::sprintf(__VA_ARGS__))
    #define LOG_TRACE(...)                  SPDLOG_TRACE(fmt::sprintf(__VA_ARGS__))
    #define LOG_LEVEL(LEVEL, ...)           SPDLOG_LOGGER_CALL(spdlog::default_logger_raw(), static_cast<spdlog::level::level_enum>(LEVEL), fmt::sprintf(__VA_ARGS__))

    #define THINGER_LOG(...)                SPDLOG_INFO(fmt::sprintf(__VA_ARGS__))
    #define THINGER_LOG_TAG(TAG, ...)       SPDLOG_INFO("[{}] {}", TAG, fmt::sprintf(__VA_ARGS__))
    #define THINGER_LOG_ERROR(...)          SPDLOG_ERROR(fmt::sprintf(__VA_ARGS__))
    #define THINGER_LOG_ERROR_TAG(TAG, ...) SPDLOG_ERROR("[{}] {}", TAG, fmt::sprintf(__VA_ARGS__))

#elif defined(THINGER_SERIAL_DEBUG)
    #define THINGER_LOG(...) Serial.printf(__VA_ARGS__)
    #define THINGER_LOG_ERROR(...) Serial.printf(__VA_ARGS__)

#else
    #define LOG_INFO(...) void()
    #define LOG_ERROR(...) void()
    #define LOG_WARNING(...) void()
    #define LOG_DEBUG(...) void()
    #define LOG_TRACE(...) void()

    #define THINGER_LOG(...) void()
    #define THINGER_LOG_ERROR(...) void()
    #define THINGER_LOG_TAG(...) void()
    #define THINGER_LOG_ERROR_TAG(...) void()
#endif

#endif
