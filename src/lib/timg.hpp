#pragma once
#include <filesystem>
#include <span>
#include "JSystem/JUtility/JUTTexture.h"

namespace assets {

// http://www.mindcontrol.org/~hplus/graphics/expand-bits.html
template <u8 v>
constexpr u8 ExpandTo8(uint8_t n) {
    if constexpr (v == 3) {
        return (n << (8 - 3)) | (n << (8 - 6)) | (n >> (9 - 8));
    } else {
        return (n << (8 - v)) | (n >> ((v * 2) - 8));
    }
}


bool writePNG_RGBA8(const std::filesystem::path& filename, u32 width, u32 height,
    const std::vector<GXColor>& rgbaData);
const std::vector<GXColor> timgToRGBA(const ResTIMG& header, const std::span<const u8>& buffer);
const std::filesystem::path bti_unpack(
    const std::filesystem::path& outputName, const std::span<const u8>& buffer);
std::vector<u8> RGBAtoTimg(ResTIMG& header, std::vector<GXColor> rgba);
const std::vector<u8> bti_pack(const std::filesystem::path& source);

GXColor rgb565_to_rgba(u16 rgb565);

};  // namespace assets
