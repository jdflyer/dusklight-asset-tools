#include <filesystem>

#include "assets_unpack.hpp"
#include "iso.hpp"
#include "arc.hpp"
#include "timg.hpp"
#include "ast.hpp"
#include "bmd.hpp"
#include "dusk/io.hpp"

#include "JSystem/JKernel/JKRDecomp.h"

namespace assets {

const std::filesystem::path assets_unpack_convertFunction_None(
    const std::filesystem::path& outputPath,
    const std::span<const u8>& buffer) {
    auto fs = dusk::io::FileStream::Create(outputPath);
    fs.Write((char*)buffer.data(), buffer.size());
    return outputPath;
}

const std::unordered_map<std::string, unpackConvertFunctionType> unpackConvTable = {
    {".iso", iso_unpack},
    {".arc", arc_unpack},
    {"speakerse.arc", assets_unpack_convertFunction_None},
    {"SpeakerSe.arc", assets_unpack_convertFunction_None},
    {"HomeButton.arc", assets_unpack_convertFunction_None},
    {".bti", bti_unpack},
    {".ast", ast_unpack},
    // {".bmd", bmd_unpack}
};

// const std::unordered_map<std::string, unpackConvertFunctionType> convTable = {
// {"/Audiores/Waves/",jaudio_wave_dir_unpack}
// };

void assets_unpack_check_dir(const std::filesystem::path& path) {}

std::filesystem::path assets_unpack_write(
    const std::filesystem::path& name, const std::span<const u8>& buffer, const std::filesystem::path& inputName) {
    const std::span<const u8>* outputBuffer = &buffer;
    bool compressed = false;
    std::vector<u8> decompBuffer;
    std::span<const u8> decompBufferSpan;
    if (buffer.size() >= 4 && JKRDecomp::checkCompressed((u8*)buffer.data()) !=
        COMPRESSION_NONE) {  // Gross cast here but that's just how JSystem works lol
        u32 outputSize = JKRDecompExpandSize((u8*)buffer.data());
        decompBuffer.resize(outputSize);
        JKRDecomp::decode((u8*)buffer.data(), decompBuffer.data(), outputSize,
            0);  // I'm not using the function parameters right but it works??
        decompBufferSpan = decompBuffer;
        outputBuffer = &decompBufferSpan;
        compressed = true;
    }

    unpackConvertFunctionType convertFunction = nullptr;

    // Search the table first by full filename, then by extension

    const std::string fullName = inputName.filename();
    auto it = unpackConvTable.find(fullName);
    if (it != unpackConvTable.end()) {
        convertFunction = it->second;
    }

    const std::string ext = inputName.extension().string();
    if (convertFunction == nullptr) {
        it = unpackConvTable.find(ext);
        if (it != unpackConvTable.end()) {
            convertFunction = it->second;
        }
    }

    std::string compressedFlag = compressed ? ".c" : "";
    const std::filesystem::path outputPath =
        name.parent_path() / (name.stem().string() + compressedFlag + name.extension().string());

    // Convert the file if any candidates
    if (convertFunction != nullptr) {
        return convertFunction(outputPath, *outputBuffer);
    }

    // Otherwise, write the file
    auto fs = dusk::io::FileStream::Create(outputPath);
    fs.Write((char*)outputBuffer->data(), outputBuffer->size());
    return outputPath;
}

int assets_unpack_main(const std::filesystem::path& input, const std::filesystem::path& output) {
    std::filesystem::path absoluteInput = std::filesystem::absolute(input);
    std::filesystem::path absoluteOutput = std::filesystem::absolute(output);
    if (std::filesystem::is_directory(absoluteInput)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(absoluteInput
                 )) {
            if (!entry.is_directory()) {
                auto rel = absoluteOutput / std::filesystem::relative(entry.path(),absoluteInput);
                std::filesystem::create_directories(rel.parent_path());
                const auto buffer = dusk::io::FileStream::ReadAllBytes(entry.path());
                printf("%s -> %s\n",entry.path().c_str(),rel.c_str());
                assets_unpack_write(rel, std::span<const u8>(buffer), entry.path());
            }
        }
    } else {
        const auto buffer = dusk::io::FileStream::ReadAllBytes(absoluteInput);
        assets_unpack_write(absoluteOutput, std::span<const u8>(buffer), absoluteInput);
    }
    return 0;
}

}  // namespace assets
