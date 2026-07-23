#pragma once
#include <filesystem>
#include "sjis.hpp"
#include <shared_mutex>
#include <mutex>

namespace assets {

// using packConvertEntryFunctionType = std::function<const std::filesystem::path(const
// std::filesystem::path& sourcePath, const std::filesystem::path& dstPathOriginal)>;
using packConvertBinaryFunctionType =
    std::function<const std::vector<u8>(const std::filesystem::path& source)>;

struct AssetPackOptions {
    bool noCompress;

    static const AssetPackOptions getOptions() {
        std::shared_lock lock(s_mutex);
        return s_options;
    }
    static void setOptions(const AssetPackOptions& options) {
        std::unique_lock lock(s_mutex);
        s_options = options;
    }
private:
    static std::shared_mutex s_mutex;
    static AssetPackOptions s_options;
};
    
struct packDef {
    packConvertBinaryFunctionType convFunction;
    std::string dstExtension;
    bool sourceIsDir = false;
    int numExtensionsToStrip = 0;
};

extern const std::unordered_map<std::string, packDef> packConvTable;
extern const AssetPackOptions g_options;

inline std::vector<std::filesystem::directory_entry> getSortedFileList(
    const std::filesystem::path& path) {
    std::vector<std::filesystem::directory_entry> entries(
        std::filesystem::directory_iterator(path), std::filesystem::directory_iterator{});
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        // This ordering matches the original tools (lowercase compare with _ being sorted last)
        auto an = utf8_to_sjis(a.path().filename().string());
        auto bn = utf8_to_sjis(b.path().filename().string());
        return std::lexicographical_compare(
            an.begin(), an.end(), bn.begin(), bn.end(), [](char a, char b) {
                auto rank = [](char c) -> int {
                    if (c == '_')
                        return 'z' + 1;
                    return std::tolower(c);
                };
                return rank(a) < rank(b);
            });
    });
    return entries;
}

std::filesystem::path assets_pack_convert_entry(const std::filesystem::path& sourcePath,
    const std::filesystem::path& dstPathOriginal, std::vector<u8>* output);
int assets_pack_main(const std::filesystem::path& input, const std::filesystem::path& output, const AssetPackOptions& options);

}  // namespace assets
