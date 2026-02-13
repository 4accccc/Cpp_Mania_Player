#include "AcbParser.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <set>
#include <cstring>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>

namespace fs = std::filesystem;

std::vector<size_t> AcbParser::findHcaBlocks(const uint8_t* data, size_t size) {
    std::vector<size_t> blocks;
    const uint8_t hcaSig[] = {'H', 'C', 'A', 0x00};

    for (size_t i = 0; i + 4 <= size; i++) {
        if (memcmp(data + i, hcaSig, 4) == 0) {
            blocks.push_back(i);
        }
    }
    return blocks;
}

size_t AcbParser::getHcaSize(const uint8_t* data, size_t offset, size_t totalSize) {
    if (offset + 8 > totalSize) return 0;

    // HCA header: "HCA\0" (4) + version (2) + header_size (2, big-endian)
    uint16_t headerSize = (data[offset + 6] << 8) | data[offset + 7];

    // Find fmt chunk for block count
    size_t fmtPos = 0;
    for (size_t i = offset; i < offset + headerSize && i + 4 <= totalSize; i++) {
        if (memcmp(data + i, "fmt\x00", 4) == 0) {
            fmtPos = i;
            break;
        }
    }
    if (fmtPos == 0 || fmtPos + 12 > totalSize) return headerSize;

    // Block count at fmt+8 (4 bytes, big-endian)
    uint32_t blockCount = (data[fmtPos + 8] << 24) | (data[fmtPos + 9] << 16) |
                          (data[fmtPos + 10] << 8) | data[fmtPos + 11];

    // Find comp chunk for block size
    size_t compPos = 0;
    for (size_t i = offset; i < offset + headerSize && i + 4 <= totalSize; i++) {
        if (memcmp(data + i, "comp", 4) == 0) {
            compPos = i;
            break;
        }
    }
    if (compPos == 0 || compPos + 6 > totalSize) return headerSize;

    // Block size at comp+4 (2 bytes, big-endian)
    uint16_t blockSize = (data[compPos + 4] << 8) | data[compPos + 5];

    return headerSize + (blockCount * blockSize);
}

int64_t AcbParser::getHcaDuration(const uint8_t* data, size_t size) {
    if (size < 16) return 0;

    // Find fmt chunk
    size_t fmtPos = 0;
    for (size_t i = 0; i + 4 <= size && i < 256; i++) {
        if (memcmp(data + i, "fmt\x00", 4) == 0) {
            fmtPos = i;
            break;
        }
    }
    if (fmtPos == 0 || fmtPos + 12 > size) return 0;

    // Sample rate: bits 4-23 of bytes 5-7 (big-endian, top 4 bits are channels)
    uint32_t sampleRate = ((data[fmtPos + 5] & 0x0F) << 16) |
                          (data[fmtPos + 6] << 8) | data[fmtPos + 7];

    // Block count at fmt+8
    uint32_t blockCount = (data[fmtPos + 8] << 24) | (data[fmtPos + 9] << 16) |
                          (data[fmtPos + 10] << 8) | data[fmtPos + 11];

    // Each block is 1024 samples
    if (sampleRate == 0) return 0;
    int64_t totalSamples = blockCount * 1024LL;
    return (totalSamples * 1000) / sampleRate;
}

bool AcbParser::parse(const std::string& path, std::vector<HcaData>& hcaFiles) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;

    size_t fileSize = file.tellg();
    file.seekg(0);

    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    file.close();

    auto blocks = findHcaBlocks(data.data(), fileSize);
    if (blocks.empty()) return false;

    hcaFiles.clear();
    for (size_t i = 0; i < blocks.size(); i++) {
        size_t offset = blocks[i];
        size_t size = getHcaSize(data.data(), offset, fileSize);
        if (size == 0) continue;

        HcaData hca;
        hca.data.assign(data.begin() + offset, data.begin() + offset + size);
        hca.cueName = std::to_string(i);
        hca.duration = getHcaDuration(hca.data.data(), hca.data.size());
        hcaFiles.push_back(std::move(hca));
    }

    return !hcaFiles.empty();
}

