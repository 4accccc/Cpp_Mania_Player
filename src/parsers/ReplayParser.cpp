#include "ReplayParser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <set>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

// LZMA decoder
#include "LzmaDec.h"

// Custom allocator for LZMA
static void *SzAlloc(ISzAllocPtr p, size_t size) { (void)p; return malloc(size); }
static void SzFree(ISzAllocPtr p, void *address) { (void)p; free(address); }
static const ISzAlloc g_Alloc = { SzAlloc, SzFree };

uint8_t ReplayParser::readByte(const uint8_t* data, size_t& offset) {
    return data[offset++];
}

int16_t ReplayParser::readInt16(const uint8_t* data, size_t& offset) {
    int16_t value = data[offset] | (data[offset + 1] << 8);
    offset += 2;
    return value;
}

int32_t ReplayParser::readInt32(const uint8_t* data, size_t& offset) {
    int32_t value = data[offset] | (data[offset + 1] << 8) |
                    (data[offset + 2] << 16) | (data[offset + 3] << 24);
    offset += 4;
    return value;
}

int64_t ReplayParser::readInt64(const uint8_t* data, size_t& offset) {
    int64_t value = 0;
    for (int i = 0; i < 8; i++) {
        value |= ((int64_t)data[offset + i]) << (i * 8);
    }
    offset += 8;
    return value;
}

uint64_t ReplayParser::readULEB128(const uint8_t* data, size_t& offset) {
    uint64_t result = 0;
    int shift = 0;
    uint8_t byte;
    do {
        byte = data[offset++];
        result |= ((uint64_t)(byte & 0x7F)) << shift;
        shift += 7;
    } while (byte & 0x80);
    return result;
}

std::string ReplayParser::readOsuString(const uint8_t* data, size_t& offset) {
    uint8_t exists = readByte(data, offset);
    if (exists == 0x00) {
        return "";
    }
    if (exists != 0x0b) {
        return "";
    }
    uint64_t length = readULEB128(data, offset);
    std::string result((char*)&data[offset], length);
    offset += length;
    return result;
}

bool ReplayParser::decompressLZMA(const uint8_t* compressed, size_t compressedSize,
                                   std::vector<uint8_t>& decompressed) {
    if (compressedSize < 13) return false;

    // LZMA header: 5 bytes props + 8 bytes uncompressed size
    const uint8_t* props = compressed;
    uint64_t uncompressedSize = 0;
    for (int i = 0; i < 8; i++) {
        uncompressedSize |= ((uint64_t)compressed[5 + i]) << (i * 8);
    }

    if (uncompressedSize > 100 * 1024 * 1024) {
        return false;  // Sanity check: max 100MB
    }

    decompressed.resize((size_t)uncompressedSize);

    size_t destLen = (size_t)uncompressedSize;
    size_t srcLen = compressedSize - 13;

    ELzmaStatus status;
    SRes res = LzmaDecode(
        decompressed.data(), &destLen,
        compressed + 13, &srcLen,
        props, 5,
        LZMA_FINISH_END, &status, &g_Alloc
    );

    return res == SZ_OK;
}

void ReplayParser::parseFrames(const std::string& data, std::vector<ReplayFrame>& frames, int& seedOut, int64_t& scoreIdOut) {
    frames.clear();
    seedOut = 0;
    scoreIdOut = 0;
    int64_t currentTime = 0;
    bool isFirstFrame = true;

    std::istringstream stream(data);
    std::string frame;

    while (std::getline(stream, frame, ',')) {
        if (frame.empty()) continue;

        int64_t w = 0;
        float x = 0, y = 0;

        size_t pos1 = frame.find('|');
        if (pos1 == std::string::npos) continue;

        w = std::stoll(frame.substr(0, pos1));

        size_t pos2 = frame.find('|', pos1 + 1);
        if (pos2 == std::string::npos) continue;

        x = std::stof(frame.substr(pos1 + 1, pos2 - pos1 - 1));

        size_t pos3 = frame.find('|', pos2 + 1);
        if (pos3 != std::string::npos) {
            y = std::stof(frame.substr(pos2 + 1, pos3 - pos2 - 1));

            // Parse the 4th value (key state or score ID for end marker)
            if (w == -12345) {
                // End marker: -12345|0|0|scoreId
                std::string scoreIdStr = frame.substr(pos3 + 1);
                if (!scoreIdStr.empty()) {
                    scoreIdOut = std::stoll(scoreIdStr);
                }
                continue;
            }
        }

        // w = -12345 is the end marker, skip it
        if (w == -12345) continue;

        // Save seed from first frame (w=0)
        if (isFirstFrame && w == 0) {
            seedOut = static_cast<int>(x);
        }
        isFirstFrame = false;

        currentTime += w;

        // Store ALL frames including negative time
        frames.push_back({currentTime, static_cast<int>(x), x, y});
    }
}

