#include "ReplayWriter.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include "LzmaEnc.h"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

// LZMA allocator
static void *SzAlloc(ISzAllocPtr p, size_t size) { (void)p; return malloc(size); }
static void SzFree(ISzAllocPtr p, void *address) { (void)p; free(address); }
static const ISzAlloc g_Alloc = { SzAlloc, SzFree };

// Get current time as .NET DateTime ticks
static int64_t getOsuTimestamp() {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    // FILETIME: 100-ns intervals since 1601-01-01
    // .NET ticks: 100-ns intervals since 0001-01-01
    // Offset: 504911232000000000 ticks (1600 years)
    return uli.QuadPart + 504911232000000000LL;
#else
    return 0;
#endif
}

void ReplayWriter::writeByte(std::vector<uint8_t>& data, uint8_t value) {
    data.push_back(value);
}

void ReplayWriter::writeInt16(std::vector<uint8_t>& data, int16_t value) {
    data.push_back(value & 0xFF);
    data.push_back((value >> 8) & 0xFF);
}

void ReplayWriter::writeInt32(std::vector<uint8_t>& data, int32_t value) {
    data.push_back(value & 0xFF);
    data.push_back((value >> 8) & 0xFF);
    data.push_back((value >> 16) & 0xFF);
    data.push_back((value >> 24) & 0xFF);
}

void ReplayWriter::writeInt64(std::vector<uint8_t>& data, int64_t value) {
    for (int i = 0; i < 8; i++) {
        data.push_back((value >> (i * 8)) & 0xFF);
    }
}

void ReplayWriter::writeOsuString(std::vector<uint8_t>& data, const std::string& str) {
    if (str.empty()) {
        writeByte(data, 0x00);
        return;
    }
    writeByte(data, 0x0b);
    // Write ULEB128 length
    size_t len = str.length();
    while (len >= 0x80) {
        data.push_back((len & 0x7F) | 0x80);
        len >>= 7;
    }
    data.push_back(len & 0x7F);
    // Write string bytes
    for (char c : str) {
        data.push_back(static_cast<uint8_t>(c));
    }
}

bool ReplayWriter::compressLZMA(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
    CLzmaEncProps props;
    LzmaEncProps_Init(&props);
    props.level = 5;
    props.dictSize = 1 << 16;

    size_t propsSize = 5;
    uint8_t propsEncoded[5];

    size_t destLen = input.size() + input.size() / 3 + 128;
    std::vector<uint8_t> dest(destLen);

    SRes res = LzmaEncode(dest.data(), &destLen,
                          input.data(), input.size(),
                          &props, propsEncoded, &propsSize,
                          0, nullptr, &g_Alloc, &g_Alloc);

    if (res != SZ_OK) return false;

    // LZMA header: 5 bytes props + 8 bytes uncompressed size + compressed data
    output.clear();
    for (int i = 0; i < 5; i++) output.push_back(propsEncoded[i]);
    uint64_t uncompSize = input.size();
    for (int i = 0; i < 8; i++) output.push_back((uncompSize >> (i * 8)) & 0xFF);
    output.insert(output.end(), dest.begin(), dest.begin() + destLen);

    return true;
}

bool ReplayWriter::write(const std::string& filepath,
                         const std::string& beatmapHash,
                         const std::string& playerName,
                         int keyCount,
                         const int* judgeCounts,
                         int maxCombo,
                         int score,
                         int mods,
                         const std::vector<ReplayFrame>& frames) {
    std::vector<uint8_t> data;

    // Header
    writeByte(data, 3);  // gameMode = mania
    writeInt32(data, 20220101);  // gameVersion
    writeOsuString(data, beatmapHash);
    writeOsuString(data, playerName);
    writeOsuString(data, "");  // replayHash

    // Counts: 300, 100, 50, geki(300g), katu(200), miss
    writeInt16(data, judgeCounts[1]);  // 300
    writeInt16(data, judgeCounts[3]);  // 100
    writeInt16(data, judgeCounts[4]);  // 50
    writeInt16(data, judgeCounts[0]);  // 300g
    writeInt16(data, judgeCounts[2]);  // 200
    writeInt16(data, judgeCounts[5]);  // miss

    writeInt32(data, score);  // totalScore
    writeInt16(data, maxCombo);
    writeByte(data, judgeCounts[5] == 0 ? 1 : 0);  // perfectCombo
    writeInt32(data, mods);  // mods

    writeOsuString(data, "");  // lifeBar
    writeInt64(data, getOsuTimestamp());  // timestamp

    // Build frame string
    std::ostringstream frameStr;
    int64_t lastTime = 0;
    for (const auto& frame : frames) {
        int64_t w = frame.time - lastTime;
        frameStr << w << "|" << frame.keyState << "|0|0,";
        lastTime = frame.time;
    }
    frameStr << "-12345|0|0|0";  // End marker

    std::string frameData = frameStr.str();
    std::vector<uint8_t> uncompressed(frameData.begin(), frameData.end());
    std::vector<uint8_t> compressed;

    if (!compressLZMA(uncompressed, compressed)) {
        return false;
    }

    writeInt32(data, compressed.size());
    data.insert(data.end(), compressed.begin(), compressed.end());

    // Online score ID (required field at end of file)
    writeInt64(data, 0);

    // Write to file
    std::ofstream file(filepath, std::ios::binary);
    if (!file) return false;
    file.write((char*)data.data(), data.size());
    return true;
}
