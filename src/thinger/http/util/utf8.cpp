#include <fstream>
#include <vector>

extern "C" int utf8_naive(const unsigned char *data, int len);

bool file_is_utf8(std::string file_path){
    std::ifstream input(file_path, std::ifstream::binary);
    if(!input.is_open()){
        return false;
    }

    std::vector<char> buffer (4096,0);

    while(!input.eof()) {
        input.read(buffer.data(), buffer.size());
        std::streamsize dataSize = input.gcount();
        if(utf8_naive(reinterpret_cast<const unsigned char*>(buffer.data()), dataSize) != 0){
            return false;
        }
    }

    return true;
}