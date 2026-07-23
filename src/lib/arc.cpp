#include "arc.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include "JSystem/JKernel/JKRDecomp.h"
#include "assets_pack.hpp"
#include "sjis.hpp"
#include "dusk/io.hpp"
#include "assets.hpp"

namespace assets {

struct ARCInfoUnpack {
    SArcDataInfo dataInfo;
    std::vector<JKRArchive::SDIDirEntry> nodes;
    std::vector<JKRArchive::SDIFileEntry> files;
    const char* stringTable;
    std::span<const u8> fileData;
    nlohmann::ordered_json json;
    std::map<int, nlohmann::ordered_json> paths;
    std::filesystem::path originalPath;

    ARCInfoUnpack(const std::span<const u8>& buffer) {
        const SArcHeader& header = *(SArcHeader*)buffer.data();
        assert(header.signature == 'RARC');

        SArcDataInfo* originalDataInfo = (SArcDataInfo*)(buffer.data() + header.header_length);
        dataInfo = *originalDataInfo;

        const auto nodeArray =
            (JKRArchive::SDIDirEntry*)((u8*)originalDataInfo + dataInfo.node_offset);
        nodes = std::vector<JKRArchive::SDIDirEntry>(nodeArray, nodeArray + dataInfo.num_nodes);

        const auto fileArray =
            (JKRArchive::SDIFileEntry*)((u8*)originalDataInfo + dataInfo.file_entry_offset);
        files =
            std::vector<JKRArchive::SDIFileEntry>(fileArray, fileArray + dataInfo.num_file_entries);

        stringTable =
            (const char*)buffer.data() + header.header_length + dataInfo.string_table_offset;

        fileData =
            buffer.subspan(header.header_length + header.file_data_offset, header.file_data_length);
    }

