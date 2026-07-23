#pragma once
#include <sstream>
#include <string>
#include <vector>
#include "nlohmann/json.hpp"
#include "dusk/io.hpp"

namespace assets {

// Helper for JSON
template <typename T>
std::string toHex(T value) {
    std::ostringstream ss;
    ss << "0x" << std::uppercase << std::hex << static_cast<u32>(value);
    return ss.str();
}

inline u32 fromHex(const std::string& value) {
    return std::stoul(value, nullptr, 16);
}

inline std::string getKeyString(const nlohmann::json& j, const std::string& key) {
    return j.at(key).get<std::string>();
}
inline u32 getKeyHex(const nlohmann::json& j, const std::string& key) {
    return fromHex(getKeyString(j, key));
}

inline void fs_writeBuf(dusk::io::FileStream& fs, const std::vector<u8>& buf) {
    fs.Write((const char*)buf.data(), buf.size());
}
inline void fs_writeString(dusk::io::FileStream& fs, const std::string str) {
    fs.Write(str.data(), str.size());
}

}  // namespace assets