bool AcbParser::extractToDir(const std::string& acbPath, const std::string& outputDir) {
    std::vector<HcaData> hcaFiles;
    if (!parse(acbPath, hcaFiles)) return false;

    fs::create_directories(outputDir);

    for (size_t i = 0; i < hcaFiles.size(); i++) {
        std::string outPath = outputDir + "/" + std::to_string(i) + ".hca";
        std::ofstream out(outPath, std::ios::binary);
        if (out) {
            out.write(reinterpret_cast<const char*>(hcaFiles[i].data.data()),
                      hcaFiles[i].data.size());
        }
    }
    return true;
}

int AcbParser::findBgmIndex(const std::vector<HcaData>& hcaFiles) {
    int bgmIndex = -1;
    int64_t maxDuration = 0;

    for (size_t i = 0; i < hcaFiles.size(); i++) {
        if (hcaFiles[i].duration > maxDuration) {
            maxDuration = hcaFiles[i].duration;
            bgmIndex = static_cast<int>(i);
        }
    }
    return bgmIndex;
}

std::vector<AcbCueInfo> AcbParser::extractCueNames(const std::string& path) {
    std::vector<AcbCueInfo> cues;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return cues;

    size_t fileSize = file.tellg();
    file.seekg(0);

    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    file.close();

    // First, count HCA blocks to know how many cue names we need
    auto hcaBlocks = findHcaBlocks(data.data(), fileSize);
    size_t hcaCount = hcaBlocks.size();
    if (hcaCount == 0) return cues;

    // Find the first HCA block position - cue names are before this
    size_t firstHcaPos = hcaBlocks[0];

    // Blacklist of metadata strings
    std::set<std::string> blacklist = {
        "CueName", "CueIndex", "CueTable", "WaveformTable", "SynthTable",
        "CommandTable", "TrackTable", "SequenceTable", "BlockTable",
        "AisacTable", "GraphTable", "GlobalAisacTable", "OutsideLinkTable",
        "StringValueTable", "Header", "AwbFile", "StreamAwbHash"
    };

    std::vector<std::string> names;
    std::string current;

    // Scan from 0x4000 to first HCA position (cue names are in header region)
    size_t startOffset = std::min((size_t)0x4000, fileSize);
    size_t endOffset = std::min(firstHcaPos, fileSize);

    for (size_t i = startOffset; i < endOffset; i++) {
        uint8_t c = data[i];
        if (c >= 32 && c < 127 && c != '\\' && c != '/') {
            current += static_cast<char>(c);
        } else if (c == 0 && current.length() >= 3 && current.length() <= 30) {
            bool valid = true;
            for (char ch : current) {
                if (!isalnum(ch) && ch != '_') {
                    valid = false;
                    break;
                }
            }
            if (valid && current[0] != '@' &&
                blacklist.find(current) == blacklist.end() &&
                current.find("Table") == std::string::npos) {
                names.push_back(current);
            }
            current.clear();
        } else {
            current.clear();
        }
    }

    // Only use up to hcaCount cue names
    size_t count = std::min(names.size(), hcaCount);
    for (size_t i = 0; i < count; i++) {
        AcbCueInfo cue;
        cue.name = names[i];
        cue.hcaIndex = static_cast<int>(i);
        cues.push_back(cue);
    }

    return cues;
}

bool AcbParser::convertHcaToWav(const std::string& hcaPath, const std::string& wavPath) {
    // Use FFmpeg to convert HCA to WAV
    std::string cmd = "ffmpeg -y -i \"" + hcaPath + "\" -acodec pcm_s16le \"" + wavPath + "\" 2>nul";
    int result = system(cmd.c_str());
    return result == 0 && fs::exists(wavPath);
}

// Parallel conversion helper: worker threads pull jobs from a shared queue
static void convertWorker(std::queue<std::pair<std::string, std::string>>& jobs,
                          std::mutex& jobMutex) {
    while (true) {
        std::pair<std::string, std::string> job;
        {
            std::lock_guard<std::mutex> lock(jobMutex);
            if (jobs.empty()) return;
            job = jobs.front();
            jobs.pop();
        }
        AcbParser::convertHcaToWav(job.first, job.second);
        // Remove HCA file after conversion
        fs::remove(job.first);
    }
}

