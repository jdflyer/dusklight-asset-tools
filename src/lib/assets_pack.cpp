#include "assets_pack.hpp"
#include <unordered_map>
#include <thread>
#include <semaphore>
#include <future>
#include "iso.hpp"
#include "arc.hpp"
#include "timg.hpp"
#include "ast.hpp"
#include "dusk/io.hpp"
#include "yaz0_compress.hpp"

namespace assets {

const std::vector<u8> assets_pack_convertFunction_None(const std::filesystem::path& source) {
    return dusk::io::FileStream::ReadAllBytes(source);
}

const std::unordered_map<std::string, packDef> packConvTable = {
    {".iso", {iso_pack, ".iso", true}},
    {"boot.bin.json", {boot_bin_pack, ".bin", false, 1}},
    {".arc", {arc_pack, ".arc", true}},
    {"speakerse.arc", {assets_pack_convertFunction_None, ".arc", false}},
    {".bti", {bti_pack, ".bti", true}},
    {".ast", {ast_pack, ".ast", true}}
};

std::shared_mutex AssetPackOptions::s_mutex;
AssetPackOptions AssetPackOptions::s_options;

// const std::unordered_map<std::string, packConvertFunctionType> dirPackConvTable = {
// {"/Audiores/Waves/",jaudio_wave_dir_pack}
// };

bool isSourceNewer(const std::filesystem::path& sourcePath, const std::filesystem::path& dstPath) {
    if (!std::filesystem::exists(dstPath)) {
        return true;
    }
    bool sourceIsDirectory = std::filesystem::is_directory(sourcePath);
    bool dstIsDirectory = std::filesystem::is_directory(dstPath);
    if (!sourceIsDirectory && !dstIsDirectory) {
        return std::filesystem::last_write_time(sourcePath) >=
               std::filesystem::last_write_time(dstPath);
    }
    if (sourceIsDirectory && !dstIsDirectory) {
        auto dstMTime = std::filesystem::last_write_time(dstPath);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(sourcePath)) {
            if (entry.is_regular_file() && entry.last_write_time() >= dstMTime) {
                return true;
            }
        }
        return false;
    }
    if (!sourceIsDirectory && dstIsDirectory) {
        auto srcMTime = std::filesystem::last_write_time(sourcePath);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dstPath)) {
            if (entry.is_regular_file() && srcMTime >= entry.last_write_time()) {
                return true;
            }
        }
        return false;
    }
    // In the case both are directories, get the max mtime from a recursive search and compare
    std::filesystem::file_time_type srcMTime;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(sourcePath)) {
        if (entry.is_regular_file()) {
            srcMTime = std::max(srcMTime, entry.last_write_time());
        }
    }
    std::filesystem::file_time_type dstMTime;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dstPath)) {
        if (entry.is_regular_file()) {
            dstMTime = std::max(srcMTime, entry.last_write_time());
        }
    }
    return srcMTime >= dstMTime;
}

