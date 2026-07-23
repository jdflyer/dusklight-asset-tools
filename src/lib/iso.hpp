#pragma once
#include <filesystem>
#include "assets.hpp"
#include "assets_unpack.hpp"

namespace assets {

constexpr u32 isoSize = 1459978240;

struct DVDEntry_BE {
    u8 isDir;
    u8 fileNameOffset[3];
    BE(u32) entryOffset;
    BE(u32) entryLength;

    std::string getName(const std::span<const u8>& stringTable) const {
        u32 strTableOffset =
            (fileNameOffset[0] << 16) | (fileNameOffset[1] << 8) | (fileNameOffset[2]);
        return std::string((const char*)stringTable.subspan(strTableOffset).data());
    }
    void setNameOffset(u32 offset) {
        fileNameOffset[2] = offset&0xFF;
        fileNameOffset[1] = (offset>>8)&0xFF;
        fileNameOffset[0] = (offset>>16)&0xFF;
    }
};

struct DVDBB2_BE {
    /* 0x00 */ BE(u32) bootFilePosition;  // dol Offset on Disk (0x2440 + apploaderSize +
                                          // trailerSize aligned up by 0x100 bytes)
    /* 0x04 */ BE(u32) FSTPosition;   // bootFilePosition + dolSize aligned to the next 0x100 bytes
    /* 0x08 */ BE(u32) FSTLength;     // Length of the FST
    /* 0x0C */ BE(u32) FSTMaxLength;  // == FSTLength
    /* 0x10 */ BE(u32) FSTAddress;    // 0x8040000 - FSTLength aligned down by 0x20
    /* 0x14 */ BE(u32) userPosition;  // FSTPosition + FSTLength aligned to the next 0x1000 bytes
    /* 0x18 */ BE(u32) userLength;    // isoSize - userPosition
    /* 0x1C */ BE(u32) padding0;
};

struct DVDDiskID_JSON {
    char gameName[4];
    char company[2];
    u8 diskNumber;
    u8 gameVersion;
    u8 streaming;
    u8 streamingBufSize;
    u8 padding[18];
    BE(u32) dvdMagic;
    char gameNameFull[992];
    BE(u32) debugMonitorOffset;
    BE(u32) debugMonitorAddr;
    u8 padding2[0x18];

    inline nlohmann::ordered_json to_json() const {
        return {{"tool_version", 1}, {"gameName", std::string(gameName, 4)},
            {"company", std::string(company, 2)}, {"diskNumber", diskNumber},
            {"gameVersion", gameVersion}, {"streaming", streaming},
            {"streamingBufSize", toHex(streamingBufSize)}, {"dvdMagic", toHex(dvdMagic)},
            {"gameNameFull", std::string(gameNameFull)},
            {"debugMonitorOffset", toHex(debugMonitorOffset)},
            {"debugMonitorAddr", toHex(debugMonitorAddr)}};
    }
    static inline DVDDiskID_JSON from_json(const nlohmann::json& j) {
        DVDDiskID_JSON d = {};

        strncpy(d.gameName, getKeyString(j, "gameName").c_str(), 4);
        strncpy(d.company, getKeyString(j, "company").c_str(), 2);
        strncpy(d.gameNameFull, getKeyString(j, "gameNameFull").c_str(), 992);

        j.at("diskNumber").get_to(d.diskNumber);
        j.at("gameVersion").get_to(d.gameVersion);
        j.at("streaming").get_to(d.streaming);

        d.streamingBufSize = getKeyHex(j,"streamingBufSize");
        d.dvdMagic = getKeyHex(j, "dvdMagic");
        d.debugMonitorOffset = getKeyHex(j, "debugMonitorOffset");
        d.debugMonitorAddr = getKeyHex(j, "debugMonitorAddr");

        return d;
    }
};

const std::filesystem::path iso_unpack(const std::filesystem::path& outputName,
    const std::span<const u8>& buffer);
const std::vector<u8> boot_bin_pack(const std::filesystem::path& source);
const std::vector<u8> iso_pack(const std::filesystem::path& source);

}  // namespace assets