bool AcbParser::extractAndConvert(const std::string& acbPath, const std::string& outputDir) {
    std::vector<HcaData> hcaFiles;
    if (!parse(acbPath, hcaFiles)) return false;

    fs::create_directories(outputDir);

    // Find BGM index (longest audio)
    int bgmIndex = findBgmIndex(hcaFiles);

    // Phase 1: Write all HCA files to disk
    std::queue<std::pair<std::string, std::string>> jobs;
    for (size_t i = 0; i < hcaFiles.size(); i++) {
        std::string baseName = std::to_string(i);
        std::string hcaPath = outputDir + "/" + baseName + ".hca";
        std::string wavPath = outputDir + "/" + baseName + ".wav";

        std::ofstream out(hcaPath, std::ios::binary);
        if (out) {
            out.write(reinterpret_cast<const char*>(hcaFiles[i].data.data()),
                      hcaFiles[i].data.size());
            out.close();
            jobs.push({hcaPath, wavPath});
        }
    }

    // Phase 2: Parallel FFmpeg conversion
    unsigned int maxThreads = std::max(1u, std::thread::hardware_concurrency());
    unsigned int numThreads = std::min(maxThreads, static_cast<unsigned int>(jobs.size()));
    std::mutex jobMutex;

    std::vector<std::thread> workers;
    for (unsigned int t = 0; t < numThreads; t++) {
        workers.emplace_back(convertWorker, std::ref(jobs), std::ref(jobMutex));
    }
    for (auto& w : workers) {
        w.join();
    }

    // Create BGM copy for easy access
    if (bgmIndex >= 0) {
        std::string bgmWav = outputDir + "/" + std::to_string(bgmIndex) + ".wav";
        std::string bgmLink = outputDir + "/bgm.wav";
        if (fs::exists(bgmWav)) {
            fs::copy_file(bgmWav, bgmLink, fs::copy_options::overwrite_existing);
        }
    }

    return true;
}

// --- @UTF table parser implementation ---