    const std::string getStringFromTable(u32 index) const {
        return std::string(&stringTable[index]);
    }
};

void arc_unpack_recurse(const std::filesystem::path& path, ARCInfoUnpack& info, u32 dirIndex) {
    const auto& dir = info.nodes[dirIndex];
    std::string dirName = sjis_to_utf8(info.getStringFromTable(dir.name_offset));
    std::filesystem::path newPath = path / dirName;
    std::filesystem::create_directories(newPath);

    for (int i = 0; i < dir.num_entries; i++) {
        const auto& entry = info.files[i + dir.first_file_index];
        const auto name = sjis_to_utf8(info.getStringFromTable(entry.getNameOffset()));
        if (entry.isDirectory()) {
            if (name == ".." || name == ".") {
                nlohmann::ordered_json pathJson;
                pathJson["path"] =
                    std::filesystem::relative(newPath, info.originalPath).string() + "/" + name;
                pathJson["type"] = "dir";
                info.paths[i + dir.first_file_index] = pathJson;
                continue;
            }
            // printf("Dir Entry Found, Index: %d\n", entry.data_offset.host());

            nlohmann::ordered_json pathJson;
            pathJson["path"] =
                std::filesystem::relative(newPath / name, info.originalPath).string();
            pathJson["type"] = "dir";
            info.paths[i + dir.first_file_index] = pathJson;

            arc_unpack_recurse(newPath, info, entry.data_offset);
            continue;
        }

        nlohmann::ordered_json pathJson;
        pathJson["path"] = std::filesystem::relative(newPath / name, info.originalPath).string();
        pathJson["type"] = "file";
        if (info.dataInfo.sync_file_ids_and_indices == false) {
            pathJson["id"] = entry.file_id.host();
        }
        if ((entry.getFlags() & 0x10) == 0 && entry.getFlags() & 0x20) {
            // File will go to aram
            pathJson["is_aram"] = true;
        } else if ((entry.getFlags() & 0x10) == 0 && entry.getFlags() & 0x40) {
            // File will go to dvd
            pathJson["is_dvd"] = true;
        }
        // pathJson["flags"] = entry.getFlags();
        info.paths[i + dir.first_file_index] = pathJson;
        assets_unpack_write(newPath / name, info.fileData.subspan(entry.data_offset, entry.data_size), newPath / name);
    }
}

const std::filesystem::path arc_unpack(const std::filesystem::path& outputName,
    const std::span<const u8>& buffer) {
    ARCInfoUnpack info = ARCInfoUnpack(buffer);

    info.json["tool_version"] = 1;
    std::string root = info.getStringFromTable(info.nodes[0].name_offset);
    info.json["root"] = root;
    info.json["sync_id_with_index"] = info.dataInfo.sync_file_ids_and_indices;

    info.originalPath = outputName;

    arc_unpack_recurse(outputName, info, 0);

    info.json["paths"] = {};
    for (const auto& [k, v] : info.paths) {
        info.json["paths"][std::to_string(k)] = v;
    }

    auto json_fs = dusk::io::FileStream::Create(outputName / "arc.json");
    fs_writeString(json_fs, info.json.dump(4));
    return outputName;
}

struct ARCInfoPack {
    std::vector<JKRArchive::SDIDirEntry> nodes;
    std::map<std::string,int> nodeNameToIndex;
    std::vector<JKRArchive::SDIFileEntry> files;
    std::vector<u8> fileData;
    std::string stringTable;
    std::map<std::string, nlohmann::json> paths;
    std::string rootName;
    std::filesystem::path originalPath;
    bool sync_id_with_index = true;
    u16 maxFileId = 0;
    u32 mMemLength = 0;
    u32 aMemLength = 0;
};

u16 computeNameHash(const std::string& str) {
    u16 hash = 0;
    for (const auto& c : str) {
        hash *= 3;
        hash += (u8)c;
    }
    return hash;
}

u32 truncateName(const std::string& name) {
    std::string truncated(4, ' ');
    int len = name.size() < 4 ? name.size() : 4;
    for (int i = 0; i < len; i++) {
        truncated[i] = std::toupper(name[i]);
    }
    return (truncated[0] << 24) | (truncated[1] << 16) | (truncated[2] << 8) | truncated[3];
}

size_t arc_pack_recurse(const std::filesystem::path& path, ARCInfoPack& info, int& firstIndex, int parentIndex) {
    std::vector<std::filesystem::directory_entry> entries = getSortedFileList(path);
    std::map<std::filesystem::path,std::vector<u8>> pathToData;
    std::vector<std::filesystem::path> updatedPaths;
    for (const auto& e : entries) {
        std::vector<u8> output;
        const auto newPath = assets_pack_convert_entry(e.path(),e.path(),&output);
        pathToData[newPath] = std::move(output);
        updatedPaths.push_back(newPath);
    }
    // Re-sort entries so they appear in with the same index as the source
    std::sort(updatedPaths.begin(), updatedPaths.end(),
    [&](const auto& a, const auto& b) {
        auto relA = std::filesystem::relative(a, info.originalPath);
        auto relB = std::filesystem::relative(b, info.originalPath);
        return info.paths[relA]["FileIndex"] < info.paths[relB]["FileIndex"];
    });

    const auto thisPathRel = std::filesystem::relative(path,info.originalPath);
    int currentDirIndex = info.nodeNameToIndex[thisPathRel.string()];

    for (int i = 0; i < updatedPaths.size(); i++) {
        const auto& entryPath = updatedPaths[i];
        std::vector<u8>& data = pathToData[entryPath];

        auto rel = std::filesystem::relative(entryPath,info.originalPath);
        bool found = info.paths.contains(rel);
        if (!found) {
            printf("Warning: %s Not Found in archive!\n",rel.c_str());
            continue;
        }

        size_t stringOffset = info.stringTable.size();
        std::string fileName = entryPath.filename().string();
        std::string sjisFileName = utf8_to_sjis(fileName);
        info.stringTable += sjisFileName;
        info.stringTable += '\0';
        u16 hash = computeNameHash(sjisFileName);

        const auto fileInfo = info.paths.at(rel);
        u16 fileIndex = fileInfo.value("FileIndex",0);

        if (firstIndex == -1) {
            firstIndex = fileIndex;
        }

        // If the output is a dir, data should be 0, else, create a file
        if (data.size() == 0) {
            // Create Node + File
            JKRArchive::SDIDirEntry node = {truncateName(fileName),stringOffset, hash};

            int nodeIndex = info.nodes.size();
            info.nodes.push_back(node);
            info.nodeNameToIndex[std::filesystem::relative(entryPath,info.originalPath).string()] = nodeIndex;

            int newFirstIndex = -1;
            size_t numEntries = arc_pack_recurse(entryPath,info,newFirstIndex, currentDirIndex);

            info.nodes[nodeIndex].first_file_index = newFirstIndex;
            info.nodes[nodeIndex].num_entries = numEntries;

            JKRArchive::SDIFileEntry fileEntry = {0xFFFF, hash, (2<<24) | (stringOffset &
            0xFFFFFF), nodeIndex, 0x10}; info.files[fileIndex] = fileEntry;
            continue;
        }

        bool is_compressed = data.size() >= 4 && JKRDecomp::checkCompressed((u8*)data.data());
        bool is_aram = fileInfo.value("is_aram",false);
        bool is_dvd = fileInfo.value("is_dvd",false);
        bool is_mmem = (is_aram == false && is_dvd == false);

        u8 flags = (is_compressed << 7) | (is_dvd << 6) | (is_aram << 5) | (is_mmem << 4) |
        (is_compressed<<2) | 1;

        u16 id = fileIndex;
        if (!info.sync_id_with_index) {
            id = fileInfo.value("id",0);
            info.maxFileId = std::max(info.maxFileId,id);
        }

        u32 dataOffset = info.fileData.size();

        u32 expandedSize = ALIGN_NEXT(data.size(),0x20);
        // if (is_compressed) {
        //     expandedSize = JKRDecompExpandSize(data.data());
        // }

        if (is_mmem) {
            info.mMemLength += expandedSize;
        }else if (is_aram) {
            info.aMemLength += expandedSize;
        }

        info.fileData.resize(ALIGN_NEXT(info.fileData.size() + data.size(),0x20));
        memcpy(info.fileData.data()+dataOffset,data.data(),data.size());
        JKRArchive::SDIFileEntry fileEntry =
        {id,hash,(flags<<24)|(stringOffset&0xFFFFFF),dataOffset,data.size()};
        info.files[fileIndex] = fileEntry;
    }

    // Search for the /. and /.. entries of this dir
    
    if (info.paths.contains(thisPathRel.string() + "/.")) {
        const auto& fileJson = info.paths[thisPathRel.string() + "/."];
        int index = fileJson.value("FileIndex",0);
        info.files[index] = {0xFFFF,computeNameHash("."),(2<<24)|(0),currentDirIndex,0x10};
    }
    if (info.paths.contains(thisPathRel.string() + "/..")) {
        const auto& fileJson = info.paths[thisPathRel.string() + "/.."];
        int index = fileJson.value("FileIndex",0);
        info.files[index] = {0xFFFF,computeNameHash(".."),(2<<24)|(2),parentIndex,0x10};
    }

    return entries.size() + 2; // Account for the . and .. directories
}

const std::vector<u8> arc_pack(const std::filesystem::path& source) {
    ARCInfoPack info;

    std::ifstream arcJsonFile(source / "arc.json");
    if (!arcJsonFile.is_open()) {
        throw std::runtime_error(std::string("Could not open ") + (source / "arc.json").string());
    }
    auto j = nlohmann::json::parse(arcJsonFile);

    info.sync_id_with_index = j.value("sync_id_with_index", true);
    for (auto& [k, v] : j["paths"].items()) {
        v["FileIndex"] = std::stoi(k);
        info.paths[v["path"]] = v;
    }

    info.rootName = j["root"];
    info.files.resize(j["paths"].size());
    info.originalPath = source;

    info.stringTable = std::string(".\0..\0", 5) + info.rootName;
    info.stringTable += '\0';
    info.nodes.push_back({truncateName("ROOT"), 5, computeNameHash(info.rootName), 0});
    info.nodeNameToIndex[info.rootName] = 0;

    int firstIndex = 0;
    size_t numRootEntires = arc_pack_recurse(source / info.rootName, info, firstIndex, -1);
    info.nodes[0].num_entries = numRootEntires;

    std::vector<u8> output(sizeof(SArcHeader) + sizeof(SArcDataInfo));

    u32 nodeOffset = output.size();
    output.resize(nodeOffset + sizeof(JKRArchive::SDIDirEntry) * info.nodes.size());
    memcpy(output.data() + nodeOffset, info.nodes.data(),
        sizeof(JKRArchive::SDIDirEntry) * info.nodes.size());

    u32 fileOffset = ALIGN_NEXT(output.size(),0x20);
    output.resize(fileOffset + sizeof(JKRArchive::SDIFileEntry) * info.files.size());
    memcpy(output.data() + fileOffset, info.files.data(),
        sizeof(JKRArchive::SDIFileEntry) * info.files.size());

    u32 stringTableOffset = ALIGN_NEXT(output.size(), 0x20);
    output.resize(stringTableOffset + info.stringTable.size());
    memcpy(output.data() + stringTableOffset, info.stringTable.data(), info.stringTable.size());

    u32 fileDataOffset = ALIGN_NEXT(output.size(), 0x20);
    output.resize(fileDataOffset + info.fileData.size());
    memcpy(output.data() + fileDataOffset, info.fileData.data(), info.fileData.size());

    if (info.sync_id_with_index) {
        info.maxFileId = info.files.size();
        if (info.nodes.size() == 1) {
            info.maxFileId -= 2; // Need to figure out the reason for this
        }
    }else{
        info.maxFileId += 1;
    }

    SArcDataInfo dataInfo = {};
    dataInfo.num_nodes = info.nodes.size();
    dataInfo.node_offset = 0x20;
    dataInfo.num_file_entries = info.files.size();
    dataInfo.file_entry_offset = fileOffset - 0x20;
    dataInfo.string_table_length = ALIGN_NEXT(info.stringTable.size(),0x20);
    dataInfo.string_table_offset = stringTableOffset - 0x20;
    dataInfo.next_free_file_id = info.maxFileId;
    dataInfo.sync_file_ids_and_indices = info.sync_id_with_index;

    memcpy(output.data() + 0x20, &dataInfo, sizeof(dataInfo));

    SArcHeader header = {};
    header.signature = 'RARC';
    header.file_length = output.size();
    header.header_length = 0x20;
    header.file_data_offset = fileDataOffset - 0x20;
    header.file_data_length = info.fileData.size();
    header.field_0x14 = info.mMemLength;
    header.field_0x18 = info.aMemLength;

    memcpy(output.data(), &header, sizeof(header));

    return output;
}

}  // namespace assets
