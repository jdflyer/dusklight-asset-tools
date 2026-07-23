#include "yaz0_compress.hpp"

void JKRDecomp::decode(u8* srcBuffer, u8* dstBuffer, u32 srcLength, u32 dstLength) {
    JKRCompression compression = checkCompressed(srcBuffer);
    if (compression == COMPRESSION_YAY0) {
        decodeSZP(srcBuffer, dstBuffer, srcLength, dstLength);
    } else if (compression == COMPRESSION_YAZ0) {
        decodeSZS(srcBuffer, dstBuffer, srcLength, dstLength);
    }
}

void JKRDecomp::decodeSZP(u8* src, u8* dst, u32 srcLength, u32 dstLength) {
    int srcChunkOffset;
    int count;
    int dstOffset;
    u32 length = srcLength;
    int linkInfo;
    int offset;
    int i;

    int decodedSize = READU32_BE(src, 4);
    int linkTableOffset = READU32_BE(src, 8);
    int srcDataOffset = READU32_BE(src, 12);

    dstOffset = 0;
    u32 counter = 0;
    srcChunkOffset = 16;

    u32 chunkBits;
    if (srcLength == 0)
        return;
    if (dstLength > decodedSize)
        return;

    do
    {
        if (counter == 0)
        {
            chunkBits = READU32_BE(src, srcChunkOffset);
            srcChunkOffset += sizeof(u32);
            counter = sizeof(u32) * 8;
        }

        if (chunkBits & 0x80000000)
        {
            if (dstLength == 0)
            {
                dst[dstOffset] = src[srcDataOffset];
                length--;
                if (length == 0)
                    return;
            }
            else
            {
                dstLength--;
            }
            dstOffset++;
            srcDataOffset++;
        }
        else
        {
            linkInfo = src[linkTableOffset] << 8 | src[linkTableOffset + 1];
            linkTableOffset += sizeof(u16);

            offset = dstOffset - (linkInfo & 0xFFF);
            count = (linkInfo >> 12);
            if (count == 0)
            {
                count = (u32)src[srcDataOffset++] + 0x12;
            }
            else
                count += 2;

            if (count > decodedSize - dstOffset)
                count = decodedSize - dstOffset;

            for (i = 0; i < count; i++, dstOffset++, offset++)
            {
                if (dstLength == 0)
                {
                    dst[dstOffset] = dst[offset - 1];
                    length--;
                    if (length == 0)
                        return;
                }
                else
                    dstLength--;
            }
        }

        chunkBits <<= 1;
        counter--;
    } while (dstOffset < decodedSize);
}

void JKRDecomp::decodeSZS(u8* src_buffer, u8* dst_buffer, u32 srcSize, u32 dstSize) {
    u8* decompEnd;
    u8* copyStart;
    s32 copyByteCount;
    s32 chunkBitsLeft = 0;
    s32 chunkBits;

    decompEnd = dst_buffer + JKRDecompExpandSize(src_buffer) - dstSize;

    if (srcSize == 0) {
        return;
    }
    if (dstSize > *(u32*)src_buffer) {
        return;
    }

    u8* curSrcPos = src_buffer + 0x10;
    do {
        if (chunkBitsLeft == 0) {
            chunkBits = curSrcPos[0];
            chunkBitsLeft = 8;
            curSrcPos++;
        }
        if ((chunkBits & 0x80) != 0) {
            if (dstSize == 0) {
                dst_buffer[0] = curSrcPos[0];
                srcSize--;
                dst_buffer++;
                if (srcSize == 0)
                    return;
            } else {
                dstSize--;
            }
            curSrcPos++;
        } else {
            u32 local_28 = curSrcPos[1] | (curSrcPos[0] & 0xF) << 8;
            u32 r31 = curSrcPos[0] >> 4;
            curSrcPos += 2;
            copyStart = dst_buffer - local_28;
            if (r31 == 0) {
                copyByteCount = curSrcPos[0] + 0x12;
                curSrcPos++;
            } else {
                copyByteCount = r31 + 2;
            }
            do {
                if (dstSize == 0) {
                    dst_buffer[0] = copyStart[-1];
                    srcSize--;
                    dst_buffer++;
                    if (srcSize == 0)
                        return;
                } else {
                    dstSize--;
                }
                copyByteCount--;
                copyStart++;
            } while (copyByteCount != 0);
        }
        chunkBits <<= 1;
        chunkBitsLeft--;
    } while (dst_buffer != decompEnd);
}

JKRCompression JKRDecomp::checkCompressed(u8* src) {
    if ((src[0] == 'Y') && (src[1] == 'a') && (src[3] == '0')) {
        if (src[2] == 'y') {
            return COMPRESSION_YAY0;
        }

        if (src[2] == 'z') {
            return COMPRESSION_YAZ0;
        }
    }

    if ((src[0] == 'A') && (src[1] == 'S') && (src[2] == 'R')) {
        return COMPRESSION_ASR;
    }

    return COMPRESSION_NONE;
}

