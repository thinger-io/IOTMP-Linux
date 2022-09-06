// The MIT License (MIT)
//
// Copyright (c) 2022 THINGER.IO
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

#include "thinger/thinger.h"

#define USER_ID             "xxxxxxx"
#define DEVICE_ID           "xxxxxxx"
#define DEVICE_CREDENTIAL   "xxxxxxx"

int main(int argc, char *argv[])
{
    loguru::init(argc, argv, {});

    thinger_device thing(USER_ID, DEVICE_ID, DEVICE_CREDENTIAL);
    thinger_shell shell(thing);
    thinger_proxy proxy(thing);

    // define thing resources here. i.e, this is a sum example
    thing["sum"] = [](pson& in, pson& out){
        out["result"] = (int) in["value1"] + (int) in["value2"];
    };

    thing.start();
    return 0;
}