static inline uint16_t readBE16(const uint8_t* p) { return (p[0] << 8) | p[1]; }
static inline uint32_t readBE32(const uint8_t* p) { return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

static size_t utfDataTypeSize(uint8_t dt) {
    switch (dt) {
        case 0: case 1: return 1;
        case 2: case 3: return 2;
        case 4: case 5: return 4;
        case 6: case 7: return 8;
        case 8: return 4;
        case 9: return 8;
        case 0xA: return 4; // string offset
        case 0xB: return 8; // blob offset + size
        default: return 0;
    }
}

bool AcbParser::parseUtfTable(const uint8_t* data, size_t size, size_t baseOffset, UtfTable& table) {
    (void)baseOffset; // offsets are now relative to data pointer
    if (size < 32) return false;
    if (memcmp(data, "@UTF", 4) != 0) return false;

    uint32_t tableSize = readBE32(data + 4);
    (void)tableSize;

    // Header fields after @UTF(4) + tableSize(4), all relative to base (data+8)
    // +0x00: uint16 encoding type
    // +0x02: uint16 rows offset (relative to base)
    // +0x04: uint32 string table offset (relative to base)
    // +0x08: uint32 data offset (relative to base)
    // +0x0C: uint32 table name string offset
    // +0x10: uint16 num columns
    // +0x12: uint16 row length
    // +0x14: uint32 num rows
    if (size < 8 + 0x18) return false;
    const uint8_t* base = data + 8;

    // uint16_t encoding = readBE16(base + 0x00);
    uint16_t rowsOff = readBE16(base + 0x02);
    uint32_t stringOff = readBE32(base + 0x04);
    uint32_t dataOff = readBE32(base + 0x08);
    uint32_t tableNameOff = readBE32(base + 0x0C);
    uint16_t numColumns = readBE16(base + 0x10);
    uint16_t rowLength = readBE16(base + 0x12);
    uint32_t numRows = readBE32(base + 0x14);

    table.data = data;
    table.dataSize = size;
    // All offsets relative to table.data
    table.rowsOffset = 8 + rowsOff;
    table.stringOffset = 8 + stringOff;
    table.dataOffset = 8 + dataOff;
    table.rowCount = numRows;
    table.rowLength = rowLength;

    // Read table name
    size_t nameAbs = table.stringOffset + tableNameOff;
    if (nameAbs < size) {
        table.tableName = reinterpret_cast<const char*>(data + nameAbs);
    }

    // Parse column definitions starting at base + 0x18 (after 24-byte header)
    size_t pos = 0x18;
    size_t baseSize = size - 8;
    table.columns.clear();
    table.fieldOffsets.clear();
    size_t rowFieldOffset = 0;

    for (uint16_t c = 0; c < numColumns; c++) {
        if (pos >= baseSize) return false;
        uint8_t flags = base[pos++];
        uint8_t storageType = (flags >> 4) & 0xF;
        uint8_t dataType = flags & 0xF;

        UtfColumn col;
        col.storageType = storageType;
        col.dataType = dataType;

        // Read column name (for named/per-row columns)
        if (storageType == 1 || storageType == 3 || storageType == 5) {
            if (pos + 4 > baseSize) return false;
            uint32_t nameOff = readBE32(base + pos);
            pos += 4;
            size_t abs = table.stringOffset + nameOff;
            if (abs < size) {
                col.name = reinterpret_cast<const char*>(data + abs);
            }
        }

        // Skip constant value (only storageType 3 has value bytes in schema;
        // storageType 1 = zero/default with no inline value)
        if (storageType == 3) {
            pos += utfDataTypeSize(dataType);
        }

        // Track per-row field offsets
        if (storageType == 5) {
            table.fieldOffsets.push_back(rowFieldOffset);
            rowFieldOffset += utfDataTypeSize(dataType);
        } else {
            table.fieldOffsets.push_back((size_t)-1); // not per-row
        }

        table.columns.push_back(col);
    }

    table.schemaEnd = pos;
    return true;
}

std::string AcbParser::readUtfString(const UtfTable& table, size_t stringOff) {
    size_t abs = table.stringOffset + stringOff;
    if (abs >= table.dataSize) return "";
    return reinterpret_cast<const char*>(table.data + abs);
}

int AcbParser::findColumn(const UtfTable& table, const std::string& name) {
    for (int i = 0; i < (int)table.columns.size(); i++) {
        if (table.columns[i].name == name) return i;
    }
    return -1;
}

uint32_t AcbParser::readUtfU32(const UtfTable& table, uint32_t row, int colIdx) {
    if (colIdx < 0 || colIdx >= (int)table.columns.size()) return 0;
    if (table.columns[colIdx].storageType != 5) return 0;
    size_t off = table.rowsOffset + (size_t)row * table.rowLength + table.fieldOffsets[colIdx];
    if (off + 4 > table.dataSize) return 0;
    return readBE32(table.data + off);
}

uint16_t AcbParser::readUtfU16(const UtfTable& table, uint32_t row, int colIdx) {
    if (colIdx < 0 || colIdx >= (int)table.columns.size()) return 0;
    if (table.columns[colIdx].storageType != 5) return 0;
    size_t off = table.rowsOffset + (size_t)row * table.rowLength + table.fieldOffsets[colIdx];
    if (off + 2 > table.dataSize) return 0;
    return readBE16(table.data + off);
}

std::string AcbParser::readUtfStringField(const UtfTable& table, uint32_t row, int colIdx) {
    if (colIdx < 0 || colIdx >= (int)table.columns.size()) return "";
    if (table.columns[colIdx].storageType != 5) return "";
    size_t off = table.rowsOffset + (size_t)row * table.rowLength + table.fieldOffsets[colIdx];
    if (off + 4 > table.dataSize) return "";
    uint32_t strOff = readBE32(table.data + off);
    size_t abs = table.stringOffset + strOff;
    if (abs >= table.dataSize) return "";
    return reinterpret_cast<const char*>(table.data + abs);
}

std::pair<size_t, size_t> AcbParser::readUtfBlob(const UtfTable& table, uint32_t row, int colIdx) {
    if (colIdx < 0 || colIdx >= (int)table.columns.size()) return {0, 0};
    if (table.columns[colIdx].storageType != 5) return {0, 0};
    size_t off = table.rowsOffset + (size_t)row * table.rowLength + table.fieldOffsets[colIdx];
    if (off + 8 > table.dataSize) return {0, 0};
    uint32_t blobOff = readBE32(table.data + off);
    uint32_t blobSize = readBE32(table.data + off + 4);
    return {table.dataOffset + blobOff, blobSize};
}

std::unordered_map<std::string, int> AcbParser::parseCueToAwbMapping(const std::string& path) {
    std::unordered_map<std::string, int> result;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) { std::cerr << "[ACB] Failed to open file" << std::endl; return result; }
    size_t fileSize = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    file.close();

    // Parse root @UTF table (Header)
    UtfTable root;
    if (!parseUtfTable(data.data(), fileSize, 0, root)) {
        std::cerr << "[ACB] Failed to parse root UTF table" << std::endl;
        return result;
    }
    std::cerr << "[ACB] Root table: " << root.tableName << ", " << root.columns.size()
              << " cols, " << root.rowCount << " rows" << std::endl;

    // Find CueNameTable and WaveformTable blob columns in root
    int cueNameTableCol = findColumn(root, "CueNameTable");
    int waveformTableCol = findColumn(root, "WaveformTable");
    std::cerr << "[ACB] CueNameTable col=" << cueNameTableCol
              << " WaveformTable col=" << waveformTableCol << std::endl;
    if (cueNameTableCol < 0 || waveformTableCol < 0) {
        // Dump all column names for debugging
        for (int i = 0; i < (int)root.columns.size(); i++) {
            std::cerr << "[ACB]   col[" << i << "] name='" << root.columns[i].name
                      << "' storage=" << (int)root.columns[i].storageType
                      << " type=" << (int)root.columns[i].dataType << std::endl;
        }
        return result;
    }

    if (root.rowCount == 0) { std::cerr << "[ACB] Root has 0 rows" << std::endl; return result; }

    // Check storage type of blob columns
    std::cerr << "[ACB] CueNameTable storage=" << (int)root.columns[cueNameTableCol].storageType
              << " type=" << (int)root.columns[cueNameTableCol].dataType << std::endl;
    std::cerr << "[ACB] WaveformTable storage=" << (int)root.columns[waveformTableCol].storageType
              << " type=" << (int)root.columns[waveformTableCol].dataType << std::endl;

    // Debug: dump per-row field offsets for blob columns
    std::cerr << "[ACB] CueNameTable fieldOffset=" << root.fieldOffsets[cueNameTableCol]
              << " WaveformTable fieldOffset=" << root.fieldOffsets[waveformTableCol] << std::endl;
    std::cerr << "[ACB] rowsOffset=" << root.rowsOffset << " rowLength=" << root.rowLength
              << " dataOffset=" << root.dataOffset << " stringOffset=" << root.stringOffset << std::endl;

    // Debug: dump raw blob field bytes
    {
        size_t blobFieldPos = root.rowsOffset + root.fieldOffsets[cueNameTableCol];
        std::cerr << "[ACB] Blob field at file offset " << blobFieldPos << ": ";
        for (int db = 0; db < 8 && blobFieldPos + db < fileSize; db++)
            std::cerr << std::hex << (int)data[blobFieldPos + db] << " ";
        std::cerr << std::dec << std::endl;
    }

    // Read CueNameTable blob
    auto [cntOff, cntSize] = readUtfBlob(root, 0, cueNameTableCol);
    std::cerr << "[ACB] CueNameTable blob: off=" << cntOff << " size=" << cntSize << std::endl;
    if (cntOff + 4 <= fileSize) {
        std::cerr << "[ACB] CueNameTable first 4 bytes: "
                  << std::hex << (int)data[cntOff] << " " << (int)data[cntOff+1] << " "
                  << (int)data[cntOff+2] << " " << (int)data[cntOff+3] << std::dec << std::endl;
    }
    if (cntSize == 0 || cntOff + cntSize > fileSize) {
        std::cerr << "[ACB] CueNameTable blob invalid (fileSize=" << fileSize << ")" << std::endl;
        return result;
    }

    UtfTable cueNameTable;
    if (!parseUtfTable(data.data() + cntOff, cntSize, cntOff, cueNameTable)) {
        std::cerr << "[ACB] Failed to parse CueNameTable (expected @UTF at offset " << cntOff << ")" << std::endl;
        return result;
    }
    std::cerr << "[ACB] CueNameTable: " << cueNameTable.rowCount << " rows, "
              << cueNameTable.columns.size() << " cols" << std::endl;

    int cueNameCol = findColumn(cueNameTable, "CueName");
    int cueIndexCol = findColumn(cueNameTable, "CueIndex");
    std::cerr << "[ACB] CueName col=" << cueNameCol << " CueIndex col=" << cueIndexCol << std::endl;
    if (cueNameCol < 0 || cueIndexCol < 0) return result;

    // Read WaveformTable blob
    auto [wftOff, wftSize] = readUtfBlob(root, 0, waveformTableCol);
    std::cerr << "[ACB] WaveformTable blob: off=" << wftOff << " size=" << wftSize << std::endl;
    if (wftSize == 0 || wftOff + wftSize > fileSize) {
        std::cerr << "[ACB] WaveformTable blob invalid" << std::endl;
        return result;
    }

    UtfTable waveformTable;
    if (!parseUtfTable(data.data() + wftOff, wftSize, wftOff, waveformTable)) {
        std::cerr << "[ACB] Failed to parse WaveformTable" << std::endl;
        return result;
    }
    std::cerr << "[ACB] WaveformTable: " << waveformTable.rowCount << " rows" << std::endl;

    int memAwbIdCol = findColumn(waveformTable, "MemoryAwbId");
    std::cerr << "[ACB] MemoryAwbId col=" << memAwbIdCol << std::endl;
    if (memAwbIdCol < 0) return result;

    // Build mapping: cueName -> MemoryAwbId
    for (uint32_t r = 0; r < cueNameTable.rowCount; r++) {
        std::string name = readUtfStringField(cueNameTable, r, cueNameCol);
        uint16_t cueIdx = readUtfU16(cueNameTable, r, cueIndexCol);

        if (name.empty() || cueIdx >= waveformTable.rowCount) continue;

        uint16_t awbId = readUtfU16(waveformTable, cueIdx, memAwbIdCol);
        result[name] = awbId;
    }

    std::cerr << "[ACB] Parsed cue mapping: " << result.size() << " entries" << std::endl;
    return result;
}

