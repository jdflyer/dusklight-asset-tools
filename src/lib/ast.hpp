#pragma once
#include <filesystem>
#include <span>

namespace assets {

const std::filesystem::path ast_unpack(
    const std::filesystem::path& outputName, const std::span<const u8>& buffer);
const std::vector<u8> ast_pack(const std::filesystem::path& source);
};  // namespace assets