std::filesystem::path assets_pack_convert_entry(
    const std::filesystem::path& sourcePath, const std::filesystem::path& dstPathOriginal, std::vector<u8>* output) {
    bool isDir = std::filesystem::is_directory(sourcePath);

    std::filesystem::path outputPath = dstPathOriginal;
    bool doCompress = false;
    if (outputPath.stem().extension() == ".c") {
        doCompress = true;
        outputPath = outputPath.parent_path() /
                     std::filesystem::path(
                         outputPath.stem().stem().string() + outputPath.extension().string());
    }

    if (AssetPackOptions::getOptions().noCompress) {
        doCompress = false;
    }

    packConvertBinaryFunctionType convFunction = nullptr;
    std::string newExtension = outputPath.extension().string();
    int numExtensionsToStrip = 0;

    // First, search all explicit names
    auto it = packConvTable.find(sourcePath.filename().string());
    if (it != packConvTable.end()) {
        if ((!isDir && it->second.sourceIsDir == false) || (isDir && it->second.sourceIsDir)) {
            convFunction = it->second.convFunction;
            newExtension = it->second.dstExtension;
            numExtensionsToStrip = it->second.numExtensionsToStrip;
        }
    }

    // Next, search by extension
    if (convFunction == nullptr) {
        it = packConvTable.find(sourcePath.extension().string());
        if (it != packConvTable.end()) {
            if ((!isDir && it->second.sourceIsDir == false) || (isDir && it->second.sourceIsDir)) {
                convFunction = it->second.convFunction;
                newExtension = it->second.dstExtension;
                numExtensionsToStrip = it->second.numExtensionsToStrip;
            }
        }
    }

    if (isDir && convFunction == nullptr) {
        return outputPath;
    }

    auto stem = outputPath.stem();
    for (int i = 0; i < numExtensionsToStrip; i++) {
        stem = stem.stem();
    }
    outputPath = outputPath.parent_path() / std::filesystem::path(stem.string() + newExtension);
    
    bool doConvert = isSourceNewer(sourcePath, outputPath);
    if (!doConvert) {
        return outputPath;
    }

    printf("Converting %s -> %s\n",sourcePath.string().c_str(),outputPath.filename().string().c_str());

    std::vector<u8> defaultOutput;
    std::vector<u8>* outputBuffer= &defaultOutput;
    if (output != nullptr) {
        outputBuffer = output;
    }
    if (convFunction != nullptr) {
        *outputBuffer = convFunction(sourcePath);
    }

    std::vector<u8> compressedBuffer;
    if (doCompress) {
        if (convFunction == nullptr) {
            *outputBuffer = dusk::io::FileStream::ReadAllBytes(sourcePath);
        }
        compressedBuffer = Yaz0Compress({*outputBuffer});
        *outputBuffer = compressedBuffer;
    }

    if (output != nullptr) {
        if (output->size() == 0) {
            *output = dusk::io::FileStream::ReadAllBytes(sourcePath);
        }
    }else {
        if (!std::filesystem::exists(outputPath.parent_path())) {
            std::filesystem::create_directories(outputPath.parent_path());
        }
        if (convFunction == nullptr && doCompress == false) {
            std::filesystem::copy(sourcePath,outputPath);
        }else {
            auto fs = dusk::io::FileStream::Create(outputPath);
            fs_writeBuf(fs,*outputBuffer);
        }
    }

    return outputPath;
}

void assets_pack_copy_recurse(
    const std::filesystem::path& input, const std::filesystem::path& output, std::counting_semaphore<>& sem) {
    auto entries = getSortedFileList(input);

    
    std::vector<std::future<void>> futures;

    for (const auto& entry : entries) {
        const auto relative = std::filesystem::relative(entry.path(), input);

        if (!entry.is_directory() || packConvTable.find(entry.path().extension().string()) != packConvTable.end()) {
            
            sem.acquire();
            futures.push_back(std::async(std::launch::async, [=, &sem] {
                assets_pack_convert_entry(entry.path(), output / relative, nullptr);
                sem.release();
            }));
            continue;
        }

        std::filesystem::create_directories(output/relative);

        assets_pack_copy_recurse(entry.path(), output / relative, sem);
    }

    for (auto& f : futures) {
        f.get();
    }
}

int assets_pack_main(const std::filesystem::path& input, const std::filesystem::path& output, const AssetPackOptions& options) {
    AssetPackOptions::setOptions(options);
    if (packConvTable.find(input.extension().string()) != packConvTable.end() || packConvTable.find(input.filename().string()) != packConvTable.end()) {
        // If argument is an asset, convert it right away
        assets_pack_convert_entry(input, output, nullptr);
    } else {
        // printf("%d\n",std::thread::hardware_concurrency());
        auto sem = std::counting_semaphore(std::thread::hardware_concurrency());
        assets_pack_copy_recurse(input, output, sem);
    }
    return 0;
}

}  // namespace assets