bool AcbParser::extractAndConvertMapped(const std::string& acbPath, const std::string& outputDir,
                                         const std::unordered_map<std::string, int>& cueToAwb) {
    std::vector<HcaData> hcaFiles;
    if (!parse(acbPath, hcaFiles)) return false;

    fs::create_directories(outputDir);

    // Build reverse map: AwbId -> list of cue names
    std::unordered_map<int, std::vector<std::string>> awbToCues;
    for (const auto& [name, awbId] : cueToAwb) {
        awbToCues[awbId].push_back(name);
    }

    // Phase 1: Write HCA files to disk, named by cue name
    std::queue<std::pair<std::string, std::string>> jobs;
    int bgmAwbId = -1;
    int64_t maxDuration = 0;

    for (size_t i = 0; i < hcaFiles.size(); i++) {
        int awbId = static_cast<int>(i);
        auto it = awbToCues.find(awbId);

        std::string baseName;
        if (it != awbToCues.end() && !it->second.empty()) {
            baseName = it->second[0]; // primary cue name
        } else {
            baseName = std::to_string(i); // fallback
        }

        std::string hcaPath = outputDir + "/" + baseName + ".hca";
        std::string wavPath = outputDir + "/" + baseName + ".wav";

        // Track BGM (longest audio)
        if (hcaFiles[i].duration > maxDuration) {
            maxDuration = hcaFiles[i].duration;
            bgmAwbId = awbId;
        }

        if (fs::exists(wavPath)) continue;

        std::ofstream out(hcaPath, std::ios::binary);
        if (out) {
            out.write(reinterpret_cast<const char*>(hcaFiles[i].data.data()),
                      hcaFiles[i].data.size());
            out.close();
            jobs.push({hcaPath, wavPath});
        }

        // Write aliases (if multiple cue names point to same AWB ID)
        if (it != awbToCues.end()) {
            for (size_t j = 1; j < it->second.size(); j++) {
                std::string aliasWav = outputDir + "/" + it->second[j] + ".wav";
                if (!fs::exists(aliasWav)) {
                    // Will be copied after conversion
                }
            }
        }
    }

    // Phase 2: Parallel FFmpeg conversion
    if (!jobs.empty()) {
        unsigned int maxThreads = std::max(1u, std::thread::hardware_concurrency());
        unsigned int numThreads = std::min(maxThreads, static_cast<unsigned int>(jobs.size()));
        std::mutex jobMutex;

        std::cerr << "[ACB] Converting " << jobs.size() << " HCA files with "
                  << numThreads << " threads" << std::endl;

        std::vector<std::thread> workers;
        for (unsigned int t = 0; t < numThreads; t++) {
            workers.emplace_back(convertWorker, std::ref(jobs), std::ref(jobMutex));
        }
        for (auto& w : workers) {
            w.join();
        }
    }

    // Phase 3: Create aliases for shared AWB IDs
    for (const auto& [awbId, names] : awbToCues) {
        if (names.size() <= 1) continue;
        std::string primaryWav = outputDir + "/" + names[0] + ".wav";
        if (!fs::exists(primaryWav)) continue;
        for (size_t j = 1; j < names.size(); j++) {
            std::string aliasWav = outputDir + "/" + names[j] + ".wav";
            if (!fs::exists(aliasWav)) {
                std::error_code ec;
                fs::copy_file(primaryWav, aliasWav, fs::copy_options::overwrite_existing, ec);
            }
        }
    }

    // Create bgm.wav link
    if (bgmAwbId >= 0) {
        auto it = awbToCues.find(bgmAwbId);
        std::string bgmName = (it != awbToCues.end() && !it->second.empty())
                              ? it->second[0] : std::to_string(bgmAwbId);
        bool isBgm = (bgmName == "BGM" || bgmName == "bgm" || bgmName == "Bgm");
        if (!isBgm) {
            std::string bgmWav = outputDir + "/" + bgmName + ".wav";
            std::string bgmLink = outputDir + "/bgm.wav";
            if (fs::exists(bgmWav)) {
                std::error_code ec;
                fs::copy_file(bgmWav, bgmLink, fs::copy_options::overwrite_existing, ec);
            }
        }
    }

    return true;
}

