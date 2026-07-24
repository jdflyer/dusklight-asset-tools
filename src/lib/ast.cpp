#include "ast.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include "JSystem/JAudio2/JASAramStream.h"
#include "dusk/audio/Adpcm.hpp"
#include "dusk/io.hpp"
#include "assets.hpp"

namespace assets {

struct WavChunk {
    char id[4];
    u32 size;
};

struct WAVFmt {
    WavChunk chunk = {{'f', 'm', 't', ' '}, 16};
    u16 AudioFormat = 1;  // 1 For PCM
    u16 NumChannels = 2;
    u32 SampleRate;
    u32 ByteRate;            // SampleRate * NumChannels * BitsPerSample/8
    u16 BlockAlign;          // NumChannels * BitsPerSample/8
    u16 BitsPerSample = 16;  // 8 bits = 8, 16 bits = 16, etc.
};

struct WAVData {
    WavChunk chunk = {{'d', 'a', 't', 'a'}};
};

struct WAVHeader {
    char ChunkID[4] = {'R', 'I', 'F', 'F'};
    u32 ChunkSize;  // File Size - 8
    char Format[4] = {'W', 'A', 'V', 'E'};
    WAVFmt fmt;
    WAVData data;
};

static constexpr u16 Coefficient0[] = {
    0,0x0800,0,0x0400,
    0x1000,0x0e00,0x0c00,0x1200,
    0x1068,0x12c0,0x1400,0x0800,
    0x0400,0xfc00,0xfc00,0xf800
};

static constexpr u16 Coefficient1[] = {
    0,0,0x0800,0x0400,0xf800,
    0xfa00,0xfc00,0xf600,0xf738,
    0xf704,0xf400,0xf800,0xfc00,
    0x0400,0,0,
};

static constexpr s16 Clamp16(s32 value) {
    if (value > 0x7FFF) return 0x7FFF;
    if (value < -0x8000) return -0x8000;
    return value;
}

std::string serializeAstFormat(u16 format) {
    switch (format) {
    case STREAM_FORMAT_ADPCM4:
        return "ADPCM";
    case STREAM_FORMAT_PCM16:
        return "PCM16";
    }
    return "Unknown";
}

u16 deserializeAstFormat(const std::string& s) {
    if (s == "ADPCM") {
        return STREAM_FORMAT_ADPCM4;
    }
    if (s == "PCM16") {
        return STREAM_FORMAT_PCM16;
    }
    return STREAM_FORMAT_PCM16;
}

const std::filesystem::path ast_unpack(
    const std::filesystem::path& outputName, const std::span<const u8>& buffer) {
    const JASAramStream::Header& header = *(JASAramStream::Header*)buffer.data();

    // assert(header.channels <= JASAramStream::CHANNEL_MAX);
    size_t offset = sizeof(JASAramStream::Header);
    std::vector<std::vector<s16> > channels(header.channels);
    for (auto& c : channels) {
        c.reserve(header.mSampleCount);
    }

    std::vector<std::pair<s16, s16> > last_penult_pair(header.channels);
    for (auto& c : last_penult_pair) {
        c = {0, 0};
    }

    while (offset < buffer.size()) {
        const JASAramStream::BlockHeader& block =
            *(JASAramStream::BlockHeader*)(buffer.data() + offset);
        offset += sizeof(block);

        for (int i = 0; i < header.channels; i++) {
            const std::span<const u8> block_data = buffer.subspan(offset, block.mSize);
            switch (header.format) {
            case STREAM_FORMAT_ADPCM4: {
                size_t numBlocks = (block.mSize + 8) / 9;
                size_t pcmLength = ((block.mSize - numBlocks) * 2);
                size_t channelOffset = channels[i].size();
                channels[i].resize(channelOffset + pcmLength);
                // s16 last = block.mAdpcmContinuationData[i].mpLast;
                // s16 penult = block.mAdpcmContinuationData[i].mpPenult;
                //  dusk::audio::Adpcm4ToPcm16(block_data.data(), block_data.size(),
                //     channels[i].data() + channelOffset, pcmLength, penult, last);
                // printf("File Last: %d Actual Last: %d File Penult: %d Real Penult
                // %d\n",last,last_penult_pair[i].second,penult,last_penult_pair[i].first);
                const u8* adpcm_block = block_data.data();
                size_t adpcm_size = block_data.size();
                std::vector<u8> blockCopy;
                if (adpcm_size % 9 != 0) {
                    // Copy the block to a new padded block so conversion can go nicely
                    adpcm_size = adpcm_size + (9 - (adpcm_size % 9));
                    blockCopy.resize(adpcm_size);
                    memcpy(blockCopy.data(), adpcm_block, block_data.size());
                    adpcm_block = blockCopy.data();
                }
                dusk::audio::Adpcm4ToPcm16(adpcm_block, adpcm_size,
                    channels[i].data() + channelOffset, pcmLength, last_penult_pair[i].first,
                    last_penult_pair[i].second);
                break;
            }
            case STREAM_FORMAT_PCM16: {
                size_t numSamples = block.mSize / 2;
                channels[i].reserve(channels[i].size() + numSamples);
                for (int j = 0; j < numSamples; j++) {
                    s16 sample = (block_data[j*2]<<8) | block_data[(j*2)+1];
                    channels[i].push_back(sample);
                }
                break;
            }
            }
            offset += block.mSize;
        }
    }

    for (auto& c : channels) {
        // Truncate to the correct sample count
        c.resize(header.mSampleCount);
    }

    std::filesystem::create_directories(outputName);
    std::filesystem::path wavName = outputName / (outputName.stem().string() + ".wav");
    std::filesystem::path jsonName = outputName / (outputName.stem().string() + ".json");

    size_t numSamples = channels[0].size();

    std::vector<u8> wavBuffer(sizeof(WAVHeader));
    {
        WAVHeader& wavHeader = *(WAVHeader*)wavBuffer.data();
        wavHeader = WAVHeader();
        wavHeader.fmt.SampleRate = header.mSampleRate;
        wavHeader.fmt.ByteRate = header.mSampleRate * header.channels * 2;
        wavHeader.fmt.BlockAlign = header.channels * 2;
        wavHeader.data.chunk.size = numSamples * header.channels * 2;
        wavHeader.ChunkSize = wavHeader.data.chunk.size + sizeof(wavHeader) - 8;
        wavBuffer.reserve(wavBuffer.size() + wavHeader.data.chunk.size);
    }

    for (int sample = 0; sample < channels[0].size(); sample++) {
        for (int channel = 0; channel < channels.size(); channel++) {
            s16 s = channels[channel][sample];
            wavBuffer.push_back(s & 0xFF);
            wavBuffer.push_back(s >> 8);
        }
    }

    auto wavFs = dusk::io::FileStream::Create(wavName);
    fs_writeBuf(wavFs, wavBuffer);

    nlohmann::ordered_json j;
    j["tool_version"] = 1;
    j["format"] = serializeAstFormat(header.format);
    j["loop"] = (bool)(header.loop > 0);
    j["loop_start"] = header.loop_start.host();
    j["loop_end"] = header.loop_end.host();
    j["block_size"] = header.block_size.host();
    j["volume"] = header.mVolume;

    auto jsonFs = dusk::io::FileStream::Create(jsonName);
    fs_writeString(jsonFs, j.dump(4));

    return outputName;
}

bool tryRecoverAdpcm(
    const std::array<s16, 16>& samples, s16& penult, s16& last, std::array<u8, 9>& outBlock) {
    for (int coefIdx = 0; coefIdx < 16; coefIdx++) {
        s16 coef1 = Coefficient0[coefIdx];
        s16 coef2 = Coefficient1[coefIdx];

        for (int scale = 0; scale <= 15; scale++) {
            s16 h1 = last;
            s16 h2 = penult;
            std::array<int, 16> nibbles = {};

            bool ok = true;
            for (int i = 0; i < samples.size() && ok; i++) {
                int prediction = (coef1 * h1 + coef2 * h2) >> 11;
                int diff = samples[i] - prediction;

                int found = std::numeric_limits<int>::min();
                for (int n = -8; n <= 7; n++) {
                    s16 decoded = Clamp16((n << scale) + prediction);
                    if (decoded == samples[i]) {
                        found = n;
                        break;
                    }
                }
                if (found == std::numeric_limits<int>::min()) {
                    ok = false;
                    break;
                }

                nibbles[i] = found;
                h2 = h1;
                h1 = samples[i];
            }

            if (ok) {
                outBlock[0] = (u8)((scale << 4) | (coefIdx&0xF));
                for (int i = 0; i < 8; i++) {
                    u8 hi = (u8)(nibbles[i*2] & 0xF);
                    u8 lo = (u8)(nibbles[i*2 + 1] & 0xF);
                    outBlock[i+1] = (hi<<4) | lo;
                }
                penult = h2;
                last = h1;
                // putchar('o');
                return true;
            }
        }
    }
    // putchar('n');
    return false;
}

std::array<u8, 9> encodeADPCM(const std::array<s16,16>& samples, s16& penult, s16& last) {
    std::array<u8, 9> encodedBlock = {};

    // First, try to recover the original adpcm data, if we can't, encode it as normal
    if (tryRecoverAdpcm(samples, penult, last, encodedBlock)) {
        return encodedBlock;
    }

    int64_t bestError = std::numeric_limits<int64_t>::max();
    s16 bestPenult = 0;
    s16 bestLast = 0;

    for (int coefIdx = 0; coefIdx < 16; coefIdx++) {
        s16 coef1 = Coefficient0[coefIdx];
        s16 coef2 = Coefficient1[coefIdx];

        for (int scale = 0; scale <= 12; scale++) {
            s16 h1 = last;
            s16 h2 = penult;
            std::array<int, 16> nibbles = {};
            int64_t error = 0;

            for (int i = 0; i < samples.size(); i++) {
                int prediction = (coef1 * h1 + coef2 * h2) >> 11;
                int diff = samples[i] - prediction;

                int round = scale > 0 ? (1 << (scale - 1)) : 0;
                int n = (diff >= 0) ? (diff + round) >> scale : -(((-diff) + round) >> scale);
                n = std::clamp(n, -8, 7);

                s16 decoded = Clamp16((n << scale) + prediction);
                int64_t e = (int64_t)samples[i] - decoded;
                error += e * e;

                nibbles[i] = n;
                h2 = h1;
                h1 = decoded;
            }

            if (error < bestError) {
                bestError = error;
                encodedBlock[0] = (u8)((scale << 4) | (coefIdx&0xF));
                for (int i = 0; i < 8; i++) {
                    u8 hi = (u8)(nibbles[i * 2] & 0xF);
                    u8 lo = (u8)(nibbles[i * 2 + 1] & 0xF);
                    encodedBlock[i+1] = (hi << 4) | lo;
                }
                
                bestPenult = h2;
                bestLast = h1;
            }
        }
    }

    penult = bestPenult;
    last = bestLast;

    return encodedBlock;
}

const std::vector<u8> ast_pack(const std::filesystem::path& source) {
    const auto wavPath = source / (source.stem().string() + ".wav");
    const auto jsonPath = source / (source.stem().string() + ".json");

    // Get the data from the json
    std::ifstream astJsonFile(jsonPath);
    if (!astJsonFile.is_open()) {
        throw std::runtime_error(std::string("Could not open ") + jsonPath.string());
    }
    auto j = nlohmann::json::parse(astJsonFile);

    const std::vector<u8> wavBuffer = dusk::io::FileStream::ReadAllBytes(wavPath);

    WAVFmt fmt;
    std::span<const u8> dataBuffer;

    size_t offset = 0;
    // Parse the wav file, extracting the fmt and data chunks for our use
    while (offset < wavBuffer.size()) {
        const WavChunk& chunk = *(WavChunk*)(wavBuffer.data() + offset);
        u32 id = chunk.id[0] << 24 | chunk.id[1] << 16 | chunk.id[2] << 8 | chunk.id[3];
        switch (id) {
        case 'RIFF':
            offset += 12;
            break;
        case 'fmt ':
            fmt = *(WAVFmt*)(wavBuffer.data() + offset);
            offset += chunk.size + 8;
            break;
        case 'data':
            dataBuffer = std::span<const u8>(
                wavBuffer.begin() + offset + 8, wavBuffer.begin() + offset + 8 + chunk.size);
            offset += chunk.size + 8;
            break;
        default:
            offset += chunk.size + 8;
            break;
        }
    }

    u32 blockSize = j.value("block_size", 10080);

    JASAramStream::Header astHeader = {};
    astHeader.tag = 'STRM';

    astHeader.format = deserializeAstFormat(j["format"]);
    astHeader.bits = 16;
    astHeader.channels = fmt.NumChannels;
    astHeader.loop = (bool)j["loop"] ? 0xFFFF : 0;
    astHeader.mSampleRate = fmt.SampleRate;
    astHeader.block_size = blockSize;
    astHeader.mVolume = (u8)j["volume"];

    std::vector<u8> outBuffer(sizeof(JASAramStream::Header));

    std::vector<std::pair<s16, s16> > last_penult_pair(astHeader.channels);
    for (auto& c : last_penult_pair) {
        c = {0, 0};
    }

    offset = 0;
    size_t sampleCount = 0;
    while (offset < dataBuffer.size()) {
        size_t written = 0;
        std::vector<std::vector<u8> > blockBuffers(fmt.NumChannels);
        for (auto& c : blockBuffers) {
            c.reserve(blockSize);
        }

        size_t fullBlockLen = (fmt.NumChannels * blockSize);
        while (written < fullBlockLen && offset < dataBuffer.size()) {
            size_t frameStart = offset;
            for (int c = 0; c < fmt.NumChannels; c++) {
                switch (astHeader.format) {
                case STREAM_FORMAT_ADPCM4: {
                    std::array<s16, 16> samples = {};
                    // std::vector<s16> samples;
                    size_t readOffset = frameStart + ((size_t)c *2);
                    for (int i = 0; i < 16 && readOffset < dataBuffer.size(); i++) {
                        s16 sample = dataBuffer[readOffset + 1] << 8 | dataBuffer[readOffset];
                        readOffset += 2*fmt.NumChannels;
                        samples[i] = sample;
                        // samples.push_back(sample);
                    }
                    std::array<u8,9> adpcm = encodeADPCM(samples, last_penult_pair[c].second, last_penult_pair[c].first);
                    blockBuffers[c].insert(blockBuffers[c].end(), adpcm.begin(), adpcm.end());
                    // last_penult_pair[c].second = last_penult_pair[c].first;
                    // last_penult_pair[c].first = samples[15];
                    written += 9;
                    sampleCount += 16;
                    break;
                }
                case STREAM_FORMAT_PCM16:
                    s16 sample = dataBuffer[offset + 1] << 8 | dataBuffer[offset];
                    offset += 2;
                    blockBuffers[c].push_back(sample >> 8);
                    blockBuffers[c].push_back(sample & 0xFF);
                    written += 2;
                    sampleCount++;
                    break;
                }
            }
            if (astHeader.format == STREAM_FORMAT_ADPCM4) {
                offset += (16*2*fmt.NumChannels);
            }
        }

        size_t blockLen = ALIGN_NEXT(written / fmt.NumChannels,0x20);
        size_t headerOffset = outBuffer.size();
        outBuffer.resize(headerOffset + sizeof(JASAramStream::BlockHeader));
        JASAramStream::BlockHeader& header =
            *(JASAramStream::BlockHeader*)(outBuffer.data() + headerOffset);
        header = {};
        header.tag = 'BLCK';
        header.mSize = blockLen;

        for (int c = 0; c < fmt.NumChannels; c++) {
            header.mAdpcmContinuationData[c].mpLast = last_penult_pair[c].first;
            header.mAdpcmContinuationData[c].mpPenult = last_penult_pair[c].second;
        }

        for (auto& c : blockBuffers) {
            outBuffer.insert(outBuffer.end(), c.begin(), c.end());
            outBuffer.resize(ALIGN_NEXT(outBuffer.size(),0x20));
        }
    }

    outBuffer.resize(ALIGN_NEXT(outBuffer.size(),0x20));

    astHeader.soundBlockSize = outBuffer.size() - sizeof(JASAramStream::Header);
    astHeader.mSampleCount = sampleCount / astHeader.channels;
    astHeader.loop_start = (int)j["loop_start"];
    if (astHeader.loop != 0) {
        astHeader.loop_end = (int)j["loop_end"];
    }else {
        astHeader.loop_end = astHeader.mSampleCount.host();
    }

    *(JASAramStream::Header*)outBuffer.data() = astHeader;

    return outBuffer;
}
};  // namespace assets
