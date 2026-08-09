#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <cmrc/cmrc.hpp>

extern cmrc::embedded_filesystem fs;

namespace fs_helper {
    inline std::string_view get_sview_from_file(std::string_view file_path) {
        auto file = fs.open(std::string{file_path});
        const char* data = file.begin();
        size_t size = file.size();
        return std::string_view(data, size);
    }

    template <typename byte_t>
    std::span<const byte_t> get_bytes_from_file(std::string_view file_path) {
        static_assert(sizeof(byte_t) == sizeof(char8_t), "`byte_t` must be 8 bits (1 byte) in size!");
        auto file = fs.open(std::string{file_path});
        const byte_t* data = reinterpret_cast<const byte_t*>(file.begin());
        size_t size = file.size();
        return std::span<const byte_t>{data, size};
    }
}  // namespace fs_helper