bool ReplayParser::parse(const std::string& filepath, ReplayInfo& info) {
#ifdef _WIN32
    // Convert UTF-8 path to wide string for Windows
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, nullptr, 0);
    std::wstring widePath(wideLen - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, &widePath[0], wideLen);
    std::ifstream file(widePath, std::ios::binary);
#else
    std::ifstream file(filepath, std::ios::binary);
#endif
    if (!file) {
        std::cerr << "Failed to open replay file: " << filepath << std::endl;
        return false;
    }

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(fileSize);
    file.read((char*)buffer.data(), fileSize);
    file.close();

    size_t offset = 0;
    const uint8_t* data = buffer.data();

    // Read header
    info.gameMode = readByte(data, offset);
    info.gameVersion = readInt32(data, offset);
    info.beatmapHash = readOsuString(data, offset);
    info.playerName = readOsuString(data, offset);
    info.replayHash = readOsuString(data, offset);

    // osu! format: count300, count100, count50, countGeki(300g), countKatu(200), countMiss
    info.count300 = readInt16(data, offset);
    info.count100 = readInt16(data, offset);
    info.count50 = readInt16(data, offset);
    info.count300g = readInt16(data, offset);  // Geki = 300g in mania
    info.count200 = readInt16(data, offset);   // Katu = 200 in mania
    info.countMiss = readInt16(data, offset);

    info.totalScore = readInt32(data, offset);
    info.maxCombo = readInt16(data, offset);
    info.perfectCombo = readByte(data, offset) != 0;
    info.mods = readInt32(data, offset);

    // Skip life bar graph
    readOsuString(data, offset);

    info.timestamp = readInt64(data, offset);

    // Debug output
    std::cout << "Replay: " << info.playerName << std::endl;
    std::cout << "GameMode: " << info.gameMode << std::endl;
    std::cout << "Mods: " << info.mods << " (0x" << std::hex << info.mods << std::dec << ")" << std::endl;
    std::cout << "300g/300/200/100/50/Miss: " << info.count300g << "/" << info.count300
              << "/" << info.count200 << "/" << info.count100 << "/" << info.count50
              << "/" << info.countMiss << std::endl;

    // Read compressed data
    int32_t compressedLength = readInt32(data, offset);
    if (compressedLength <= 0 || offset + compressedLength > fileSize) {
        std::cerr << "Invalid compressed data length" << std::endl;
        return false;
    }

    std::vector<uint8_t> decompressed;
    if (!decompressLZMA(data + offset, compressedLength, decompressed)) {
        std::cerr << "Failed to decompress replay data" << std::endl;
        return false;
    }
    offset += compressedLength;

    std::string frameData(decompressed.begin(), decompressed.end());
    parseFrames(frameData, info.frames, info.seed, info.onlineScoreId);

    // Read OnlineScoreID from file end (if available)
    if (offset + 8 <= fileSize) {
        info.onlineScoreId = readInt64(data, offset);
    }

    // Read Target mode extra data (if Target mod is enabled)
    const int TargetMod = 8388608;
    info.targetAccuracy = 0.0;
    if ((info.mods & TargetMod) && offset + 8 <= fileSize) {
        // Read as double (8 bytes)
        uint64_t raw = 0;
        for (int i = 0; i < 8; i++) {
            raw |= ((uint64_t)data[offset + i]) << (i * 8);
        }
        offset += 8;
        memcpy(&info.targetAccuracy, &raw, sizeof(double));
        std::cout << "Target mode accuracy value: " << info.targetAccuracy << std::endl;
    }

    std::cout << "Replay loaded: " << info.playerName << std::endl;
    std::cout << "Frames: " << info.frames.size() << std::endl;

    return true;
}

