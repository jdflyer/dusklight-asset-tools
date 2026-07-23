#pragma once
#include <filesystem>
#include <functional>
#include <span>

namespace assets {

using unpackConvertFunctionType =
    std::function<const std::filesystem::path(const std::filesystem::path& outputPath,
        const std::span<const u8>& buffer)>;

const std::filesystem::path assets_unpack_convertFunction_None(
    const std::filesystem::path& outputPath, const std::span<const u8>& buffer);
std::filesystem::path assets_unpack_write(
    const std::filesystem::path& name, const std::span<const u8>& buffer,const std::filesystem::path& inputName);
int assets_unpack_main(const std::filesystem::path& input, const std::filesystem::path& output);
void assets_unpack_check_dir(const std::filesystem::path& path);

}  // namespace assets
