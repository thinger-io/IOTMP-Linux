// The MIT License (MIT)
//
// Copyright (c) 2020 INTERNET OF THINGER SL
// Author: alvarolb@gmail.com (Alvaro Luis Bustamante)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "core/pson.h"
#include "core/thinger_log.hpp"
using namespace protoson;

#ifndef THINGER_DO_NOT_INIT_MEMORY_ALLOCATOR
    #ifndef THINGER_USE_STATIC_MEMORY
        dynamic_memory_allocator alloc;
    #else
        #ifndef THINGER_STATIC_MEMORY_SIZE
            #define THINGER_STATIC_MEMORY_SIZE 512
        #endif
        circular_memory_allocator<THINGER_STATIC_MEMORY_SIZE> alloc;
    #endif
    memory_allocator& protoson::pool = alloc;
#endif

#if ASIO_CLIENT
    #include "linux/asio_client.hpp"
    #include "linux/terminal/thinger_shell.hpp"
    #include "linux/proxies/thinger_proxy.hpp"
    using namespace thinger_client;
    typedef asio_client thinger_device;
#elif defined(THINGER_OPEN_SSL)
    #include "linux/tls_client.hpp"
    using namespace thinger_client;
    typedef tls_client thinger_device;
#else
    #include "linux/tcp_client.hpp"
    using namespace thinger_client;
    typedef thinger_client thinger_device;
#endif
