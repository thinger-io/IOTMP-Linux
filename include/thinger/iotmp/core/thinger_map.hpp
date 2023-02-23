// The MIT License (MIT)
//
// Copyright (c) 2017 THINK BIG LABS SL
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

#ifndef THINGER_CLIENT_MAP_HPP
#define THINGER_CLIENT_MAP_HPP

#include "pson.h"
#include <cstring>

namespace thinger::iotmp{

    template <class K, class V>
    struct map_entry{
        map_entry()= default;
        K left;
        V right;
    };

    template <class K, class V>
    class thinger_map : public protoson::pson_container<map_entry<K, V>>{

    public:
        thinger_map()= default;

        virtual ~thinger_map()= default;

    protected:
        bool equal(const char* key, const char* value2) const{
            return strcmp(key, value2)==0;
        }

        template <class KK>
        bool equal(KK value_1, KK value_2) const{
            return value_1==value_2;
        }

    public:

        size_t size(){
            size_t size = 0;
            for(auto it=protoson::pson_container<map_entry<K, V>>::begin(); it.valid(); it.next()){
                ++size;
            }
            return size;
        }

        typename protoson::pson_container<map_entry<K, V>>::iterator find_it(K key) const{
            for(auto it=protoson::pson_container<map_entry<K, V>>::begin(); it.valid(); it.next()){
                if(equal(it.item().left, key)) return it;
            }
            return {};
        }

        V* find(K key) const
        {
            auto it = find_it(key);
            return it.valid() ? &(it.item().right) : nullptr;
        }

        bool contains(K key) const{
            return find(key)!=nullptr;
        }

        V& operator[](K key){
            V* existing = find(key);
            if(existing!=nullptr) return *existing;
            auto* new_item = protoson::pson_container<map_entry<K, V>>::create_item();
            // TODO add exception if nex_item is null
            new_item->left = key;
            return new_item->right;
        }

        bool erase(K key)
        {
            return protoson::pson_container<map_entry<K, V>>::erase(find_it(key));
        }

    };
}

#endif