bool AcbParser::extractAndConvert(const std::string& acbPath, const std::string& outputDir,
                                  const std::vector<std::string>& cueNames) {
    std::vector<HcaData> hcaFiles;
    if (!parse(acbPath, hcaFiles)) return false;

    fs::create_directories(outputDir);

    // Find BGM index (longest audio)
    int bgmIndex = findBgmIndex(hcaFiles);

    // Phase 1: Write all HCA files to disk
    std::queue<std::pair<std::string, std::string>> jobs;
    for (size_t i = 0; i < hcaFiles.size(); i++) {
        std::string baseName = (i < cueNames.size()) ? cueNames[i] : std::to_string(i);
        std::string hcaPath = outputDir + "/" + baseName + ".hca";
        std::string wavPath = outputDir + "/" + baseName + ".wav";

        // Skip if WAV already exists (per-file cache)
        if (fs::exists(wavPath)) continue;

        std::ofstream out(hcaPath, std::ios::binary);
        if (out) {
            out.write(reinterpret_cast<const char*>(hcaFiles[i].data.data()),
                      hcaFiles[i].data.size());
            out.close();
            jobs.push({hcaPath, wavPath});
        }
    }

    // Phase 2: Parallel FFmpeg conversion
    if (!jobs.empty()) {
        unsigned int maxThreads = std::max(1u, std::thread::hardware_concurrency());
        unsigned int numThreads = std::min(maxThreads, static_cast<unsigned int>(jobs.size()));
        std::mutex jobMutex;

        std::cerr << "[MUSYNX] Converting " << jobs.size() << " HCA files with "
                  << numThreads << " threads" << std::endl;

        std::vector<std::thread> workers;
        for (unsigned int t = 0; t < numThreads; t++) {
            workers.emplace_back(convertWorker, std::ref(jobs), std::ref(jobMutex));
        }
        for (auto& w : workers) {
            w.join();
        }
    }

    // Create BGM copy for easy access (skip if same file on case-insensitive systems)
    if (bgmIndex >= 0 && bgmIndex < (int)cueNames.size()) {
        std::string bgmName = cueNames[bgmIndex];
        // Check if cue name is already "BGM" (case-insensitive)
        bool isBgm = (bgmName == "BGM" || bgmName == "bgm" || bgmName == "Bgm");
        if (!isBgm) {
            std::string bgmWav = outputDir + "/" + bgmName + ".wav";
            std::string bgmLink = outputDir + "/bgm.wav";
            if (fs::exists(bgmWav)) {
                std::error_code ec;
                fs::copy_file(bgmWav, bgmLink, fs::copy_options::overwrite_existing, ec);
            }
        }
    }

    return true;
}
