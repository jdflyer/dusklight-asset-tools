#pragma once

#include <dolphin/os/OSUtf.h>

namespace assets {

inline std::string sjis_to_utf8(const std::string& str) {
    std::string out;
    for (int i = 0; i < str.size(); i++) {
        if ((u8)str[i] < 0x80) {
            out.push_back(str[i]);
            continue;
        }
        u32 utf32 = OSSJIStoUTF32(((u8)str[i])<<8 | (u8)str[i+1]);
        i++;
        char utf8[5] = "\0\0\0\0";
        OSUTF32to8(utf32,utf8);
        out += utf8;
    }
    return out;
}

inline std::string utf8_to_sjis(const std::string& str) {
    std::string out;
    for (int i = 0; i < str.size();) {
        if ((u8)str[i] < 0x80) {
            out.push_back(str[i]);
            i++;
            continue;
        }
        u32 utf32;
        char* end = OSUTF8to32(&str[i],&utf32);
        u16 sjis = OSUTF32toSJIS(utf32);
        i += (end-&str[i]);
        out += (sjis>>8)&0xFF;
        out += sjis&0xFF;
    }
    return out;
}

}
