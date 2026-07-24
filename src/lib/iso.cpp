#include "iso.hpp"
#include <array>
#include <dolphin/dvd.h>
#include <fstream>
#include <future>
#include <mutex>
#include <semaphore>
#include "assets.hpp"
#include "assets_pack.hpp"
#include "dusk/dvd_asset.hpp"
#include "dusk/io.hpp"

namespace assets {

struct DolHeader {
    BE(u32) textOffset[7];
    BE(u32) dataOffset[11];
    BE(u32) textAddr[7];
    BE(u32) dataAddr[11];
    BE(u32) textSize[7];
    BE(u32) dataSize[11];
};

struct PathPriority {
    std::filesystem::path path;
    u32 alignment = 4;
};

inline void to_json(nlohmann::ordered_json& j, const PathPriority& f) {
    j = {{"path", f.path}};
    if (f.alignment != 4)
        j["alignment"] = f.alignment;
}

inline void from_json(const nlohmann::ordered_json& j, PathPriority& f) {
    j.at("path").get_to(f.path);
    f.alignment = j.value("alignment", 4);
}

std::array<PathPriority, 10> defaultPathPriority = {{{"rel/"}, {"./"},  // All uncaught files
    {"BOOT"},  // All files in defaultBootOrder
    {"res/Object/"}, {"res/Stage/"}, {"Audiores/Waves/"}, {"Audiores/Stream/"}, {"res/Particle/"},
    {"Movie/", 0x8000}, {"map/"}}};

std::array<PathPriority, 57> defaultBootOrder = {{{"str/Final/Release/COPYDATE"}, {"RELS.arc"},
    {"Audiores/Z2Sound.baa"}, {"Audiores/Seqs/Z2SoundSeqs.arc"}, {"res/Object/LogoUs.arc"},
    {"res/Object/Always.arc"}, {"res/Object/Alink.arc"}, {"res/FieldMap/Field0.arc"},
    {"res/Object/AlAnm.arc"}, {"res/Layout/fmapres.arc"}, {"res/Layout/dmapres.arc"},
    {"res/Layout/clctres.arc"}, {"res/Layout/itemicon.arc"}, {"res/Layout/ringres.arc"},
    {"res/Layout/playerName.arc"}, {"res/Layout/itmInfRes.arc"}, {"res/Layout/button.arc"},
    {"res/CardIcon/cardicon.arc"}, {"res/Msgus/bmgres.arc"}, {"res/Layout/msgcom.arc"},
    {"res/Layout/msgres00.arc"}, {"res/Layout/msgres01.arc"}, {"res/Layout/msgres02.arc"},
    {"res/Layout/msgres03.arc"}, {"res/Layout/msgres04.arc"}, {"res/Layout/msgres05.arc"},
    {"res/Layout/msgres06.arc"}, {"res/Layout/main2D.arc"}, {"res/Fontus/fontres.arc"},
    {"res/Fontus/rubyres.arc"}, {"res/Particle/common.jpc"}, {"res/ItemTable/item_table.bin"},
    {"res/ItemTable/enemy_table.bin"}, {"res/Stage/F_SP102/STG_00.arc"}, {"res/Object/Event.arc"},
    {"res/Object/CamParam.arc"}, {"res/Particle/Pscene001.jpc"}, {"res/Msgus/bmgres8.arc"},
    {"rel/Final/Release/d_a_title.rel"}, {"res/Stage/F_SP102/R00_00.arc"}, {"res/Object/Title.arc"},
    {"res/Object/Demo38_01.arc"}, {"res/Layout/Title2D.arc"}, {"res/Object/Kmdl.arc"},
    {"rel/Final/Release/d_a_obj_ihasi.rel"}, {"rel/Final/Release/d_a_horse.rel"},
    {"res/Object/Obj_ihasi.arc"}, {"res/Object/CstaFB.arc"}, {"res/Object/@bg0056.arc"},
    {"res/Object/@bg0010.arc"}, {"res/Object/HyShd.arc"}, {"res/Object/Horse.arc"},
    {"res/Object/J_Umak.arc"}, {"res/Object/Midna.arc"}, {"Audiores/Stream/title_back.ast"},
    {"res/Object/fileSel.arc"}, {"Audiores/Stream/menu_select.ast"}}};

// Creates a boot.bin that dolphin can use to launch an unpackaged iso
const std::vector<u8> boot_bin_pack(const std::filesystem::path& source) {
    const std::vector<u8> buffer(sizeof(DVDDiskID_JSON) + sizeof(DVDBB2_BE));

    std::ifstream bootJsonFile(source);
    auto bootJson = nlohmann::json::parse(bootJsonFile);
    *(DVDDiskID_JSON*)buffer.data() = DVDDiskID_JSON::from_json(bootJson);

    return buffer;
}

void iso_parse_fst(const std::filesystem::path& filesystemPath, const std::span<const u8>& fst,
    const std::span<const u8>& fullBuffer) {
    auto entries = (const DVDEntry_BE*)&fst.data()[0];

    auto& root = entries[0];
    u32 numEntries = root.entryLength;

    auto stringTable = fst.subspan(sizeof(entries[0]) * numEntries);

    for (int i = 0; i < numEntries; i++) {
        auto& entry = entries[i];
        if (entry.isDir != 1) {
            continue;
        }
        std::filesystem::path dvdPath = entry.getName(stringTable);
        for (int j = entry.entryOffset; j != 0; j = entries[j].entryOffset) {
            dvdPath = entries[j].getName(stringTable) / dvdPath;
        }
        if (i == 0) {
            dvdPath = "./";  // Make sure files under Root get put in root
        }
        std::filesystem::path dvdPathFull = filesystemPath / dvdPath;
        // printf("Creating Directory: %s\n", dvdPath.c_str());
        if (!std::filesystem::exists(dvdPathFull)) {
            std::filesystem::create_directories(dvdPathFull);
        }
        for (int j = i + 1; j < entry.entryLength; j++) {
            auto& fileEntry = entries[j];
            if (fileEntry.isDir == 1) {
                j = fileEntry.entryLength - 1;
                continue;
            }
            std::filesystem::path filePath = dvdPathFull / fileEntry.getName(stringTable);
            printf("Extracting: %s\n", filePath.string().c_str());
            assets_unpack_write(
                filePath, fullBuffer.subspan(fileEntry.entryOffset, fileEntry.entryLength), filePath);
        }
    }
}

const std::filesystem::path iso_unpack(const std::filesystem::path& outputName,
    const std::span<const u8>& buffer) {
    const u8* data = buffer.data();
    auto diskId = (const DVDDiskID_JSON*)&data[0];

    if (!std::filesystem::exists(outputName)) {
        std::filesystem::create_directories(outputName);
    }

    std::filesystem::create_directories(outputName / "sys");

    nlohmann::ordered_json json;
    json["tool_version"] = 1;
    assets_unpack_write(outputName / "sys" / "bi2.bin", buffer.subspan(0x440, 0x2000), outputName / "sys" / "bi2.bin");
    json["bi2Path"] = "sys/bi2.bin";

    BE(u32) apploaderSize = *(BE<u32>*)&data[0x2440 + 0x14];
    BE(u32) trailerSize = *(BE<u32>*)&data[0x2440 + 0x18] + 0x20;
    assets_unpack_write(
        outputName / "sys" / "apploader.img", buffer.subspan(0x2440, apploaderSize + trailerSize), outputName / "sys" / "apploader.img");
    json["apploaderPath"] = "sys/apploader.img";

    auto bb2 = (const DVDBB2_BE*)&data[0x420];
    // printf("0x2440 + apploaderSize + trailerSize: 0x%X\n", 0x2440 + apploaderSize + trailerSize);
    // printf("bootFilePosition 0x%X\n",bb2->bootFilePosition.host());
    // printf("FSTPosition 0x%X\n",bb2->FSTPosition.host());
    // printf("FSTAddress 0x%X\n",bb2->FSTAddress.host());
    // printf("FSTLength 0x%X\n",bb2->FSTLength.host());
    // printf("FSTMaxLength 0x%X\n",bb2->FSTMaxLength.host());
    // printf("userPosition 0x%X\n",bb2->userPosition.host());
    // printf("userLength 0x%X\n",bb2->userLength.host());
    u32 dolSize = 0;
    auto dolHeader = (const DolHeader*)&data[bb2->bootFilePosition];
    for (int i = 0; i < ARRAY_SIZE(dolHeader->textOffset); i++) {
        dolSize = std::max(dolHeader->textOffset[i] + dolHeader->textSize[i], dolSize);
    }
    for (int i = 0; i < ARRAY_SIZE(dolHeader->dataOffset); i++) {
        dolSize = std::max(dolHeader->dataOffset[i] + dolHeader->dataSize[i], dolSize);
    }
    // printf("dolSize 0x%X\n",dolSize);

    assets_unpack_write(outputName / "sys" / "main.dol", buffer.subspan(bb2->bootFilePosition, dolSize) ,outputName / "sys" / "main.dol");
    json["dolPath"] = "sys/main.dol";
    json["bootPath"] = "sys/boot.bin.json";

    json["dvdPath"] = "files";
    json["fstAddrHi"] = "0x80400000";

    json["pathPriority"] = defaultPathPriority;
    json["bootPriority"] = defaultBootOrder;

    auto bootJson = diskId->to_json();
    auto boot_json_fs = dusk::io::FileStream::Create(outputName / "sys" / "boot.bin.json");
    fs_writeString(boot_json_fs, bootJson.dump(4));

    std::filesystem::path filesystemPath = outputName / "files";
    if (!std::filesystem::exists(filesystemPath)) {
        std::filesystem::create_directories(filesystemPath);
    }
    iso_parse_fst(
        filesystemPath, buffer.subspan(bb2->FSTPosition, bb2->FSTLength), buffer);

    for (const auto& entry : std::filesystem::recursive_directory_iterator(outputName / "files")) {
        if (!entry.is_directory()) {
            continue;
        }
        assets_unpack_check_dir(entry.path());
    }

    auto json_fs = dusk::io::FileStream::Create(outputName / "disk.json");
    fs_writeString(json_fs, json.dump(4));

    return outputName;
}

struct FSTPackEntry {
    std::vector<u8> buffer;
    std::filesystem::path diskPath;
    bool isDir;
    size_t fileNameOffset;
    size_t parentIndex;
    size_t dirSize = 0;
    size_t fstIndex = 0;
    bool used = false;
};

size_t iso_pack_recurse_fst(const std::filesystem::path& original,
    const std::filesystem::path& path, std::vector<FSTPackEntry>& fstEntries,
    std::string& stringTable, size_t parentIndex, std::counting_semaphore<>& sem) {
    auto entries = getSortedFileList(path);
    std::vector<std::future<void> > futures;
    std::vector<FSTPackEntry> localEntries(entries.size());
    std::mutex localEntriesMutex;
    for (int i = 0; i < entries.size(); i++) {
        const auto& entry = entries[i];
        // If it's a file or will be converted to a file
        if (entry.is_directory() &&
            packConvTable.find(entry.path().extension().string()) == packConvTable.end())
        {
            continue;  // We handle these after the async functions have run
        }
        sem.acquire();
        futures.push_back(
            std::async(std::launch::async, [=, &sem, &localEntriesMutex, &localEntries] {
                std::vector<u8> file;
                auto out = assets_pack_convert_entry(entry.path(), entry.path(), &file);
                auto rel = std::filesystem::relative(out, original);
                {
                    std::lock_guard<std::mutex> lock(localEntriesMutex);
                    localEntries[i] = {std::move(file), rel, false, 0, parentIndex};
                }
                sem.release();
            }));
    }

    for (auto& f : futures) {
        f.get();
    }

    size_t beginningEntries = fstEntries.size();
    for (int i = 0; i < entries.size(); i++) {
        const auto& entry = entries[i];
        if (!entry.is_directory() ||
            packConvTable.find(entry.path().extension().string()) != packConvTable.end())
        {
            localEntries[i].fileNameOffset = stringTable.size();
            stringTable += localEntries[i].diskPath.filename().string() + '\0';
            fstEntries.push_back(std::move(localEntries[i]));
        } else {
            size_t newIndex = fstEntries.size();
            fstEntries.push_back({{}, std::filesystem::relative(entry.path(), original), true,
                stringTable.size(), parentIndex});
            size_t currentBeginningEntries = fstEntries.size();
            stringTable += entry.path().filename().string() + '\0';
            size_t childEntries = iso_pack_recurse_fst(
                original, entry.path(), fstEntries, stringTable, fstEntries.size() - 1, sem);
            fstEntries[newIndex].dirSize = currentBeginningEntries + childEntries;
        }
    }

    return fstEntries.size() - beginningEntries;
}

const std::vector<u8> iso_pack(const std::filesystem::path& source) {
    // printf("ISO PACK: %s\n", source.c_str());

    std::ifstream diskJsonFile(source / "disk.json");
    if (!diskJsonFile.is_open()) {
        throw std::runtime_error(std::string("Could not open ") + (source / "disk.json").string());
    }

    auto j = nlohmann::json::parse(diskJsonFile);

    // Allow conversion from both the original boot.bin.json, and the version that dolphin likes to
    // use in sys/boot.bin
    std::filesystem::path bootBinPath = source / j.at("bootPath");
    std::vector<u8> diskIdData(sizeof(DVDDiskID_JSON));
    if (std::filesystem::exists(bootBinPath)) {
        diskIdData = boot_bin_pack(bootBinPath);
    } else {
        bootBinPath = source / "sys" / "boot.bin";
        if (!std::filesystem::exists(bootBinPath)) {
            throw std::runtime_error(std::string(bootBinPath.string() + " does not exist!"));
        }
        diskIdData = dusk::io::FileStream::ReadAllBytes(bootBinPath);
    }

    std::vector<u8> bi2 = dusk::io::FileStream::ReadAllBytes(source / j.at("bi2Path"));
    std::vector<u8> apploader = dusk::io::FileStream::ReadAllBytes(source / j.at("apploaderPath"));
    std::vector<u8> dol = dusk::io::FileStream::ReadAllBytes(source / j.at("dolPath"));

    size_t dolPosition = ALIGN_NEXT(0x2440 + apploader.size(), 0x100);

    std::filesystem::path dvdPath = source / j.at("dvdPath");
    size_t fstPosition = ALIGN_NEXT(dolPosition + dol.size(), 0x100);

    size_t fstAddrHi = getKeyHex(j, "fstAddrHi");

    std::vector<PathPriority> pathPriority = j.at("pathPriority");
    std::vector<PathPriority> bootPriority = j.at("bootPriority");

    std::vector<FSTPackEntry> fstEntries;
    std::string stringTable;
    fstEntries.push_back({{}, "./", true, 0});

    auto sem = std::counting_semaphore(std::thread::hardware_concurrency());

    size_t rootLen =
        iso_pack_recurse_fst(source / dvdPath, source / dvdPath, fstEntries, stringTable, 0, sem);
    fstEntries[0].dirSize = rootLen;

    std::vector<std::tuple<PathPriority, std::vector<FSTPackEntry*> > > fullPriority;
    std::vector<FSTPackEntry*> bootEntries;
    size_t catchPriority = 0;
    for (int i = 0; i < pathPriority.size(); i++) {
        auto& path = pathPriority[i];
        if (path.path == "./") {
            catchPriority = i;
            path.path = "";
            fullPriority.push_back({path, {}});
        } else {
            fullPriority.push_back({path, {}});
        }
    }

    for (const auto& bootPath : bootPriority) {
        for (int i = 0; i < fstEntries.size(); i++) {
            auto& entry = fstEntries[i];
            if (entry.diskPath.string() == bootPath.path.string()) {
                bootEntries.push_back(&entry);
                entry.used = true;
                break;
            }
        }
    }

    for (int i = 0; i < fstEntries.size(); i++) {
        auto& entry = fstEntries[i];
        entry.fstIndex = i;

        if (entry.used) {
            continue;
        }

        size_t maxLen = 0;
        std::tuple<PathPriority, std::vector<FSTPackEntry*> >* best = nullptr;
        for (auto& pathTuple : fullPriority) {
            auto& [path, list] = pathTuple;
            if (path.path == "BOOT") {  // Handle boot priority files
                continue;
            }

            if (entry.diskPath.string().starts_with(path.path.string()) &&
                path.path.string().size() > maxLen)
            {
                maxLen = path.path.string().size();
                best = &pathTuple;
            }
        }
        if (best == nullptr) {
            auto& [path, list] = fullPriority[catchPriority];
            list.push_back(&entry);
        } else {
            auto& [path, list] = *best;
            list.push_back(&entry);
        }
        entry.used = true;
    }

    std::vector<u8> isoBin(isoSize);
    u8* data = isoBin.data();

    auto fst = (DVDEntry_BE*)&data[fstPosition];

    size_t currentOffset = isoSize - 20;
    fst[0] = {1, {0, 0, 0}, 0, (u32)rootLen + 1};
    for (auto& [path, entries] : fullPriority) {
        if (path.path == "BOOT") {
            entries = bootEntries;
        }
        for (const auto& entry : std::views::reverse(entries)) {
            // printf("%s ", entry->diskPath.c_str());
            if (entry->diskPath == "./") {
                continue;
            }
            if (entry->isDir) {
                fst[entry->fstIndex] = {entry->isDir, {}, (u32)entry->parentIndex, (u32)entry->dirSize};
                fst[entry->fstIndex].setNameOffset(entry->fileNameOffset);
            } else {
                size_t size = entry->buffer.size();
                size_t offset = ALIGN_PREV(currentOffset - size, path.alignment);
                fst[entry->fstIndex] = {entry->isDir, {}, (u32)offset, (u32)size};
                fst[entry->fstIndex].setNameOffset(entry->fileNameOffset);
                memcpy(&data[offset], entry->buffer.data(), size);
                currentOffset = offset;
            }
        }
    }
    // printf("\n");

    memcpy(&fst[fstEntries.size()], stringTable.data(), stringTable.size());

    memcpy(&data[0], diskIdData.data(), sizeof(DVDDiskID_JSON));
    memcpy(&data[0x440], bi2.data(), bi2.size());
    memcpy(&data[0x2440], apploader.data(), apploader.size());
    memcpy(&data[dolPosition], dol.data(), dol.size());

    DVDBB2_BE bb2;
    bb2.bootFilePosition = dolPosition;
    bb2.FSTPosition = fstPosition;
    bb2.FSTLength = sizeof(fst[0]) * fstEntries.size() + stringTable.size();
    bb2.FSTMaxLength = bb2.FSTLength;
    bb2.FSTAddress = ALIGN_PREV(fstAddrHi - bb2.FSTLength, 0x20);
    bb2.userPosition = ALIGN_NEXT(bb2.FSTPosition + bb2.FSTLength, 0x10000);
    bb2.userLength = isoSize - bb2.userPosition;
    bb2.padding0 = 0;
    memcpy(&data[0x420], &bb2, sizeof(bb2));

    return isoBin;
}

}  // namespace assets