std::string ReplayParser::calculateReplayHash(const ReplayInfo& info) {
#ifdef _WIN32
    // Formula: MD5(MaxCombo + "osu" + PlayerName + BeatmapHash + TotalScore + Grade + Timestamp)

    // Calculate Grade
    int totalHits = info.count300 + info.count100 + info.count50 + info.countMiss;
    double accuracy = 0;
    if (totalHits > 0) {
        accuracy = (double)(info.count300 * 300 + info.count100 * 100 + info.count50 * 50) / (totalHits * 300);
    }

    std::string grade;
    if (accuracy == 1.0) grade = "SS";
    else if (accuracy > 0.93) grade = "S";
    else if (accuracy > 0.8) grade = "A";
    else if (accuracy > 0.7) grade = "B";
    else if (accuracy > 0.6) grade = "C";
    else grade = "D";

    // Convert timestamp to DateTime string
    // Windows FILETIME: 从1601年1月1日起的100纳秒间隔数
    FILETIME ft;
    ft.dwLowDateTime = (DWORD)(info.timestamp & 0xFFFFFFFF);
    ft.dwHighDateTime = (DWORD)(info.timestamp >> 32);
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);

    std::ostringstream timeStr;
    timeStr << st.wYear << "/" << st.wMonth << "/" << st.wDay << " "
            << st.wHour << ":" << std::setfill('0') << std::setw(2) << st.wMinute << ":"
            << std::setfill('0') << std::setw(2) << st.wSecond;

    std::ostringstream ss;
    ss << info.maxCombo << "osu" << info.playerName << info.beatmapHash
       << info.totalScore << grade << timeStr.str();

    std::string data = ss.str();

    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    std::string result;

    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
            if (CryptHashData(hHash, (BYTE*)data.data(), data.size(), 0)) {
                BYTE hash[16];
                DWORD hashLen = 16;
                if (CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0)) {
                    std::ostringstream oss;
                    for (int i = 0; i < 16; i++) {
                        oss << std::hex << std::setfill('0') << std::setw(2) << (int)hash[i];
                    }
                    result = oss.str();
                }
            }
            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
    }
    return result;
#else
    return "";
#endif
}

// Write methods
void ReplayParser::writeByte(std::vector<uint8_t>& buffer, uint8_t value) {
    buffer.push_back(value);
}

void ReplayParser::writeInt16(std::vector<uint8_t>& buffer, int16_t value) {
    buffer.push_back(value & 0xFF);
    buffer.push_back((value >> 8) & 0xFF);
}

void ReplayParser::writeInt32(std::vector<uint8_t>& buffer, int32_t value) {
    buffer.push_back(value & 0xFF);
    buffer.push_back((value >> 8) & 0xFF);
    buffer.push_back((value >> 16) & 0xFF);
    buffer.push_back((value >> 24) & 0xFF);
}

void ReplayParser::writeInt64(std::vector<uint8_t>& buffer, int64_t value) {
    for (int i = 0; i < 8; i++) {
        buffer.push_back((value >> (i * 8)) & 0xFF);
    }
}

void ReplayParser::writeULEB128(std::vector<uint8_t>& buffer, uint64_t value) {
    do {
        uint8_t byte = value & 0x7F;
        value >>= 7;
        if (value != 0) byte |= 0x80;
        buffer.push_back(byte);
    } while (value != 0);
}

void ReplayParser::writeOsuString(std::vector<uint8_t>& buffer, const std::string& str) {
    writeByte(buffer, 0x0b);
    writeULEB128(buffer, str.length());
    if (!str.empty()) {
        buffer.insert(buffer.end(), str.begin(), str.end());
    }
}

// Include LZMA encoder
#include "LzmaEnc.h"

