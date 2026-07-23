#pragma once
#include "assets_unpack.hpp"
#include "JSystem/JKernel/JKRArchive.h"
#include <nlohmann/json.hpp>

namespace assets {

const std::filesystem::path arc_unpack(const std::filesystem::path& outputName,
    const std::span<const u8>& buffer);
const std::vector<u8> arc_pack(const std::filesystem::path& source);

}
