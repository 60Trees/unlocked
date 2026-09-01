/**
 * Some code I copied off github
 * @author https://gist.github.com/miguelmota/4fc9b46cf21111af5fa613555c14de92
 */

#include <sstream>
#include <iostream>
#include <iomanip>

std::string uint8_to_hex_string(const uint8_t* v, const size_t s) {
    std::stringstream ss;

    ss << std::hex << std::setfill('0');

    for (int i = 0; i < s; i++) {
        ss << std::hex << std::setw(2) << static_cast<int>(v[i]);
    }

    return ss.str();
}