bool ReplayParser::compressLZMA(const std::vector<uint8_t>& data, std::vector<uint8_t>& compressed) {
    if (data.empty()) {
        compressed.clear();
        return true;
    }

    CLzmaEncProps props;
    LzmaEncProps_Init(&props);
    props.level = 5;
    props.dictSize = 1 << 21;  // 2MB dictionary (osu! stable format)

    uint8_t propsEncoded[LZMA_PROPS_SIZE];
    size_t propsSize = LZMA_PROPS_SIZE;

    size_t destLen = data.size() + data.size() / 3 + 128;
    std::vector<uint8_t> dest(destLen);

    SRes res = LzmaEncode(
        dest.data(), &destLen,
        data.data(), data.size(),
        &props, propsEncoded, &propsSize,
        1, nullptr, &g_Alloc, &g_Alloc  // writeEndMark = 1
    );

    if (res != SZ_OK) return false;

    // LZMA header: 5 bytes props + 8 bytes uncompressed size + data
    compressed.clear();
    compressed.insert(compressed.end(), propsEncoded, propsEncoded + 5);

    // Use actual uncompressed size (osu! stable format)
    uint64_t uncompSize = data.size();
    for (int i = 0; i < 8; i++) {
        compressed.push_back((uncompSize >> (i * 8)) & 0xFF);
    }

    compressed.insert(compressed.end(), dest.begin(), dest.begin() + destLen);
    return true;
}

std::string ReplayParser::serializeFrames(const std::vector<ReplayFrame>& frames, int seed, int64_t scoreId) {
    std::ostringstream ss;
    int64_t lastTime = 0;

    // Debug: print frames around index 110
    std::cout << "[Serialize] frames.size()=" << frames.size() << std::endl;
    std::cout << "[Serialize] First 5 frames:" << std::endl;
    for (size_t i = 0; i < 5 && i < frames.size(); i++) {
        std::cout << "[Serialize]   frames[" << i << "]: x=" << frames[i].x << ", y=" << frames[i].y << std::endl;
    }
    std::cout << "[Serialize] Frames around 110:" << std::endl;
    for (size_t i = 108; i < 115 && i < frames.size(); i++) {
        std::cout << "[Serialize]   frames[" << i << "]: x=" << frames[i].x << ", y=" << frames[i].y << std::endl;
    }

    for (size_t i = 0; i < frames.size(); i++) {
        const auto& frame = frames[i];
        int64_t delta = frame.time - lastTime;
        // Format floats to match osu! format
        ss << delta << "|";
        if (frame.x == (int)frame.x) {
            ss << (int)frame.x;
        } else {
            ss << std::fixed << std::setprecision(5) << frame.x;
        }
        ss << "|";
        if (frame.y == (int)frame.y) {
            ss << (int)frame.y;
        } else {
            ss << std::fixed << std::setprecision(5) << frame.y;
        }
        ss << "|0,";
        ss << std::defaultfloat;
        lastTime = frame.time;
    }

    // Add end marker with original seed (with trailing comma)
    // Note: scoreId is used for watermark in file footer, not in frame data
    ss << "-12345|0|0|" << seed << ",";
    return ss.str();
}

bool ReplayParser::save(const std::string& filepath, const ReplayInfo& info) {
    std::vector<uint8_t> buffer;

    // Recalculate replay hash
    std::string newReplayHash = calculateReplayHash(info);

    // Write header
    writeByte(buffer, (uint8_t)info.gameMode);
    writeInt32(buffer, info.gameVersion);
    writeOsuString(buffer, info.beatmapHash);
    writeOsuString(buffer, info.playerName);
    writeOsuString(buffer, newReplayHash.empty() ? info.replayHash : newReplayHash);

    // osu! format order
    writeInt16(buffer, (int16_t)info.count300);
    writeInt16(buffer, (int16_t)info.count100);
    writeInt16(buffer, (int16_t)info.count50);
    writeInt16(buffer, (int16_t)info.count300g);
    writeInt16(buffer, (int16_t)info.count200);
    writeInt16(buffer, (int16_t)info.countMiss);

    writeInt32(buffer, info.totalScore);
    writeInt16(buffer, (int16_t)info.maxCombo);
    writeByte(buffer, info.perfectCombo ? 1 : 0);
    writeInt32(buffer, info.mods);

    // Life bar graph (empty)
    writeOsuString(buffer, "");

    writeInt64(buffer, info.timestamp);

    // Compress frame data
    std::string frameData = serializeFrames(info.frames, info.seed, info.onlineScoreId);
    std::vector<uint8_t> frameBytes(frameData.begin(), frameData.end());
    std::vector<uint8_t> compressed;

    if (!compressLZMA(frameBytes, compressed)) {
        std::cerr << "Failed to compress replay data" << std::endl;
        return false;
    }

    writeInt32(buffer, (int32_t)compressed.size());
    buffer.insert(buffer.end(), compressed.begin(), compressed.end());

    // Write online score ID at the end (8 bytes)
    writeInt64(buffer, info.onlineScoreId);

    // Write Target mode extra data (if Target mod is enabled)
    const int TargetMod = 8388608;
    if (info.mods & TargetMod) {
        uint64_t raw;
        memcpy(&raw, &info.targetAccuracy, sizeof(double));
        writeInt64(buffer, (int64_t)raw);
    }

    // Write to file
#ifdef _WIN32
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, nullptr, 0);
    std::wstring widePath(wideLen - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, &widePath[0], wideLen);
    std::ofstream file(widePath, std::ios::binary);