namespace assets {

// The Following Algorithm is taken from the old decomp tools:
// https://github.com/zeldaret/tp/blob/dol2asm/tools/yaz0/yaz0.c
// Note: It is quite slow, a modern implementation like oead's should be considered:
// https://github.com/zeldamods/oead/blob/master/src/yaz0.cpp

constexpr size_t MAX_RUNLEN = (0xFF + 0x12);

struct Yaz0Header {
    char magic[4] = {'Y', 'a', 'z', '0'};
    BE(u32) uncompressedSize;
    BE(u32) alignment;
    u32 padding = 0;
};

static uint32_t simpleEnc(const std::span<const u8>& src, int pos, uint32_t *pMatchPos)
{
    int numBytes = 1;
    int matchPos = 0;

    int startPos = pos - 0x1000;
    int end = src.size() - pos;

    if (startPos < 0)
        startPos = 0;

    // maximum runlength for 3 byte encoding
    if (end > MAX_RUNLEN)
        end = MAX_RUNLEN;

    for (int i = startPos; i < pos; i++)
    {
        int j;

        for (j = 0; j < end; j++)
        {
            if (src[i + j] != src[j + pos])
                break;
        }
        if (j > numBytes)
        {
            numBytes = j;
            matchPos = i;
        }
    }

    *pMatchPos = matchPos;

    if (numBytes == 2)
        numBytes = 1;

    return numBytes;
}

struct nintendoEncState {
    uint32_t numBytes1 = 0;
    uint32_t matchPos = 0;
    int prevFlag = 0;
};

// a lookahead encoding scheme for ngc Yaz0
static uint32_t nintendoEnc(const std::span<const u8>& src, int pos, uint32_t *pMatchPos, nintendoEncState& state)
{
    uint32_t numBytes = 1;

    // if prevFlag is set, it means that the previous position
    // was determined by look-ahead try.
    // so just use it. this is not the best optimization,
    // but nintendo's choice for speed.
    if (state.prevFlag == 1)
    {
        *pMatchPos = state.matchPos;
        state.prevFlag = 0;
        return state.numBytes1;
    }

    state.prevFlag = 0;
    numBytes = simpleEnc(src, pos, &state.matchPos);
    *pMatchPos = state.matchPos;

    // if this position is RLE encoded, then compare to copying 1 byte and next position(pos+1) encoding
    if (numBytes >= 3)
    {
        state.numBytes1 = simpleEnc(src, pos + 1, &state.matchPos);
        // if the next position encoding is +2 longer than current position, choose it.
        // this does not guarantee the best optimization, but fairly good optimization with speed.
        if (state.numBytes1 >= numBytes + 2)
        {
            numBytes = 1;
            state.prevFlag = 1;
        }
    }
    return numBytes;
}

void yaz0_encode(const std::span<const u8>& src, std::vector<u8>& dst)
{
    int srcPos = 0;
    int bufPos = 0;
    nintendoEncState state;

    uint8_t buf[24]; // 8 codes * 3 bytes maximum

    uint32_t validBitCount = 0; // number of valid bits left in "code" byte
    uint8_t currCodeByte = 0; // a bitfield, set bits meaning copy, unset meaning RLE

    while (srcPos < src.size())
    {
        uint32_t numBytes;
        uint32_t matchPos;

        numBytes = nintendoEnc(src, srcPos, &matchPos, state);
        if (numBytes < 3)
        {
            // straight copy
            buf[bufPos] = src[srcPos];
            bufPos++;
            srcPos++;
            //set flag for straight copy
            currCodeByte |= (0x80 >> validBitCount);
        }
        else
        {
            //RLE part
            uint32_t dist = srcPos - matchPos - 1;
            uint8_t byte1, byte2, byte3;

            if (numBytes >= 0x12)  // 3 byte encoding
            {
                byte1 = 0 | (dist >> 8);
                byte2 = dist & 0xFF;
                buf[bufPos++] = byte1;
                buf[bufPos++] = byte2;
                // maximum runlength for 3 byte encoding
                if (numBytes > MAX_RUNLEN)
                    numBytes = MAX_RUNLEN;
                byte3 = numBytes - 0x12;
                buf[bufPos++] = byte3;
            }
            else  // 2 byte encoding
            {
                byte1 = ((numBytes - 2) << 4) | (dist >> 8);
                byte2 = dist & 0xFF;
                buf[bufPos++] = byte1;
                buf[bufPos++] = byte2;
            }
            srcPos += numBytes;
        }

        validBitCount++;

        // write eight codes
        if (validBitCount == 8)
        {
            dst.push_back(currCodeByte);
            for (int j = 0; j < bufPos; j++)
                dst.push_back(buf[j]);

            currCodeByte = 0;
            validBitCount = 0;
            bufPos = 0;
        }
    }

    if (validBitCount > 0)
    {
        dst.push_back(currCodeByte);
        for (int j = 0; j < bufPos; j++)
            dst.push_back(buf[j]);

    }else {
        dst.push_back(0);
    }
}

std::vector<u8> Yaz0Compress(const std::span<const u8>& src) {
    // Write the header.
    Yaz0Header header;
    header.uncompressedSize = u32(src.size());
    header.alignment = 0;

    std::vector<u8> result(sizeof(header));
    *(Yaz0Header*)result.data() = header;

    result.reserve(sizeof(header) + src.size());

    yaz0_encode(src,result); 

    // result.resize(ALIGN_NEXT(result.size(),0x20));

    return result;
}

}  // namespace assets
