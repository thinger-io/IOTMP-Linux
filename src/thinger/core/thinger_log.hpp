#ifdef THINGER_LOGURU_DEBUG
    #include <loguru.hpp>
    #define THINGER_LOG(...) LOG_F(INFO, __VA_ARGS__)
    #define THINGER_LOG_TAG(TAG, ...) LOG_F(INFO, "[" TAG "] " __VA_ARGS__)
    #define THINGER_LOG_ERROR(...) LOG_F(ERROR, __VA_ARGS__)
    #define THINGER_LOG_ERROR_TAG(TAG, ...) LOG_F(ERROR, "[" TAG "] " __VA_ARGS__)
#elif defined(THINGER_SERIAL_DEBUG)
    #define THINGER_LOG(...) Serial.printf(__VA_ARGS__)
    #define THINGER_LOG_ERROR(...) Serial.printf(__VA_ARGS__)
#else
    #define THINGER_LOG(...) void()
    #define THINGER_LOG_ERROR(...) void()
    #define THINGER_LOG_TAG(...) void()
    #define THINGER_LOG_ERROR_TAG(...) void()
#endif