#else
    std::ofstream file(filepath, std::ios::binary);
#endif

    if (!file) {
        std::cerr << "Failed to create replay file: " << filepath << std::endl;
        return false;
    }

    file.write((char*)buffer.data(), buffer.size());
    file.close();

    std::cout << "Replay saved: " << filepath << std::endl;
    return true;
}

void ReplayParser::mirrorKeys(ReplayInfo& info, int keyCount) {
    // Mirror key states for mania mode
    // For 7K: 0<->6, 1<->5, 2<->4, 3 stays
    // For 4K: 0<->3, 1<->2

    std::cout << "[Mirror] Starting mirror with keyCount=" << keyCount << ", frames=" << info.frames.size() << std::endl;

    // Debug: print first few frames BEFORE mirror
    std::cout << "[Mirror] Before modification:" << std::endl;
    for (size_t i = 0; i < 5 && i < info.frames.size(); i++) {
        std::cout << "[Mirror]   frames[" << i << "]: x=" << info.frames[i].x << ", y=" << info.frames[i].y << std::endl;
    }

    int modifiedCount = 0;
    int skippedCount = 0;

    for (size_t idx = 0; idx < info.frames.size(); idx++) {
        auto& frame = info.frames[idx];

        int oldState = (int)frame.x;  // In mania, x stores key state
        int newState = 0;

        for (int i = 0; i < keyCount; i++) {
            if (oldState & (1 << i)) {
                int mirroredKey = keyCount - 1 - i;
                newState |= (1 << mirroredKey);
            }
        }

        // Debug: print first few changes with actual index
        if (modifiedCount < 5 && oldState != newState) {
            std::cout << "[Mirror] frames[" << idx << "]: " << oldState << " -> " << newState << std::endl;
        }

        if (oldState != newState) {
            modifiedCount++;
        }

        frame.keyState = newState;
        frame.x = (float)newState;
    }

    // Debug: print first few frames AFTER mirror
    std::cout << "[Mirror] After modification:" << std::endl;
    for (size_t i = 0; i < 5 && i < info.frames.size(); i++) {
        std::cout << "[Mirror]   frames[" << i << "]: x=" << info.frames[i].x << ", y=" << info.frames[i].y << std::endl;
    }

    std::cout << "[Mirror] Done. Modified=" << modifiedCount << ", Skipped=" << skippedCount << std::endl;
}

// Watermark magic bytes: "MG" (0x4D, 0x47)
const uint16_t WATERMARK_MAGIC = 0x474D;  // Little-endian "MG"

int64_t ReplayParser::createWatermark() {
    // Get current time in milliseconds since epoch
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    // Format: [2 bytes magic][6 bytes timestamp]
    int64_t watermark = WATERMARK_MAGIC;  // Lower 2 bytes = magic
    watermark |= (ms << 16);  // Upper 6 bytes = timestamp

    return watermark;
}

bool ReplayParser::hasWatermark(int64_t onlineScoreId) {
    // Check if lower 2 bytes match magic
    uint16_t magic = (uint16_t)(onlineScoreId & 0xFFFF);
    return magic == WATERMARK_MAGIC;
}

int64_t ReplayParser::getWatermarkTime(int64_t onlineScoreId) {
    if (!hasWatermark(onlineScoreId)) {
        return 0;
    }
    // Extract upper 6 bytes as timestamp
    return (onlineScoreId >> 16) & 0xFFFFFFFFFFFF;
}
